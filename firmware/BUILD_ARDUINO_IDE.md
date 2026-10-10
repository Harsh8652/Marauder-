# Build & flash the ESP32 OLED Marauder (Arduino IDE)

Sketch folder: `firmware/upstream/esp32_marauder/` (open `esp32_marauder.ino`).
Board target is already selected in `configs.h`: `#define MARAUDER_ESP32_OLED`.

## 1. Arduino IDE
- Install Arduino IDE 2.x.
- Boards Manager: install **esp32 by Espressif Systems, version 3.3.4**
  (the same core upstream CI uses for classic ESP32 builds).

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
- SD log (only if a card is present): cap = the card's full volume size, read at boot
  (`SD.totalBytes()`), split into 64 KB files under `/marauder_logs`.
- When the cap is reached, the oldest segment is deleted before a new one is written.
- A 100% cap means the card will be completely full of logs. FAT needs a little free space,
  so writes may fail near the limit. Tell me if you want a reserve percentage (it is one constant).
