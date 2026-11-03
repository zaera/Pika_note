#!/usr/bin/env python3
"""Download a verified Pika release, follow USB re-enumeration and flash it.

Python 3.10+. Double-click flash_pika.bat on Windows. No full-chip erase.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import venv
import zipfile

REPO = "zaera/Pika_note"
API = f"https://api.github.com/repos/{REPO}/releases?per_page=100"
MAX_DOWNLOAD = 16 * 1024 * 1024
DEPENDENCIES = ("esptool>=5.0,<6", "pyserial==3.5")


class FlashError(Exception):
    pass


def bootstrap():
    """Keep pip packages out of the user's global Python and source checkout."""
    try:
        import serial  # noqa: F401
        if importlib.metadata.version("esptool").split(".")[0] == "5":
            return
    except (ImportError, importlib.metadata.PackageNotFoundError):
        pass
    if os.environ.get("PIKA_FLASHER_BOOTSTRAPPED"):
        raise FlashError("Flasher dependencies could not be loaded. Remove the flasher cache and retry.")
    cache = Path(os.environ.get("LOCALAPPDATA", Path.home() / ".cache")) / "PikaNote" / "flasher"
    env = cache / f"python-{sys.version_info.major}.{sys.version_info.minor}"
    python = env / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    if not python.exists():
        print("Preparing a private Python environment (first run only)...", flush=True)
        venv.EnvBuilder(with_pip=True).create(env)
    subprocess.run([str(python), "-m", "pip", "install", "--disable-pip-version-check", *DEPENDENCIES], check=True)
    child_env = dict(os.environ, PIKA_FLASHER_BOOTSTRAPPED="1")
    raise SystemExit(subprocess.call([str(python), str(Path(__file__).resolve()), *sys.argv[1:]], env=child_env))


def fetch(url: str, limit: int = MAX_DOWNLOAD) -> bytes:
    if not url.startswith("https://"):
        raise FlashError("Release downloads must use HTTPS.")
    request = urllib.request.Request(url, headers={"User-Agent": "Pika-Note-Flasher", "Accept": "application/vnd.github+json"})
    try:
        with urllib.request.urlopen(request, timeout=45) as response:
            if not response.geturl().startswith("https://"):
                raise FlashError("An insecure download redirect was rejected.")
            data = response.read(limit + 1)
    except urllib.error.HTTPError as exc:
        if exc.code == 403:
            raise FlashError("GitHub rejected the download or its anonymous API limit was reached. Try again later.") from exc
        raise FlashError(f"Download failed (HTTP {exc.code}): {url}") from exc
    if len(data) > limit:
        raise FlashError("Release file exceeds the allowed download size.")
    return data


def choose_release(releases: list, stable: bool = False) -> dict:
    candidates = [r for r in releases if not r.get("draft") and (not stable or not r.get("prerelease"))]
    for release in sorted(candidates, key=lambda r: r.get("published_at") or "", reverse=True):
        assets = {a["name"]: a for a in release.get("assets", [])}
        bundles = [name for name in assets if re.fullmatch(r"pika-note-firmware-[0-9][\w.\-]*\.zip", name)]
        if len(bundles) == 1 and "SHA256SUMS.txt" in assets:
            return release
    raise FlashError("No compatible published firmware release with SHA256SUMS.txt was found.")


