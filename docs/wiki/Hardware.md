# Hardware & printable case

The target is the Waveshare ESP32-S3-ePaper-1.54 family with the monochrome panel driver used in this firmware, 8 MB flash/PSRAM, microSD, microphone, speaker, two buttons and battery. This is not a universal image for all Waveshare panels.

## Case files
The current STL2 designs are published under `case/stl/`:

| File | Part |
| --- | --- |
| pika_case.stl | Main case |
| pika_case_vib.stl | Vibration variant |
| pika_cover.stl | Cover |
| pika_buttons.stl | Buttons |

Check fit, battery clearance and your board revision before assembly. CAD editor backups and SKP files are intentionally omitted.

## Optional vibration
Firmware supports ten GPIO2 patterns with per-scenario choices. Drive a vibration motor through a suitable transistor and flyback diode; never directly from a GPIO. Match motor voltage/current and transistor base/gate drive to your actual parts. Vibration is optional and disabled/configurable in Companion.

## Controls
Use Config to assign combinations. Hold both buttons to request reconnection; a five-second recovery hold requests restart. USB Mount exposes the SD for backup; safely eject the host drive before leaving. Lower button returns from USB Backup.
