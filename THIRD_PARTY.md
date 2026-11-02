# Third-party components

Original copyright and SPDX notices in the source files are retained.

- `pika_note/src/esp_codec_dev`: Espressif codec support. Files marked
  `SPDX-License-Identifier: Apache-2.0` remain under Apache License 2.0;
  the license text is included in `licenses/Apache-2.0.txt`. Local adaptations
  integrate codec lifecycle, power control and streaming with Pika Note.
- Board/display/audio support targets Waveshare ESP32-S3-ePaper-1.54.
  Preserve original notices in driver files. No blanket relicensing of
  third-party components is implied.
- PlatformIO downloads PioArduino/Arduino-ESP32, Adafruit GFX 1.12.1 and
  ArduinoJson 7.4.2 during build. They are not bundled in this source archive;
  their upstream licenses and notices apply to those dependencies.



## Supplemental Cyrillic glyphs (custom build)
GNU FreeFont 20120503, FreeSans and FreeSans Bold, from
https://ftp.gnu.org/gnu/freefont/freefont-ttf-20120503.zip .
Generated glyph tables use the GNU GPL v3 with the GNU FreeFont font embedding
exception; see licenses/GNU-FreeFont-COPYING.txt and tools/font-source/README.
Original ASCII Adafruit tables are unchanged. Regenerate the supplemental tables
with tools/generate_cyrillic.py (Pillow required).

The Android Today overlay bundles the same FreeSans.ttf and FreeSansBold.ttf files as web fonts, with the same GNU FreeFont embedding exception and notice.
