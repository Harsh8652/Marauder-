// Board-config self-check for MARAUDER_ESP32_OLED. It compiles the REAL configs.h with the
// host stubs and asserts the things that are easy to get wrong and expensive to debug on the
// bench: pin collisions, pins that a classic ESP32 cannot use, the feature flags the display
// and log code depend on, menu geometry vs. the 128x64 panel, and the partition table in
// partitions.csv (which decides whether the log fits next to the settings).
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "configs.h"
#include "LogStores.h"

#ifndef PARTITIONS_CSV
#define PARTITIONS_CSV "partitions.csv"
#endif

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, what)                                          \
  do {                                                             \
    g_checks++;                                                    \
    if (!(cond)) {                                                 \
      g_failures++;                                                \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, (what)); \
    }                                                              \
  } while (0)

#define CHECK_EQ(a, b, what)                                                             \
  do {                                                                                    \
    g_checks++;                                                                            \
    if ((long long)(a) != (long long)(b)) {                                                 \
      g_failures++;                                                                         \
      std::printf("FAIL %s:%d  %s (got %lld, want %lld)\n", __FILE__, __LINE__, (what),     \
                  (long long)(a), (long long)(b));                                         \
    }                                                                                       \
  } while (0)

// ---- the board must be the one the notes say we are building ---------------------
#if !defined(MARAUDER_ESP32_OLED)
#error "host_tests must compile configs.h with MARAUDER_ESP32_OLED selected"
#endif
#ifdef MARAUDER_OLED128X64
static_assert(true, "MARAUDER_OLED128X64 follows from MARAUDER_ESP32_OLED");
#else
#error "MARAUDER_OLED128X64 must be defined for MARAUDER_ESP32_OLED or no display code compiles"
#endif

static_assert(TFT_WIDTH == 128, "the panel is 128 columns wide");
static_assert(TFT_HEIGHT == 64, "the panel is 64 rows tall");
static_assert(TEXT_HEIGHT == 8, "one font row is 8 pixels");

// ---- feature flags the OLED port relies on ---------------------------------------
// These are #error rather than CHECK on purpose: a missing flag does not just fail a test,
// it silently leaves the display or button code out of the binary.
#ifndef HAS_SCREEN
#error "HAS_SCREEN missing: Display/MenuFunctions would not be built at all"
#endif
#ifndef HAS_BUTTONS
#error "HAS_BUTTONS missing: no button objects are created"
#endif
#ifndef HAS_MINI_SCREEN
#error "HAS_MINI_SCREEN missing: the small-screen menu layout is skipped"
#endif
#ifndef HAS_MINI_KB
#error "HAS_MINI_KB missing: the six-button text editor is not compiled"
#endif
#ifndef HAS_SD
#error "HAS_SD missing: no SD log and no sdDeleteMenu"
#endif
#ifndef USE_SD
#error "USE_SD missing: SD code is compiled out (SDInterface is empty)"
#endif
#ifndef HAS_BT
#error "HAS_BT missing: BLE attacks are compiled out"
#endif
#ifndef HAS_NIMBLE_2
#error "HAS_NIMBLE_2 missing: NimBLE 1.x API assumed"
#endif
#ifndef SCREEN_BUFFER
#error "SCREEN_BUFFER missing: the OLED cannot scroll, menus need the framebuffer path"
#endif
// C5-only or hardware we do not have. If one of these appears, the C5 code paths
// (RMT WS2812, dual band, PSRAM buffers) get compiled for a classic ESP32 and break.
#ifdef HAS_NEOPIXEL_LED
#error "HAS_NEOPIXEL_LED must stay off: this board has no LED, and PoomWs2812 needs the C5 RMT"
#endif
#ifdef HAS_C5_SD
#error "HAS_C5_SD must stay off: it routes SD through a C5-only shared SPI bus"
#endif
#ifdef HAS_DUAL_BAND
#error "HAS_DUAL_BAND must stay off: a classic ESP32 has no 5 GHz radio"
#endif
#ifdef HAS_PSRAM
#error "HAS_PSRAM must stay off: this module has no PSRAM"
#endif
#ifdef HAS_BATTERY
#error "HAS_BATTERY must stay off: there is no battery divider on this board"
#endif
#ifdef HAS_GPS
#error "HAS_GPS must stay off unless a GPS module is actually wired"
#endif
#ifdef HAS_CYD_TOUCH
#error "HAS_CYD_TOUCH must stay off: no touch controller on this board"
#endif
#ifdef HAS_CAP_TOUCH
#error "HAS_CAP_TOUCH must stay off: no touch controller on this board"
#endif

