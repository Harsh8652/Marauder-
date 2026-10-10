# Build & flash the ESP32 OLED Marauder (Arduino IDE)

Sketch folder: `firmware/upstream/esp32_marauder/` (open `esp32_marauder.ino`).
Board target is already selected in `configs.h`: `#define MARAUDER_ESP32_OLED`.

## 0. Before you open the IDE
```sh
sh firmware/host_tests/run_tests.sh        # ~10 s, needs only g++ + python3
```
That compiles and runs the OLED driver and the log store against a fake I2C bus, a fake panel
and a fake filesystem, checks the pin map and `partitions.csv`, and lints every `#if` in the
tree for this board. It will not prove the whole sketch builds (that is step 3), but it does
catch the class of mistakes that waste a flash cycle: a `tft.<method>()` the OLED driver does not
have, a typo'd `#ifdef MARAUDER_...` that silently deletes your code, two board targets enabled
at once, a log cap that overshoots the partition.

Want to see the menu animation before wiring anything?
```sh
sh firmware/tools/render_preview.sh /tmp/oled-preview   # writes /tmp/oled-preview/slide.gif
```
It uses the real driver, so it shows the real pacing (~20 fps, ~7 frames per transition).

## 1. Arduino IDE
- Install Arduino IDE 2.x.
- Boards Manager: install **esp32 by Espressif Systems, version 3.3.4**
  (the same core upstream CI uses for classic ESP32 builds).

The Arduino IDE is the supported route for this board. `upstream/platformio.ini` only carries
upstream's `[env:native]` host tests - there is no PlatformIO environment for the OLED build,
because an unverifiable one is worse than none. If you want to add it, the settings that matter
are in steps 1 and 3 here, and `configs.h` already selects the board (no `-D` flag needed).

## 2. Libraries (Library Manager, or ZIP from GitHub)
Required:
- **NimBLE-Arduino 2.3.8** (h2zero/NimBLE-Arduino)
- **ESPAsyncWebServer** and **AsyncTCP** (ESP32Async forks, as in upstream CI)
- **LinkedList** (ivanseidel/LinkedList)
- **MicroNMEA** (stevemarple/MicroNMEA)
- **ESP32Ping** (marian-craciunescu/ESP32Ping)

Not needed for this board (leave them out): TFT_eSPI, XPT2046_Touchscreen, lv_arduino,
JPEGDecoder, Adafruit_NeoPixel. If the compiler reports a missing header from one of these,
install it and rebuild.

## 3. Tools menu (board settings)
| Setting | Value |
|---|---|
| Board | **ESP32 Dev Module** |
| Flash Size | **4MB (32Mb)** |
| Partition Scheme | **Custom** (uses `partitions.csv` in the sketch folder) |
| Upload Speed | 921600 (drop to 115200 if uploads fail) |
| CPU Frequency | 240MHz (WiFi/BT) |

Partition layout (`partitions.csv`): nvs 20 KB, otadata 8 KB, app 3 MB,
SPIFFS 960 KB (settings + the 256 KB internal log). This replaces the stock
"Minimal SPIFFS" scheme, which only gives ~190 KB of SPIFFS.
**Trade-off:** there are no OTA app slots, so updates must be done over USB.

## 4. Wiring
| Part | ESP32 GPIO |
|---|---|
| OLED SDA / SCL (SSD1306 or SH1106, 128x64, addr 0x3C) | 21 / 22 |
| Up / Down / Left / Right | 13 / 14 / 17 / 16 |
| OK (center) / B | 25 / 26 |
| SD CS / SCK / MISO / MOSI (optional) | 5 / 18 / 19 / 23 |

Buttons: one side to the GPIO, the other side to GND (internal pull-ups are enabled).
Power the OLED and SD card from 3.3 V. Do not connect GPIOs 0, 1, 3, 6–11, or 34–39 to these parts.

## 5. Upload
1. Connect the board over USB.
2. Click **Upload**.
3. When the IDE shows "Connecting...", **hold the BOOT (IO0) button** until the upload starts,
   then release it. (This matches the Robocraze product page.)
4. After flashing, press RESET once.

## 6. Logs
- Internal log: 8 × 32 KB segments in SPIFFS (256 KB). Always on.
- SD log (only if a card is present): cap = the card's volume size read at boot
  (`SD.totalBytes()`) minus a 5% reserve, split into 64 KB files under `/marauder_logs`.
  The reserve keeps free space so writes don't fail near full.
- When the cap is reached, the oldest segment is deleted before a new one is written.

## 7. Menu animation
Menus slide on page changes: forward navigation slides in from the right, back navigation from
the left (300 ms). The animation runs inside `display()`, which the main loop calls once per
pass, so it never blocks the loop on purpose.

Numbers measured by `host_tests/test_display.cpp` against a simulated 400 kHz bus:

| thing | cost |
|---|---|
| one full 128x64 frame | 1024 B = 88 transactions = **28 ms** |
| one 8-row page (a text line, the menu cursor) | 128 B = 11 transactions = **3.5 ms** |
| slide frames | paced at **20 fps** (~7 frames per transition) |
| bus share while sliding | ~55 % of the main loop |

The flush is blocking (no DMA), so a full-frame repaint at 30 fps would have left the Wi-Fi
capture callbacks only ~20 % of the loop; that is why slides are paced at 20 fps while
everything else is cheap enough to stay at 30. If you see tearing or a stutter on the panel,
change `kSlideFrameIntervalMs` / `kFrameIntervalMs` in `esp32_marauder/OledSlide.h`.

## 8. First boot checklist
1. Serial monitor at 115200: `ESP-IDF version is: ...`, then `Marauder v1.18.0` start-up lines.
   No display and no serial output at all is almost always a boot-mode or wiring problem, not code.
2. The splash should read `ESP32 Marauder` / `v1.18.0` / `ESP32 OLED`. The third line is
   `HARDWARE_NAME` - if it says `POOM`, you are looking at a different build than this tree.
3. Panel completely dark? Check SDA/SCL are on 21/22, that the module is a 0x3C part (some
   128x64 modules ship with a 0x3D link), and that it is powered from 3.3 V. The driver does not hang on a missing panel (there is a host test for that),
   so boot still continues and the log still fills up.
4. Buttons: Up/Down move the highlight, OK selects, Left goes back (and slides the other way),
   B is the editor cancel key. GPIO 16/17 are the UART2 RTS/CTS pins, which is fine as buttons.
5. Logs: press OK into a menu, then read `/logs/00000000.log` on SPIFFS (or `/marauder_logs/` on
   the card). Boot writes two records, one of which repeats the pin map and the SD state:
   `boot: oled i2c 21/22 btn 13/14/17/16/25/26 sd log 16106127360 B`. If it says `sd none`,
   the card was not seen; the internal log keeps working either way.
