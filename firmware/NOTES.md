# Marauder OLED port — working notes

## Source
- Upstream: https://github.com/justcallmekoko/ESP32Marauder (MIT License, Copyright 2020 Just Call Me Koko)
- Snapshot: commit `d3ceb28afeda1196ca27c13adeaa69ada52383f3` (main, MARAUDER_VERSION v1.18.0)
- Copied to `firmware/upstream/` (source only; `PreviousVersions/` with ~106 MB of old binaries was left out).
- MIT allows personal modification. If you ever share a build, keep the license and copyright notice.

## How the firmware is organised
- One source tree, many boards. The board is picked by a single `#define` at the top of
  `esp32_marauder/configs.h` (e.g. `GENERIC_ESP32`, `MARAUDER_POOM`, `MARAUDER_V8`).
- Each board block sets capabilities: `HAS_SCREEN`, `HAS_BUTTONS`, `HAS_SD`, `HAS_NEOPIXEL_LED`, `HAS_BT`, etc.
- Pins are defined per board in `configs.h` (buttons ~line 999, I2C ~3000, SD ~2725/3276, LED ~2864).
- Display: `Display.h` picks `PoomDisplay.h` (custom SH1106/128x64 I2C driver) for POOM, otherwise `TFT_eSPI`.
  - `PoomDisplay.cpp` is the OLED driver we want to reuse: I2C at 0x3C, `Wire.begin(0,1,100000)`, SH1106-style init, column offset 2, 128x64 framebuffer.
  - `PoomFont.h` supplies the font.
- Menus: `MenuFunctions.cpp` (has many `#ifdef MARAUDER_POOM` branches for the small screen and six-button editor).
- LEDs: `LedInterface.*` + `PoomWs2812.*` (POOM uses the C5 RMT backend; classic ESP32 would need Adafruit_NeoPixel or no LEDs).
- Tests: `tools/test_poom_target.py` checks the POOM pin map.

## Important: POOM pins do NOT fit a classic ESP32
The POOM pin map is for the ESP32-C5. Several of these pins are unsafe on a classic ESP32:

| POOM function | POOM GPIO | Classic ESP32 problem |
|---|---|---|
| OLED SDA | 0 | Boot-strapping pin (boot mode) |
| OLED SCL | 1 | UART0 TX (serial console) |
| Left button | 3 | UART0 RX |
| Up button | 7 | Connected to internal SPI flash — cannot use |
| B button | 9 | Connected to internal SPI flash — cannot use |
| SD MOSI/MISO/SCK | 4 / 8 / 6 | 6 and 8 are flash pins; 4 is fine |
| SD CS | 5 | Usable |
| WS2812 data | 27 | Usable |

