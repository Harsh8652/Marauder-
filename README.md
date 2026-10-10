# Marauder

ESP32 Marauder (upstream `justcallmekoko/ESP32Marauder`, MIT) ported to a **classic ESP32
DevKit with a 128x64 I2C OLED and six buttons** - a small, cheap, pocket auditor with a screen,
instead of the C5/POOM or TFT boards upstream targets.

    firmware/
      upstream/            the ESP32 Marauder source tree + the port's changes
        esp32_marauder/    board target MARAUDER_ESP32_OLED in configs.h
        partitions.csv ...
      BUILD_ARDUINO_IDE.md what to install, how to wire it, how to flash it  <- start here
      host_tests/          run the OLED driver and the log store on a PC, no board needed
      tools/               source linter for this board + a preview renderer for the menu slide
      NOTES.md             the running log of decisions, findings and open questions

## If you just want to build it
1. `sh firmware/host_tests/run_tests.sh` - about ten seconds, needs g++ and python3 only.
2. `firmware/BUILD_ARDUINO_IDE.md` - Arduino IDE 2.x, esp32 core 3.3.4, five libraries,
   custom partition scheme, then flash over USB (hold BOOT on this board when it starts).
3. Wiring is in that file too: OLED on 21/22, buttons on 13/14/17/16/25/26, optional SD on
   5/18/19/23.

## What is ported
- `PoomDisplay`, upstream's SH1106/SSD1306 driver, reused for this board at 400 kHz I2C with
  per-page dirty flushing, and Flipper-style slide transitions between menus (300 ms, 20 fps).
- Six-button menu navigation and the small-screen text editor (Up/Down/Left/Right/OK/B).
- Rotating logs: 256 KB in SPIFFS always on, mirrored to the SD card at (card size - 5 %) in
  64 KB segments, oldest segment dropped first, resume after reboot.
- Deliberately off: LEDs, battery, GPS, PSRAM, dual-band, touch. The board has none of it.

## Status
The OLED driver and the log store are compiled and executed by the host tests, and every source
file in the tree is linted for this board. **The full firmware has never been compiled** - this
sandbox cannot reach the Espressif toolchain - and nothing has run on hardware yet. The first
real build and the first flash are still the two big unknowns; `firmware/NOTES.md` lists what is
verified, what is inferred and what is open, round by round.