def unpack_verified(release: dict, destination: Path) -> dict[str, Path]:
    assets = {a["name"]: a for a in release["assets"]}
    name = next(n for n in assets if re.fullmatch(r"pika-note-firmware-[0-9][\w.\-]*\.zip", n))
    checks = fetch(assets["SHA256SUMS.txt"]["browser_download_url"], 65536).decode("utf-8-sig")
    matches = re.findall(r"^([0-9a-fA-F]{64})\s+\*?" + re.escape(name) + r"\s*$", checks, re.MULTILINE)
    if len(matches) != 1:
        raise FlashError("The firmware ZIP has no unique SHA-256 entry in the release.")
    data = fetch(assets[name]["browser_download_url"])
    if hashlib.sha256(data).hexdigest() != matches[0].lower():
        raise FlashError("Firmware checksum mismatch. Nothing was sent to the device.")
    destination.mkdir(parents=True, exist_ok=True)
    archive = destination / name
    archive.write_bytes(data)
    files = {}
    try:
        with zipfile.ZipFile(archive) as bundle:
            for member, maximum in (("firmware.bin", 0x330000), ("bootloader.bin", 0x8000), ("partitions.bin", 0x1000)):
                entries = [entry for entry in bundle.infolist() if entry.filename == member]
                if len(entries) != 1 or not 0 < entries[0].file_size <= maximum:
                    raise FlashError(f"Invalid or duplicate {member} in firmware bundle.")
                payload = bundle.read(entries[0])
                if member != "partitions.bin" and (len(payload) < 24 or payload[0] != 0xE9 or int.from_bytes(payload[12:14], "little") != 9):
                    raise FlashError(f"{member} is not an ESP32-S3 image.")
                path = destination / member
                path.write_bytes(payload)
                files[member] = path
    except zipfile.BadZipFile as exc:
        raise FlashError("Invalid firmware ZIP.") from exc
    validate_partitions(files["partitions.bin"].read_bytes())
    print(f"SHA-256 verified: {name}", flush=True)
    return files


def validate_partitions(data: bytes):
    """Check the expected user-data boundaries before any device write."""
    rows = {}
    for offset in range(0, len(data) - 31, 32):
        row = data[offset:offset + 32]
        if row[:2] != b"\xaa\x50":
            break
        label = row[12:28].split(b"\0", 1)[0].decode("ascii", errors="strict")
        rows[label] = (row[2], row[3], int.from_bytes(row[4:8], "little"), int.from_bytes(row[8:12], "little"))
    required = {"nvs": (1, 2, 0x9000, 0x5000), "otadata": (1, 0, 0xE000, 0x2000),
                "app0": (0, 0x10, 0x10000, 0x330000), "app1": (0, 0x11, 0x340000, 0x330000)}
    if any(rows.get(name) != expected for name, expected in required.items()):
        raise FlashError("Unsupported release partition layout. Update the flasher for this release.")


def ports():
    from serial.tools import list_ports
    return list(list_ports.comports())


def select_port(requested: str | None = None):
    available = ports()
    if not available:
        raise FlashError("No serial ports found. Connect Pika by a USB DATA cable and wake it, then retry.")
    print("\nAvailable serial ports:")
    for i, port in enumerate(available, 1):
        print(f"  {i}. {port.device} — {port.description}")
    while True:
        answer = requested or input("\nPika port (COM20, /dev/ttyACM0, or list number): ").strip()
        match = next((p for p in available if p.device.lower() == answer.lower()), None)
        if match is None and answer.isdigit() and 1 <= int(answer) <= len(available):
            match = available[int(answer) - 1]
        if match is not None:
            return match
        if requested:
            raise FlashError(f"Port {requested} is not present.")
        print("Enter one of the ports listed above.")


def serial_id(port) -> str:
    return re.sub(r"[^a-z0-9]", "", (port.serial_number or "").lower())


def boot_candidate(original, before: set[str], current: list):
    identified = [p for p in current if (serial_id(original) and serial_id(p) == serial_id(original))
                  or (original.location and p.location == original.location)]
    if len(identified) == 1:
        return identified[0]
    if len(identified) > 1:
        return None
    # If the original USB identity is known, never replace it with another device.
    if serial_id(original) or original.location:
        return None
    same = [p for p in current if p.device == original.device and p.vid == original.vid and p.pid == original.pid]
    new = [p for p in current if p.device not in before and p.vid == 0x303A]
    if len(same) == 1 and not new:
        return same[0]
    return new[0] if original.vid == 0x303A and len(new) == 1 and not same else None


