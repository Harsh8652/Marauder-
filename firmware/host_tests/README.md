# Host tests — verify the OLED port without a board

    sh firmware/host_tests/run_tests.sh

Needs `g++` (7+) and `python3`. Nothing else: no PlatformIO, no Arduino core, no ESP32, no
network. Takes about 10 seconds. Exit code 0 means every check below passed.

These tests exist because the ESP32 toolchain cannot be downloaded in the sandbox where this
port was written, so "it compiles" could not be checked. Running the driver against a fake
panel is the next best thing, and it found four real bugs (see `firmware/NOTES.md`, round 5).

## What is here

| file | runs | checks |
|---|---|---|
| `test_core.cpp` | `RotatingLog.h`, `OledAnim.h` | log rotation and the easing curve, pure math |
| `test_logs.cpp` | `RotatingLog.h` | cap enforcement, split records, `resume()` trimming, the SD size/reserve math, and that a fresh log does not double its first segment |
| `test_slide.cpp` | `OledSlide.h`, `OledAnim.h` | slide endpoints, no bleed between pages, monotonic timing, and the "animate / skip / which way" rule menus use |
| `test_display.cpp` | **`../upstream/esp32_marauder/PoomDisplay.cpp`** | the real driver on a fake I2C bus: pins, 400 kHz, init order, frame cost in ms, per-page dirty flushes, pacing, both slide directions, key-repeat mid-slide, dead panel |
| `test_logstores.cpp` | **`../upstream/esp32_marauder/LogStores.cpp`** | the real log store on an in-memory SPIFFS + SD: boot record, rotation at 256 KB, reboot resume, stray files, card sizes, the 5 % reserve, failing writes |
| `test_board_config.cpp` | **`../upstream/esp32_marauder/configs.h`** + `partitions.csv` | one board selected, pins free of flash/strapping/UART0/input-only ranges and of each other, feature flags on/off as the port assumes, menu geometry vs. 128×64, partition table adds up |
| `../tools/check_sources.py` | every `.c/.cpp/.h/.ino` | balanced and non-typo'd `#if`s for this board, every live `tft.*()` call exists on `PoomDisplay`, headers visible where used |
| `../tools/render_preview.sh` | `PoomDisplay.cpp` again | not a test: renders the menu slide to PPM frames + `slide.gif` so you can look at the animation |

`test_display.cpp` and `test_logstores.cpp` compile the **firmware's own files**, not copies. If
you change `PoomDisplay.cpp`, these tests change behaviour with it.

## `stubs/` — the fake Arduino

Small stand-ins so the firmware sources compile with plain g++:

- `Arduino.h` — `String`, `PROGMEM`, `F()`, GPIO, and a **virtual clock**: `millis()` only moves
  when `delay()` runs or the fake bus consumes time. That is what makes "how long does a frame
  take" and "is the flush throttled" deterministic instead of wall-clock dependent.
- `Wire.h` + `stub_arduino.cpp` — records every transaction and charges each byte (8 data + 1
  ACK bit, plus start/stop) to the clock at the frequency the driver requested. Can NACK
  everything to simulate a panel that is not wired.
- `PanelSim.h` — a software SSD1306/SH1106: decodes page/column commands and pixel writes into
  GDDRAM, so tests assert on what the panel shows. Can also dump a frame as a scaled PPM.
- `FS.h`, `SPIFFS.h`, `SD.h` — an in-memory volume per medium with a capacity, an absent-card
  state, a `fail_writes` switch, and a switch for the `File::name()` difference between ESP32
  core 2.x (bare name) and 3.x (full path).

## What these tests cannot tell you

They do not compile `WiFiScan.cpp`, `MenuFunctions.cpp` and friends — those need the WiFi,
NimBLE, ESPAsyncWebServer and ArduinoJson headers, which need the ESP32 core. `configs.h` *is*
compiled here, and `tools/check_sources.py` reads the rest of the tree, but only a real Arduino
IDE or PlatformIO build proves the whole firmware links and fits in flash.

They also cannot tell you how the panel looks: contrast, tearing, whether 20 fps feels smooth,
whether a held button autorepeats the way you want. That is the bench's job.
