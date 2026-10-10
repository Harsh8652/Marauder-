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

Round 4 changes:
- SD log cap now keeps a 5% reserve (`kSdReservePercent` in `LogStores.cpp`).
- Slide animation is wired into the menus:
  - `PoomDisplay::beginSlide(dir)` snapshots the page on screen. `display()` composes frames from the
    old page and the new page for `kSlideDurationMs` (300 ms), at ~30 fps, without blocking.
  - `MenuFunctions::changeMenu()` calls it on real page changes: forward for a new menu, back when the
    target is the parent menu. Same-menu refreshes do not animate.
  - On the OLED, `changeMenu()` no longer calls `display_obj.init()`, because that clears the screen.
- `PoomDisplay.cpp` now includes `configs.h`. Without it, Arduino IDE builds would miss `OLED_SDA/SCL`
  and the whole file (upstream CI avoids this by passing the board as a `-D` flag).

NOT yet done / not verified at the end of round 4 (status: see round 5 at the end of this file):
- Full firmware compile: needs the Arduino ESP32 core, which this sandbox cannot download.
  `PoomDisplay.cpp` was syntax-checked against stubs. `MenuFunctions.cpp` was not checked.
  -> round 5: PoomDisplay.cpp and LogStores.cpp are now compiled *and run* on the PC against a
     fake I2C bus/panel/filesystem; MenuFunctions.cpp is covered by the static source checker.
- The slide is not yet visually tested on the hardware. Watch for flicker and for frame rate.
  -> round 5: `sh firmware/tools/render_preview.sh` renders the slide with the real driver;
     frame rate is measured (20 fps paced, 28 ms per full frame). Flicker still needs a panel.
- Button "OK" is wired to C_BTN (GPIO 25) and B to B_BTN (GPIO 26), using the upstream POOM editor logic.
  -> round 5: checked (MenuFunctions.h declares `extern Switches b_btn` under MARAUDER_OLED128X64,
     the .ino defines it, the editor uses it) plus pin-collision checks in test_board_config.cpp.
- Slide RAM cost: two 1 KB buffers (2 KB total) in PoomDisplay.
  -> round 5: 2 KB still, but for a different reason - the composed frame is now built one page
     at a time in a 128-byte scratch, so the third 1 KB buffer is gone (was 3 KB).
---

## Round 5 — the verification round (what "complete" meant here)

Round 4 left four items open. Three of them were "we could not check this", so this round
built the machinery to check them, and the checking found four real bugs.

`firmware/host_tests/run_tests.sh` runs everything below. It needs only g++ and python3 —
no ESP32 toolchain, no hardware.

### New: the firmware sources are now compiled and *executed* on the PC

`host_tests/stubs/` is a small Arduino stand-in: `String`, `Print`, `PROGMEM`, GPIO, plus

- `Wire.h` — a fake I2C bus that charges every transferred byte (8 data + 1 ACK bit) to a
  shared **virtual clock** at the frequency the driver asked for. So "how long does a frame
  take" is a measurement, not an estimate.
- `PanelSim.h` — a software SSD1306/SH1106: it decodes commands (page set, column set) and
  pixel writes into GDDRAM, so a test can assert what the panel would *show*.
- `FS.h` / `SPIFFS.h` / `SD.h` — an in-memory volume per medium, with a capacity, a
  "no card" state, a `fail_writes` switch and a switch for the `File::name()` difference
  between ESP32 core 2.x (bare name) and 3.x (full path).

That means `PoomDisplay.cpp` and `LogStores.cpp` are now built with `-Wall -Wextra` from the
same files the ESP32 build compiles, and run against simulated hardware.

| test | what it pins down |
|---|---|
| `test_display.cpp` (45 checks) | I2C opens on 21/22 at 400 kHz; panel off→init→on; a full frame is 1024 B in 64 chunks and costs **28 ms** of bus time; dirty-page flush sends 1 of 8 pages; a no-op redraw sends nothing; 30/20 fps pacing; forward slide moves the old page left while the new one enters from the right, back slide the other way; a re-triggered slide still settles; a NACKing/absent panel cannot hang boot or the loop |
| `test_logstores.cpp` (49 checks) | boot record lands in `/logs/00000000.log`; rotation holds 256 KB in 8 × 32 KB; records that straddle a segment boundary are neither lost nor reordered; resume after a reboot appends instead of truncating; stray files in the log dir are ignored; works with bare names; SD mirroring; 5 % card reserve respected; a card smaller than one segment declines the SD log instead of half-adopting it; write failures are absorbed |
| `test_board_config.cpp` (106 checks) | `configs.h` alone resolves to `MARAUDER_ESP32_OLED` + `MARAUDER_OLED128X64`; no pin is shared; no pin sits on flash (6–11), UART0 (1/3), a strapping pin or an input-only pin (34–39) used as a button; the C5-only feature flags stay off; 5 menu rows + 8 px status bar + 21 chars/line fit 128×64; `partitions.csv` sums into 4 MB without overlaps and leaves SPIFFS headroom over the log |
| `tools/check_sources.py` | every `#if`/`#ifdef` in all 102 source files is balanced and every board macro it mentions actually exists (a typo silently deletes code instead of failing the build); every `tft.*()` call that is **live for this board** exists on `PoomDisplay` (TFT_eSPI has ~200 methods, the OLED driver implements the ~45 needed); every file that uses a symbol from `OledAnim/OledSlide/RotatingLog/LogStores/PoomDisplay` can see that header; exactly one board target is enabled |