def enter_bootloader(original, timeout: float = 35):
    import serial
    before = {p.device for p in ports()}
    print(f"\nEntering bootloader on {original.device} (USB 1200-baud touch)...", flush=True)
    touch_error = None
    try:
        # Opening the Arduino CDC port at 1200 may itself disconnect the port on
        # Windows. That exception is only accepted if the device reappears.
        with serial.Serial(original.device, 1200, timeout=0.2) as connection:
            connection.dtr = False
            connection.rts = False
    except (serial.SerialException, OSError) as exc:
        touch_error = exc
        message = str(exc).lower()
        if "access is denied" in message or "access denied" in message or "permission denied" in message:
            raise FlashError("Port is busy. Close Cura, serial monitors and other tools using Pika, then retry.") from exc
    time.sleep(1.5)
    end = time.monotonic() + timeout
    last = None
    while time.monotonic() < end:
        candidate = boot_candidate(original, before, ports())
        if candidate is not None and last == candidate.device:
            print(f"Bootloader port: {candidate.device}", flush=True)
            return candidate.device
        last = candidate.device if candidate else None
        time.sleep(0.4)
    detail = f" ({touch_error})" if touch_error else ""
    raise FlashError("Pika did not reappear in bootloader mode. Reconnect USB and retry; check the board's manual boot instructions." + detail)


def esptool(port: str, arguments: list[str], after: str = "no-reset") -> str:
    command = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port,
               "--baud", "921600", "--before", "no-reset", "--after", after, *arguments]
    output = []
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                          encoding="utf-8", errors="replace") as process:
        assert process.stdout is not None
        for line in process.stdout:
            print(line, end="", flush=True)
            output.append(line)
        code = process.wait()
    if code:
        raise FlashError("esptool failed. No full-chip erase was requested. Keep USB connected and retry; the device may remain in bootloader.")
    return "".join(output)


def flash(port: str, files: dict[str, Path], workspace: Path, install: bool = False):
    identity = esptool(port, ["flash-id"])
    if not re.search(r"Detected flash size:\s*8\s*MB", identity, re.IGNORECASE):
        raise FlashError("This build requires the 8 MB ESP32-S3 board. Flash size did not match.")
    if not install:
        existing = workspace / "device-partitions.bin"
        esptool(port, ["read-flash", "0x8000", "0x1000", str(existing)])
        expected = files["partitions.bin"].read_bytes()
        if existing.read_bytes()[:len(expected)] != expected:
            raise FlashError("Device partition table differs. Nothing was flashed. For a NEW/ERASED matching board use --install; do not use it to migrate an unknown populated layout.")
    arguments = ["write-flash"]
    if install:
        arguments += ["--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "8MB",
                      "0x0", str(files["bootloader.bin"]), "0x8000", str(files["partitions.bin"])]
    arguments += ["0x10000", str(files["firmware.bin"])]
    esptool(port, arguments)
    # Only the firmware boot selector is reset, after esptool verifies app0.
    # This prevents an old app1 OTA selection from booting the previous image.
    # NVS settings (0x9000) and SD files are never erased by the flasher.
    esptool(port, ["erase-region", "0xe000", "0x2000"], after="hard-reset")
    print("\nFirmware verified. Pika was restarted. Settings and microSD were kept.")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Skip the interactive port question")
    parser.add_argument("--stable", action="store_true", help="Exclude preview releases (previews are included by default)")
    parser.add_argument("--install", action="store_true", help="First installation on a NEW/ERASED supported board; also write bootloader/partitions")
    parser.add_argument("--download-only", type=Path, metavar="DIRECTORY", help="Download and verify without touching a device")
    args = parser.parse_args(argv)
    print("Pika Note — USB firmware updater\nBack up before updating. Safely eject USB SD Backup first.", flush=True)
    if sys.version_info < (3, 10):
        raise FlashError("Python 3.10 or newer is required. Install Python from python.org, then retry.")
    if not args.download_only:
        bootstrap()
    release = choose_release(json.loads(fetch(API, 2 * 1024 * 1024)), args.stable)
    print(f"Latest compatible release: {release['tag_name']}\n{release['html_url']}", flush=True)
    if args.download_only:
        unpack_verified(release, args.download_only.resolve())
        print(f"Downloaded to {args.download_only.resolve()}. No device was accessed.")
        return 0
    with tempfile.TemporaryDirectory(prefix="pika-flash-") as temporary:
        workspace = Path(temporary)
        files = unpack_verified(release, workspace)
        original = select_port(args.port)
        boot_port = enter_bootloader(original)
        flash(boot_port, files, workspace, args.install)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nCancelled. If flashing had started, keep Pika connected and rerun the updater.", file=sys.stderr)
        raise SystemExit(130)
    except (FlashError, OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"\nERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
