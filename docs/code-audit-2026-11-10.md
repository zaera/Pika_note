# Reliability and performance audit — 10 November 2026

Scope: firmware and Android Companion source, including UI assets, storage, Ask, voice, BLE/LAN, sleep/wake, backup and USB Mount. Generated embedded assets and build dependencies were excluded from source inventory. Automated searches covered 188 source files / 21,423 lines; deeper manual review focused on ownership of sockets/tasks, thread and memory limits, durable queues, lifecycle and hot paths. This is a source audit plus targeted device tests, not proof that every path is fault-free.

## Changes delivered in this pass

| Area | Confirmed problem / behavior | Change |
| --- | --- | --- |
| Wi-Fi selection | An active Wi-Fi network was sufficient, allowing the phone on a different SSID to bridge through the router. | Exact, case-sensitive match of phone SSID and configured Pika SSID is required. Unknown SSID keeps BLE. Check before offer, at handshake, after network changes and periodically while connected. Firmware also validates the offered name and its actual association. |
| Android SSID access | Modern Android hides SSID without location permission. | Request coarse + precise location when Wi-Fi boost is enabled; missing permission fails closed to BLE. No GPS coordinates are requested or used by this policy. |
| LAN send timeout | A single three-second budget covered an entire multi-frame message, even while frames were progressing. | Apply the budget to each individual encrypted frame; stalled writes still close the connection. |
| LAN offer scheduling | Repeated network callbacks could queue redundant attempts behind a blocked listener. | Coalesce queued offers and retain at most one follow-up request. |
| ESP socket ownership | A reply task could stop the socket while its owning task was reading it. | Failed sending signals stop; the LAN worker owns final socket close. USB Mount joins that worker before Wi-Fi/SD teardown and cancels file hashing. |
| Android compatibility | Two `InputStream.readAllBytes()` calls require API 33 but minimum Android API is 26. `NoSuchMethodError` is not caught by `catch(Exception)`. | Replace with bounded buffered reading (8 MiB cap) for local HTML/SVG assets. |
| Bluetooth revocation | Async scan/GATT callbacks and teardown contained unguarded permission-sensitive calls; permission can change after initial checks. | Handle `SecurityException` in the affected callbacks, protect close/disconnect, and include adapter access in the scan error boundary. |

SSID equality is a name rule, not a radio-band measurement. If one router gives 2.4/5 GHz exactly the same name, this rule cannot distinguish them. A separate 2.4 GHz SSID remains the way to enforce that distinction. Password/key material stays inside the authenticated BLE offer; it is not logged by the new policy.

## Remaining findings, ranked

### 1. Interrupted phone recordings are not recovered — high priority

Sources: `PhoneRecorder.java:14–17`, `PhoneVoice.java:20`, `PhoneAsk.java:15`.

Recording writes PCM to `*.wav.recording` and finalizes its header and atomic rename only at normal completion. On a recording exception, a sufficiently long partial file is retained, but startup recovery scans only completed `*.wav` files. Process death or interruption before rename therefore leaves captured audio outside the processing queue. This is a confirmed code path; no user recording was intentionally interrupted in this audit.

Recommended change: recover validated partial files on startup, repair RIFF lengths from the actual even PCM byte count, atomically finalize and enqueue once by UUID. Preserve cancellation semantics and never automatically execute an interrupted Ask without deciding how consent/partial commands should work. Add process-kill and storage-full tests.

### 2. Unbounded request thread creation — medium priority

Source: `MainActivity.java`, `requests = Executors.newCachedThreadPool()` and `Bridge.request()`.

Slow RPC/network requests can coexist with UI polling and repeated actions; the pool has no thread cap. Each worker also carries a stack and retains request/activity objects. Under a persistent stall, this can increase memory use and contention. This is an unbounded-resource risk, not a reproduced OOM in the current test.

Recommended change: bounded executor and queue, explicit busy responses on admission failure, coalesce read-only polling, and keep voice/backup work in separate controlled lanes. Replacing it with a fixed pool alone would merely move the problem to an unbounded queue.

### 3. Ask polling grows with the complete history — medium priority

Sources: `phone.js` Ask `setInterval(draw,1200)`, `MainActivity` `/api/phone/ask`, `PhoneAsk.importFeed()` and `summary()`.

While Ask is open, each poll imports/scans the entire JSONL feed and serializes/sorts the complete dialogue. The UI compares a full JSON signature afterward, so unchanged history still costs disk/CPU/allocation and holds the Store monitor. As history grows, unrelated operations can wait on that monitor.

Recommended change: revision-based no-change response; incremental feed offset with reset detection; paginated message loading and a compact ID index. Keep the visible bottom-of-chat behavior.

### 4. Small SD reads and repeated Ask feed scans — medium priority

Sources: `brain.cpp:46` `readFilePrefix()`, `ask_assistant.inc:48` `feedHas()` and `pikaAskImportTurn()`.

Transcript prefix reading uses one file read and String append per byte. Each imported Ask turn scans the feed for deduplication, then scans again to verify success. Costs grow with record count/history; the latter must retain crash-safe deduplication.

Recommended change: bounded block reads preserving UTF-8 truncation and durable ID/revision indexing. Measure before/after on real SD; these costs alone do not explain external AI latency.