// ---- pins ------------------------------------------------------------------------
struct PinUse {
  const char* what;
  int pin;
};

// A classic ESP32 (WROOM-32) cannot use these:
//   6-11 internal SPI flash, 0/2/12/15 strapping (0 and 2 matter for a button held at boot),
//   1/3 UART0 (serial console), 34-39 input-only with no internal pull-up (buttons need one).
static bool pinUsableForInput(int pin) {
  if (pin < 0 || pin > 39) return false;
  if (pin >= 34 && pin <= 39) return false;   // no pull-up, so a button to GND would float
  if (pin >= 6 && pin <= 11) return false;    // wired to the flash chip
  if (pin == 0 || pin == 2) return false;     // strapping pins: a held button blocks boot
  return true;
}
static bool pinUsableForOutput(int pin) {
  if (pin < 0 || pin > 33) return false;
  if (pin >= 6 && pin <= 11) return false;
  if (pin == 1 || pin == 3) return false;     // keep the serial console
  return true;
}

static void testPins() {
  const PinUse uses[] = {
      {"OLED SDA", OLED_SDA},   {"OLED SCL", OLED_SCL},
      {"BTN UP", U_BTN},        {"BTN DOWN", D_BTN},
      {"BTN LEFT", L_BTN},       {"BTN RIGHT", R_BTN},
      {"BTN OK", C_BTN},         {"BTN B", B_BTN},
      {"SD CS", SD_CS},          {"SD SCK", 18},
      {"SD MISO", 19},           {"SD MOSI", 23},
  };
  const int n = static_cast<int>(sizeof(uses) / sizeof(uses[0]));
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j)
      CHECK(uses[i].pin != uses[j].pin,
            (std::string("pin shared by ") + uses[i].what + " and " + uses[j].what).c_str());
  }
  CHECK_EQ(OLED_SDA, 21, "OLED SDA on 21");
  CHECK_EQ(OLED_SCL, 22, "OLED SCL on 22");
  CHECK(pinUsableForOutput(OLED_SDA) && pinUsableForOutput(OLED_SCL), "OLED pins are usable as I2C");
  const int buttons[] = {U_BTN, D_BTN, L_BTN, R_BTN, C_BTN, B_BTN};
  for (int b : buttons)
    CHECK(pinUsableForInput(b), (std::string("button on GPIO ") + std::to_string(b) +
                                " is usable with an internal pull-up").c_str());
  CHECK(pinUsableForOutput(SD_CS), "SD CS is usable as an output");
  // VSPI defaults on a classic ESP32: SCK 18, MISO 19, MOSI 23. SDInterface.cpp calls
  // SD.begin(SD_CS) for this board, i.e. it relies on exactly those defaults.
  CHECK_EQ(SD_CS, 5, "SD CS on 5");
  // 16/17 are the UART2 RTS/CTS pins: fine as buttons, but note it in the docs.
  std::printf("ok config: 12 pins checked, no collisions, no flash/strapping/UART0 pins\n");
}

static void testMenuGeometry() {
  // Five menu rows plus the status bar must fit in 64 rows, and one menu line must fit 21
  // characters of the 6-pixel font.
  CHECK_EQ(TEXT_HEIGHT, 8, "TEXT_HEIGHT is one 8-pixel font row");
  CHECK(strcmp(HARDWARE_NAME, "ESP32 OLED") == 0, "HARDWARE_NAME identifies this board");
  CHECK(STATUS_BAR_WIDTH + BUTTON_SCREEN_LIMIT * TEXT_HEIGHT <= TFT_HEIGHT,
        "status bar + menu rows fit the panel height");
  CHECK(STANDARD_FONT_CHAR_LIMIT * CHAR_WIDTH <= TFT_WIDTH, "a text line fits the panel width");
  CHECK_EQ(STANDARD_FONT_CHAR_LIMIT, 21, "21 characters per line on a 128 px panel");
  CHECK(CHAN_PER_PAGE * TEXT_HEIGHT <= TFT_HEIGHT - STATUS_BAR_WIDTH, "the channel list fits");
  CHECK(GRAPH_VERT_LIM > 0 && GRAPH_VERT_LIM < TFT_HEIGHT, "the graph height is sane");
  CHECK_EQ(BUTTON_ARRAY_LEN, BUTTON_SCREEN_LIMIT, "the button array is sized for the visible rows");
  std::printf("ok config: %d menu rows + %d px status bar fit %d rows, %d chars per line\n",
              (int)BUTTON_SCREEN_LIMIT, (int)STATUS_BAR_WIDTH, (int)TFT_HEIGHT,
              (int)STANDARD_FONT_CHAR_LIMIT);
}

