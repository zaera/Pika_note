# Architecture

Pika records locally and communicates with Android over BLE. Android owns the primary library and performs online recognition, assistant requests, voice generation and service access. Device-side copies support offline capture and local screens.

Ordinary sync exchanges changed records/files, not a fresh copy of the library. Each side tracks local edits; full reconciliation is available separately. Changes update Today counts after durable application. Audio and foreground recording take priority over background file transfers.

The persistent checksum-protected catalog tracks memory-derived sources, records and files. A flushed write-intent marker precedes source changes; atomic replacement commits the catalog. Clean boots reuse it; interrupted changes or invalid catalogs trigger conservative recovery. See [incremental catalog design](https://github.com/zaera/Pika_note/blob/main/docs/INCREMENTAL_CATALOG.md).

Offline Memory always saves locally. Deferred transcription respects manual edits. Queued actionable Ask requests are processed in historical order; casual stale offline questions are not replayed as actions.

This tree has no original-firmware snapshot or historical version folders. It retains driver/library notices and compatibility code needed for existing data, migrations and restore. SD encryption is not implemented.