`tools/check_sources.py` is the piece that covers `MenuFunctions.cpp` and the other files no
compiler here can reach (they need WiFi/NimBLE/AsyncWebServer headers). It is not a compiler
and does not pretend to be — but it does catch the two mistakes that actually happened below.

### Bugs found and fixed

1. **`RotatingLog` doubled the first segment.** `rotate()` used one flag (`started_`) both for
   "a segment is open" and "we have rotated once", so the *first* rotation advanced nothing:
   segment 0 got 64 KB instead of 32 KB, and the log stayed one segment over its cap for the
   life of the card — `resume()` trims by segment *count*, so it was never noticed. Round 4's
   test missed it because by the time it checked sizes, segment 0 had rotated out. Fixed with
   an explicit `have_cur_` + `advance()`; now covered at two levels (a fresh-log unit test and
   the end-to-end 8 × 32 KB check).
2. **`MenuFunctions.cpp` used `slideDirection()` without including `OledSlide.h`.** Found by
   the checker, not by me being careful: round 4 had `beginSlide()` inline in `changeMenu()`
   with a hand-written condition; moving that rule into the header (so it can be unit tested)
   exposed the missing include. Arduino IDE compiles each `.cpp` separately, so this is
   "fatal error: slideDirection was not declared" on the bench, and nothing in this sandbox
   would have caught it.
3. **Boot log used `%lu` with `millis()`** (`uint32_t` is `unsigned int` on xtensa too) —
   `-Wformat` warning in the real build. Now cast explicitly.
4. **`logSdCapBytes()` reported a card cap even when no SD log was created** (card too small
   for one 64 KB segment), so the UI would promise a log that would never be written.
   The cap is now only published when the log exists.

### Behaviour changes worth knowing about

- **Partial flushes.** The driver keeps one dirty bit per 8-row page instead of a single
  `dirty_` flag. A menu cursor move or one text row is now 11 I2C transactions and ~3.5 ms
  instead of 88 transactions and 28 ms. Redrawing identical pixels queues nothing at all,
  which matters because the menus redraw every pass.
- **Slide frames are composed page by page into a 128-byte scratch** instead of a second 1 KB
  frame buffer, so the animation costs 2 KB of static RAM (framebuffer + snapshot), not 3 KB.
  That closes round 4's "Slide RAM cost" item.
- **Slide pacing is 20 fps** (`kSlideFrameIntervalMs = 50`) while a whole page slides, because
  a full frame costs 28 ms of blocking I2C: at 30 fps the flush ate ~80 % of the main loop,
  at 20 fps it is ~55 % (measured by `test_display.cpp`). Ordinary updates stay at 30 fps now
  that they are 8× cheaper.
- **Boot splash prints `HARDWARE_NAME`** ("ESP32 OLED") instead of the hard-coded "POOM" — the
  one screen that tells you which build is in the flash.
- **A second boot record** with the pins and the SD state, so a board that "does nothing" can be
  diagnosed from the log: `boot: v1.18.0 ESP32 OLED` then
  `boot: oled i2c 21/22 btn 13/14/17/16/25/26 sd log 16106127360 B`.
- A slide interrupted by another slide now snaps to its target and slides from there (the old
  code snapshotted the half-drawn composed frame). Key repeat through a menu chain looks sane.

### Still open — needs the bench

- **A full firmware build.** Not possible here (no `dl.espressif.com`). The board's own code is
  now compiled and executed; `MenuFunctions.cpp`, `WiFiScan.cpp` and friends are only
  structurally checked. Treat the first Arduino IDE compile as part of the test.
- Real frame rate, tearing/flicker, button feel, key-repeat timing, NimBLE behaviour, and
  whether the settings JSON still fits the 960 KB SPIFFS partition after a year of writes.
- **Cosmetic, upstream, not fixed:** on a 128×64 panel `displayCurrentMenu()` draws the menu
  title (banner) at y=8 and then paints the first menu row over it, so the title is never seen.
  POOM behaves the same way. Either live with 5 rows and no title (current) or take
  `BUTTON_SCREEN_LIMIT` down to 4 and start rows at y=18 to get the title back.
- `NOTES.md` round 2's pin proposal had LEFT/RIGHT the other way round (16/17). `configs.h` and
  `BUILD_ARDUINO_IDE.md` agree on **LEFT=17, RIGHT=16** and are the source of truth.
