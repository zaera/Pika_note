"""Offline updater tests: identity, checksums, archive bounds and flash ordering."""
import hashlib
import importlib.util
import io
import struct
import tempfile
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('flash_pika', ROOT / 'tools/flash_pika.py')
flasher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flasher)


def port(device, serial='7C:E8:B1:E1:6D:20', location='usb-1', vid=0x303A):
    return SimpleNamespace(device=device, serial_number=serial, location=location,
                           vid=vid, pid=0x1001, description='Fixture Pika')


def partition_table():
    rows = []
    for name, kind, subtype, offset, size in [
        ('nvs', 1, 2, 0x9000, 0x5000), ('otadata', 1, 0, 0xE000, 0x2000),
        ('app0', 0, 0x10, 0x10000, 0x330000), ('app1', 0, 0x11, 0x340000, 0x330000)]:
        rows.append(struct.pack('<HBBII16sI', 0x50AA, kind, subtype, offset, size, name.encode(), 0))
    return b''.join(rows).ljust(3072, b'\xff')


class FlasherChecks(unittest.TestCase):
    def test_previews_and_drafts(self):
        assets = [{'name': 'pika-note-firmware-8.0.5.zip'}, {'name': 'SHA256SUMS.txt'}]
        stable = dict(tag_name='old', published_at='2026-11-01', assets=assets)
        preview = dict(tag_name='new', published_at='2026-11-03', assets=assets, prerelease=True)
        draft = dict(tag_name='draft', published_at='2026-11-04', assets=assets, draft=True)
        self.assertEqual(flasher.choose_release([stable, draft, preview])['tag_name'], 'new')
        self.assertEqual(flasher.choose_release([stable, draft, preview], True)['tag_name'], 'old')

    def test_tracks_identity_not_port_number(self):
        original = port('COM20')
        other = port('COM16', 'another-board', 'usb-2')
        boot = port('COM19', '7ce8b1e16d20')
        self.assertIs(flasher.boot_candidate(original, {'COM16', 'COM20'}, [other, boot]), boot)
        self.assertIsNone(flasher.boot_candidate(original, {'COM16', 'COM20'}, [other]))
        self.assertIsNone(flasher.boot_candidate(original, {'COM20'}, [boot, port('COM21')]))

    def test_mismatching_identity_does_not_fall_back_to_com_number(self):
        self.assertIsNone(flasher.boot_candidate(port('COM20'), {'COM20'}, [port('COM20', 'wrong', 'usb-9')]))

    def test_checksum_and_safe_extraction(self):
        memory = io.BytesIO()
        image = bytes([0xE9]) + bytes(11) + bytes([9, 0]) + bytes(100)
        with zipfile.ZipFile(memory, 'w') as z:
            z.writestr('firmware.bin', image)
            z.writestr('bootloader.bin', image)
            z.writestr('partitions.bin', partition_table())
            z.writestr('../outside.txt', 'must never be extracted')
        data = memory.getvalue()
        name = 'pika-note-firmware-8.0.5.zip'
        release = {'assets': [{'name': name, 'browser_download_url': 'https://example.invalid/fw'},
                              {'name': 'SHA256SUMS.txt', 'browser_download_url': 'https://example.invalid/sums'}]}
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            checks = (hashlib.sha256(data).hexdigest() + '  ' + name + '\n').encode()
            with patch.object(flasher, 'fetch', side_effect=[checks, data]):
                files = flasher.unpack_verified(release, root / 'download')
                self.assertEqual(files['firmware.bin'].read_bytes(), image)
                self.assertFalse((root / 'outside.txt').exists())
            with patch.object(flasher, 'fetch', side_effect=[b'0' * 64 + b'  ' + name.encode(), data]):
                with self.assertRaisesRegex(flasher.FlashError, 'checksum'):
                    flasher.unpack_verified(release, root / 'bad')
                self.assertFalse((root / 'bad').exists())

    def run_flash(self, mismatch=False, fail_write=False, install=False, size='8MB'):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        files = {name: root / name for name in ['firmware.bin', 'bootloader.bin', 'partitions.bin']}
        files['partitions.bin'].write_bytes(partition_table())
        calls = []

        def command(peer, arguments, after='no-reset'):
            calls.append((peer, arguments, after))
            if arguments[0] == 'flash-id':
                return 'Detected flash size: ' + size
            if arguments[0] == 'read-flash':
                Path(arguments[-1]).write_bytes((b'wrong' if mismatch else partition_table()).ljust(4096, b'\xff'))
            if arguments[0] == 'write-flash' and fail_write:
                raise flasher.FlashError('write failed')
            return ''

        with patch.object(flasher, 'esptool', side_effect=command):
            if mismatch or fail_write or size != '8MB':
                with self.assertRaises(flasher.FlashError):
                    flasher.flash('COM19', files, root, install)
            else:
                flasher.flash('COM19', files, root, install)
        return calls

    def test_verifies_layout_then_writes_then_selects_app0(self):
        calls = self.run_flash()
        self.assertEqual([c[1][0] for c in calls], ['flash-id', 'read-flash', 'write-flash', 'erase-region'])
        self.assertEqual(calls[2][1][1], '0x10000')
        self.assertEqual(calls[-1][1], ['erase-region', '0xe000', '0x2000'])
        self.assertEqual(calls[-1][2], 'hard-reset')

    def test_never_writes_on_layout_or_flash_size_mismatch(self):
        for calls in [self.run_flash(mismatch=True), self.run_flash(size='4MB')]:
            self.assertNotIn('write-flash', [c[1][0] for c in calls])
            self.assertNotIn('erase-region', [c[1][0] for c in calls])

    def test_failed_write_does_not_reset_boot_selection(self):
        calls = self.run_flash(fail_write=True)
        self.assertNotIn('erase-region', [c[1][0] for c in calls])

    def test_new_board_mode_writes_only_declared_images(self):
        calls = self.run_flash(install=True)
        write = calls[1][1]
        self.assertEqual(write[0], 'write-flash')
        self.assertIn('0x0', write)
        self.assertIn('0x8000', write)
        self.assertIn('0x10000', write)
        self.assertNotIn('--erase-all', write)


if __name__ == '__main__':
    unittest.main()
