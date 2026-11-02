# Build Pika Note

## Firmware
Install Python and PlatformIO Core. From this checkout run:

```sh
pio run -e pika_note_2
pio run -e pika_note_2 -t upload --upload-port YOUR_PORT
```

Use the port reported by your operating system; bootloader and runtime ports may differ. The pinned PioArduino platform and dependencies download on first build. The board requires 8 MB flash and OPI PSRAM. `platformio.ini` specifies the partition map and USB mode.

Build output is `.pio/build/pika_note_2/`. A new/erased board needs bootloader, partition table, boot application support and firmware, not just the application image. Prefer PlatformIO upload, which uses the correct offsets. Updating an already matching installation may use the application image at 0x10000. Do not erase flash to perform a normal update.

Credentials are provisioned after pairing. The build generates an empty local credential header; personal config and generated headers must never be committed.

## Android
Use JDK 17, Android SDK 35 and Gradle 8.9. Set ANDROID_HOME or an ignored `android-companion/local.properties` with sdk.dir.

```sh
gradle -p android-companion assembleDebug
```

The APK is `android-companion/app/build/outputs/apk/debug/app-debug.apk`. The preview download is debug signed; Android requires the same signing key for in-place updates. Back up before changing signing identities. No API credentials are bundled.

## Tests
Python checks live in `tools/tests/`; host C++ tests require a C++17 compiler and ArduinoJson headers. Browser checks require their stated Node/browser dependencies. Some older checks describe legacy compatibility paths; consult each test before running device operations. Never run hardware/destructive probes on your only library without a backup.