// ---- partitions.csv ---------------------------------------------------------------
struct Part {
  std::string name, type, subtype;
  unsigned long long offset = 0, size = 0;
};

static std::vector<Part> readPartitions(const std::string& path, bool* ok) {
  std::vector<Part> out;
  std::ifstream in(path);
  *ok = static_cast<bool>(in);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::stringstream ss(line);
    Part p;
    std::string field;
    std::vector<std::string> fields;
    while (std::getline(ss, field, ',')) fields.push_back(field);
    if (fields.size() < 5) continue;
    auto trim = [](std::string& s) {
      while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
      while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
    };
    for (auto& f : fields) trim(f);
    p.name = fields[0];
    p.type = fields[1];
    p.subtype = fields[2];
    p.offset = std::strtoull(fields[3].c_str(), nullptr, 0);
    p.size = std::strtoull(fields[4].c_str(), nullptr, 0);
    out.push_back(p);
  }
  return out;
}

static void testPartitions() {
  bool ok = false;
  const std::vector<Part> parts = readPartitions(PARTITIONS_CSV, &ok);
  if (!ok) {
    std::printf("SKIP partitions.csv not found (run from the firmware/ directory)\n");
    return;
  }
  CHECK(parts.size() >= 3, "the table has nvs, app and spiffs");
  const unsigned long long flash = 4ull * 1024 * 1024;
  const Part* app = nullptr;
  const Part* spiffs = nullptr;
  for (size_t i = 0; i < parts.size(); ++i) {
    const Part& p = parts[i];
    CHECK(p.offset % 4096 == 0 || p.type == "data", "partitions are 4 KB aligned");
    CHECK(p.size % 4096 == 0 || p.type == "data", "partition sizes are multiples of 4 KB");
    CHECK(p.offset + p.size <= flash, (p.name + ": does not fit in 4 MB flash").c_str());
    if (i) CHECK(p.offset >= parts[i - 1].offset + parts[i - 1].size,
                 (p.name + ": overlaps the previous partition").c_str());
    if (p.type == "app") app = &p;
    if (p.subtype == "spiffs") spiffs = &p;
  }
  CHECK(app != nullptr, "there is an app partition");
  CHECK(spiffs != nullptr, "there is a spiffs partition");
  if (app && spiffs) {
    // The app partition must be big enough for this build (upstream C5 builds are ~2.5 MB).
    CHECK(app->size >= 2800u * 1024u, "app partition is at least 2.8 MB");
    // SPIFFS holds the settings JSON (6 KB) plus the rotating log, and needs free blocks for
    // wear levelling: a log that fills the partition is a log that stops working.
    CHECK(spiffs->size >= logInternalCapBytes() + 64u * 1024u,
          "SPIFFS holds the 256 KB log plus 64 KB of headroom");
    std::printf("ok config: app %llu KB, spiffs %llu KB (log %lu KB + headroom)\n",
                app->size / 1024, spiffs->size / 1024,
                (unsigned long)(logInternalCapBytes() / 1024));
  }
  if (app) CHECK(app->offset >= 0x10000, "the app starts after the bootloader/Partition Table");
}

int main() {
  testPins();
  testMenuGeometry();
  testPartitions();
  if (g_failures) {
    std::printf("%d of %d checks FAILED\n", g_failures, g_checks);
    return 1;
  }
  std::printf("ok config: all %d checks passed\n", g_checks);
  std::printf("ALL PASSED\n");
  return 0;
}