### 5. Internal RAM remains a scarce shared resource — medium priority

Sources: LAN framing/crypto, speech worker, JSON/model assembly and live record snapshots.

The earlier real-device failures showed the danger of allocating stacks/crypto buffers after Wi-Fi startup. The current worktree already reserves the speech worker early and chunks GCM work through small internal buffers. Future features still compete for internal contiguous memory even with free PSRAM.

Recommended change: log minimum heap and largest allocatable internal block by stage; prefer bounded buffers and PSRAM for large immutable payloads. Avoid copying cached task/event vectors in read-only paths. Do not replace copies with references where mutations invalidate cache ownership.

### 6. Renew and Ask startup need an atomic boundary — investigate

Sources: `MainActivity` Ask `renew` branch and `PhoneAsk.kick()`.

Renew checks the atomic `running` flag before taking the Store monitor; worker startup uses a separate compare-and-set. A queued worker can begin between the check and history reset. Whether a stale job can finish into renewed state needs deterministic reproduction.

Recommended change: one admission/generation boundary shared by Renew, enqueue and worker selection; reject commits from an obsolete generation. Treat this as a suspected race, not confirmed data loss.

### 7. Backup power-loss coverage is incomplete — validation gap

Phone state uses Android Keystore AES-GCM and AtomicFile. Restore uses staging, a recovery marker and atomic moves, and mutation receipts protect retries. These are useful safeguards, but successful normal restore does not establish behavior at every power-loss point; marker/state directory durability needs fault injection.

Recommended tests: kill after every restore transition, restart, compare checksums and settings; simulate full storage and truncated input. Preserve the original library until the replacement is committed. Do not run destructive production-library tests without a verified fresh backup.

### 8. Regression harness drift — medium priority

The broad host run executed 59 entry points: **42 passed; 17 did not reach a passing result**. Fifteen failed to compile their generated host harness (missing platform stubs/declarations such as `esp_timer_get_time`, `String`, `NetworkClient`, vibration/log helpers). One source-text assertion references a removed function; one test assumes a directory exists. These results cannot be counted as product runtime failures or passing coverage. `test_home_taps` is an alias of `test_home_worker`, so this is not 59 independent behaviors.

The Android fast-sync fixture also expected the old request count before sleep-lease acquisition was fixed. Its expectation now requires acquire → exchange → release rather than removing the required lease from production.

Recommended change: repair shared host adapters, reduce source-string extraction tests, and establish a clean CI baseline before further transport changes. Keep hardware tests for e-paper BUSY timing, sleep, microphone and audio continuity.

## Checks performed

- PlatformIO `pika_note_2`: firmware 8.0.17 built and flashed, flash hash verified.
- Android debug application and instrumentation APK assembled; application 8.0.15-preview installed.
- Pure Java SSID policy: exact match, mismatch, letter case, unknown/null, significant whitespace and quoted SSID cases pass. Firmware guard presence checked separately.
- Broad host run: 42/59 pass. Passing coverage includes LAN wire integrity, incremental catalog, USB backup, recovery hold, sync lease, delayed panel BUSY/previous-frame handling, recording release, speech buffering/cancellation, upload window, cache and metadata paths.
- Android lint found two API compatibility errors (fixed above). The subsequent run has 17 permission-analysis errors and 18 warnings; lint is **not clean**. Many permission calls are already under exception handling but this does not justify blanket suppression. Remaining warnings include custom-view allocation, static-context analysis, free-space checks and layout/API hygiene.
- Real BLE connection after flashing: `online=1 secure=1 mtu=517 lan=0`; firmware 8.0.17 reported 5 memories, 7 tasks, 1 future event and ~70.8 KiB free heap. This is one snapshot, not a memory-stability proof.
- Real-phone SSID test passed in foreground: phone `zaera5`, configured Pika network `zaera2`, `wifiEligibleByName=false`. A background-only first attempt saw Android redaction; the policy correctly kept BLE. The positive equal-name path is unit-tested, not newly measured over physical Wi-Fi in this pass.
- Android transport regression suite passed after updating its lease expectation: batching, retry/deduplication, upload window, live snapshots and speech flow checks.
- After instrumentation the app/service were restarted. USB reconfirmed secure BLE (`online=1`, `lan=0`). The temporary ten-minute test hold was explicitly released; ordinary configured sleep is restored.

## Recommended implementation order

1. Recording recovery and deterministic crash tests.
2. Bounded request admission plus Ask revision/feed pagination.
3. Buffered SD hot paths and persistent Ask ID index.
4. Restore fault injection, Renew generation boundary and heap-pressure soak tests.

Do not merge a large simultaneous rewrite of transport, storage and Ask planning: keep each change independently measurable and recoverable.

## Host test entry points requiring harness repair

- `test_activity_led.py`
- `test_ask_worker.py`
- `test_change_pin.py`
- `test_connection_lease.py`
- `test_data_restore.py`
- `test_deferred_http.py`
- `test_google_portal_async.py`
- `test_home_display.py`
- `test_home_taps.py`
- `test_home_worker.py`
- `test_mail_http.py`
- `test_offline_capture.py`
- `test_phone_foreground.py`
- `test_response_json.py`
- `test_sound_reminders.py`
- `test_unified_ask.py`
- `test_wifi_settings.py`