Classic ESP32 pins to avoid or be careful with: 0, 2, 12, 15 (strapping); 1, 3 (UART0); 6–11 (flash);
34–39 (input-only, no internal pull-ups — don't use for buttons unless external pull-ups are added).

Common, safe choices for a classic ESP32 OLED build (to verify on your board's pinout):
- OLED I2C: SDA=21, SCL=22 (the usual ESP32 I2C pins)
- Buttons: 13, 14, 16, 17, 18, 19, 23, 25, 26, 27 (check your PCB; avoid 34–39 unless pulled up)
- SD (if used): SPI on VSPI 18/19/23 + CS 5 (or no SD at first)

## Plan
1. Confirm the exact classic ESP32 board (DevKit V1 / WROOM-32 / 4MB or 16MB flash) and wire the OLED + buttons.
2. Build upstream `GENERIC_ESP32` as a baseline, flashed headless, to prove the toolchain works.
3. Add a new board block `MARAUDER_ESP32_OLED` in `configs.h`:
   - `HAS_SCREEN`, `HAS_BUTTONS`, `HAS_BT`, `HAS_MINI_SCREEN` (or the POOM-style small screen path)
   - OLED pins (I2C 21/22), button pins on safe GPIOs, and no LED or SD unless needed
4. In `Display.h`, route this board to `PoomDisplay` (the existing SH1106 driver) and make sure `PoomDisplay.cpp` uses the new I2C pins.
5. Fix every `#ifdef MARAUDER_POOM` path that assumes C5 hardware (RMT LED, C5-only features) so they compile for the new board.
6. Build, flash, and test: boot splash, menu navigation with buttons, one Wi-Fi scan.
7. If the POOM screen code fights the classic ESP32 too much, write a standalone OLED frontend that talks to the headless serial CLI instead.

## Open questions for you
- Which exact classic ESP32 board do you have (chip revision, flash size, and any PCB silk labels)?
- How many buttons, and do you want 6 (like POOM: Up/Down/Left/Right/Center/B) or fewer?
- Do you want SD storage on this build, or is serial output enough at first?

---

## Decisions (round 2)

### Board (confirmed)
- Robocraze "ESP32 38-Pin WiFi + Bluetooth NodeMCU Development Board" (SKU TIFCC0111)
- ESP-WROOM-32 module, classic ESP32 (dual-core LX6), ~4 MB flash (module dependent), USB-UART
- Product page says: hold the IO0 (BOOT) button 1–2 s when uploading. Plan for manual boot mode.
- Build target: `GENERIC_ESP32`-based new board block (not POOM, not C5).

### Pin map (proposed; verify against your board's silkscreen before wiring)
| Function | GPIO | Notes |
|---|---|---|
| OLED SDA | 21 | I2C, SSD1306/SH1106 128x64, addr 0x3C |
| OLED SCL | 22 | I2C |
| BTN UP | 13 | input, internal pull-up, active low |
| BTN DOWN | 14 | input, internal pull-up |
| BTN LEFT (back) | 16 | input, internal pull-up |
| BTN RIGHT | 17 | input, internal pull-up |
| BTN OK | 25 | input, internal pull-up |
| BTN B (cancel/edit) | 26 | input, internal pull-up |
| SD CS | 5 | VSPI |
| SD SCK | 18 | VSPI |
| SD MISO | 19 | VSPI |
| SD MOSI | 23 | VSPI |

Avoided: 0 (BOOT), 1/3 (UART0), 2/12/15 (strapping), 6–11 (internal flash), 34–39 (input-only, no pull-ups).
Buttons: 6 chosen for full parity with POOM (text editor needs Up/Down/Left/Right/OK/B).
Only 2 extra GPIOs are needed beyond this if you later add a status LED.

### Logging policy
- Internal logs go to an internal flash partition (LittleFS/SPIFFS), always on, capped by size.
- SD card is optional. If present, logs are mirrored to SD as well.
- Both use `RotatingLog` (in `extras/rotating_log.h`): fixed-size segments, max N segments; when full, the oldest segment is deleted before the new one is written.
- Tested on host: `host_tests/test_core.cpp` (run: `g++ -std=c++17 host_tests/test_core.cpp && ./a.out`).

### UI plan (Flipper-style)
- Horizontal slide between pages, ease-out cubic, ~30 fps target (`extras/anim.h`).
- I2C at 400 kHz: a full 128x64 frame (1 KB) is ~25 ms, so 30 fps is only reachable with dirty-region updates. Target 20–25 fps for full-screen slides, 30 fps for small regions.
- Animations use fixed time steps, not frame counts, so the speed does not depend on the display rate.

### Status / limits of this build environment
- Sandbox can reach GitHub and PyPI, but NOT `dl.espressif.com`, the PlatformIO registry, or `downloads.arduino.cc`. ESP32 cross-compilers therefore cannot be downloaded here, so the firmware cannot be compiled in this sandbox.
- What was verified: logic in `extras/` on the host (g++). What still needs a local build: the full firmware in Arduino IDE or PlatformIO on your PC, then flash and test on the board.


---

## Implementation status (round 3)

Done in `firmware/upstream/esp32_marauder/` (classic ESP32, Arduino IDE):
- `configs.h`: new board `MARAUDER_ESP32_OLED` (enabled). New macro `MARAUDER_OLED128X64`
  covers both POOM and the new board, and all display/UI code now uses it. C5/LED-only code still
  uses `MARAUDER_POOM`. Pins: OLED I2C 21/22, buttons 13/14/17/16/25/26, SD CS 5.
- Display: `PoomDisplay` now uses `OLED_SDA`/`OLED_SCL` and runs I2C at 400 kHz (was 100 kHz, which
  caps a full frame at ~90 ms).
- `RotatingLog.h`: log core with RAM use independent of cap size (only oldest/newest numbers kept).
- `LogStores.cpp/.h`: SPIFFS log (256 KB) + optional SD log. SD cap = `SD.totalBytes()` at boot.
  `logInit()` and `marauderLog()` are called from `setup()`.
- `partitions.csv`: custom layout, 3 MB app and 960 KB SPIFFS (no OTA).
- `OledAnim.h`, `OledSlide.h`: slide-transition math and frame composition (no hardware calls).
- `BUILD_ARDUINO_IDE.md`: exact IDE, library, and flash steps.

Tests on this PC (g++, all pass):
- `host_tests/test_logs.cpp`: rotation cap, no loss across segments, resume after reboot, SD cap math
  (16 GB -> 244,140 segments of 64 KB), internal 256 KB = 8 x 32 KB.
- `host_tests/test_slide.cpp`: slide endpoints, no cross-page bleed, monotonic timing.
- `host_tests/test_core.cpp`: rotation and easing.
- `LogStores.cpp` syntax-checked against stub Arduino headers (found and fixed a missing `<cstring>`).

NOT yet done / not verified:
- Full firmware compile: needs the Arduino ESP32 core, which this sandbox cannot download.
  Expect some compile fixes on first build; send me the errors.
- Slide animation is NOT wired into the menus yet. The math is ready. Next step is to hook it into
  the menu page change path in `MenuFunctions.cpp`, using `composeSlide` at the flush point.
- Slide duration: at 200 ms there are only ~7 frames at 30 fps (too choppy). Plan 300 ms (~10 frames).
- Button "OK" is wired to C_BTN (GPIO 25) and B to B_BTN (GPIO 26), using the upstream POOM editor logic.
- SD cap at 100% can make writes fail near full. Add a reserve constant if that happens.
