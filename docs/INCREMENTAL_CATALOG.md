# Incremental device catalog (firmware 8.0.4)

Canonical notes, WAVs, manager.json and tasks.state remain the source of truth. The root .pika-phone-sync.json is a disposable, checksummed schema-2 index. Phone protocol 4 remains compatible with Companion 8.0.5.

## Ordinary changes

- Atomic file commits register their canonical path before replacement. WAV creation, direct removal, restore and Undo have equivalent hooks.
- Only affected memories are read. Raw memory-derived task sources are stored in the catalog; completion and manager overlays are applied to those sources in RAM, without opening every memory again.
- Untouched record versions and file entries are retained. Deletions remain versioned tombstones. Day counts and the active-memory count are calculated from indexed records in RAM.
- Live snapshot pushes reuse the committed index instead of paginating the full manager renderer repeatedly. Home can reuse the committed memory count.
- Private Undo files and incomplete temporary files do not enter the synchronization inventory.

## Commit and recovery

1. Flush a root .pika-sync-dirty intent marker before changing canonical data. If this fails, the guarded operation does not replace its source file.
2. Build a candidate catalog separately; retain the published generation on read/allocation/write failure or input preemption.
3. Hash the candidate with streaming SHA-256 and atomically replace it through the existing temporary/backup rename protocol.
4. Publish the RAM catalog and task cache only after a complete save. Clear the intent marker last.

At boot, validate schema, checksum, compact index.csv hash and membership count. A valid clean index is reused across software restart and deep sleep. A missing/corrupt/legacy index, changed membership or an outstanding intent triggers conservative source reconciliation. Unknown changes after a crash deliberately use a full recovery scan rather than guessing a partial patch. Restore and archive cleanup retain their normal source journals.

This provides application-level crash recovery; it does not make FAT or a physically failing SD card power-loss-proof. Derived checksums are not a backup of canonical recordings.

## Validation

Production C++ fault-injection coverage: clean restart without scans, one-note patches, RAM task overlays, unrelated version preservation, failed source reads, failed overlay reads, failed catalog writes/renames, reset with a pending intent, reset after catalog replacement before intent removal, corrupted catalog checksum, deletion tombstones, and an intent-write failure blocking a source replacement. Manager CRUD, task caching, inventory and Undo recovery tests also pass.

Device measurements on the existing 27-memory / 33-task / 3-event library:

- One-note reindex: 2.49–2.95 seconds, one memory and one file touched, no directory scan.
- Metadata-only reconciliation: 0.74–0.79 seconds, no memory/brain reads or directory scan (small overlays and index.csv are still read).
- Previous full snapshot/scan: approximately 34 seconds.
- First schema migration: 41 seconds, once. Ready Home after the tested software restart: 1.65 seconds.

The one-note benchmark marks an existing transcript as touched without changing its content. These figures measure catalog work, not speech recognition, AI latency or WAV transfer. Phone record IDs, text, dates and statuses were compared before/after migration with no differences.

USB diagnostics: CATALOG_STATUS and CATALOG_BENCH. The latter refreshes derived state and may advance its generation, but does not change user record contents.
