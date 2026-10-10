// Runs the real OLED driver (upstream/esp32_marauder/PoomDisplay.cpp) against a fake I2C
// bus, an SSD1306/SH1106 panel emulator and a virtual clock. This is the closest thing to
// hardware available without hardware: it checks the I2C pins and speed, the init sequence,
// what actually lands in panel memory, how many bytes each flush costs, how long that takes
// on a 400 kHz bus, and the frame pacing of the menu slide.
#include <cstdio>
#include <cstring>
#include <vector>

#include "Arduino.h"
#include "Wire.h"
#include "PanelSim.h"
#include "configs.h"
#include "OledAnim.h"
#include "OledSlide.h"
#include "PoomDisplay.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, what)                                                       \
  do {                                                                          \
    g_checks++;                                                                 \
    if (!(cond)) {                                                              \
      g_failures++;                                                             \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, (what));              \
    }                                                                           \
  } while (0)

#define CHECK_EQ(a, b, what)                                                              \
  do {                                                                                    \
    g_checks++;                                                                            \
    const long long _x = (long long)(a), _y = (long long)(b);                              \
    if (_x != _y) {                                                                         \
      g_failures++;                                                                         \
      std::printf("FAIL %s:%d  %s (got %lld, want %lld)\n", __FILE__, __LINE__, (what),     \
                  _x, _y);                                                                  \
    }                                                                                       \
  } while (0)

// ------------------------------------------------------------------ panel emulator
// Decodes the bus the way the controller does: a transaction starting with 0x00 carries
// commands, 0x40 carries pixel data written at the current page/column with auto-increment.
// State is cumulative, exactly like a real panel's GDDRAM.
// The panel model lives in stubs/PanelSim.h so the preview renderer uses the same one.
using Panel = PanelSim;
static Panel g_panel;

// The firmware main loop: one display() call per pass, delay(1) at the end.
static void loopOnce(PoomDisplay& tft) {
  tft.display();
  delay(1);
}

static void settle(PoomDisplay& tft, uint32_t budget_ms = 5000) {
  for (uint32_t i = 0; i < budget_ms && (tft.sliding() || tft.dirtyPages() != 0); ++i) loopOnce(tft);
}

// ------------------------------------------------------------------ tests
static void testPinsAndInit() {
  stubResetArduino();
  g_panel = Panel();
  PoomDisplay tft;
  tft.init();
  g_panel.drain();

  CHECK_EQ(Wire.sdaPin(), OLED_SDA, "Wire.begin() is called with OLED_SDA from configs.h");
  CHECK_EQ(Wire.sclPin(), OLED_SCL, "Wire.begin() is called with OLED_SCL from configs.h");
  CHECK_EQ(Wire.frequency(), 400000u, "I2C is opened at 400 kHz (100 kHz caps a frame at ~90 ms)");
  CHECK_EQ(OLED_SDA, 21, "the board config puts OLED SDA on GPIO 21");
  CHECK_EQ(OLED_SCL, 22, "the board config puts OLED SCL on GPIO 22");
  CHECK(g_panel.display_off >= 1, "the panel is switched off before being configured");
  CHECK(g_panel.display_on >= 1, "the panel is switched on at the end of init");
  CHECK_EQ(g_panel.overflow, 0, "no pixel data is written past the last column");
  std::printf("ok display: I2C GPIO %d/%d at %lu kHz, panel inited and enabled\n", Wire.sdaPin(),
              Wire.sclPin(), (unsigned long)(Wire.frequency() / 1000));
}

static void testFullFrameCost() {
  stubResetArduino();
  g_panel = Panel();
  PoomDisplay tft;
  tft.init();
  g_panel.clearTraffic();
  stubWireResetBusTime();

  tft.fillScreen(TFT_WHITE);
  const uint32_t t0 = millis();
  tft.display(true);
  const uint32_t spent_ms = millis() - t0;
  g_panel.drain();

  uint32_t data_bytes = 0;
  for (const auto& tx : stub_i2c_log)
    if (tx.bytes.size() > 1 && tx.bytes[0] == 0x40) data_bytes += static_cast<uint32_t>(tx.bytes.size() - 1);
  CHECK_EQ(data_bytes, 1024u, "a full frame pushes 1024 pixel bytes");
  CHECK_EQ(g_panel.data_tx, 64, "a full frame is 8 pages x 8 chunks of 16 bytes");
  CHECK(spent_ms > 15u && spent_ms < 40u, "one full frame costs 15-40 ms of bus time at 400 kHz");

  uint8_t grab[1024];
  g_panel.grab(grab);
  bool all_white = true;
  for (int i = 0; i < 1024; ++i) all_white = all_white && grab[i] == 0xFF;
  CHECK(all_white, "fillScreen(WHITE) reaches every visible pixel");
  std::printf("ok display: full frame = 1024 B = %lu ms of bus time (ceiling ~%u fps)\n",
              (unsigned long)spent_ms, (unsigned)(1000u / (spent_ms ? spent_ms : 1)));
}

static void testPartialFlushAndNoops() {
  stubResetArduino();
  g_panel = Panel();
  PoomDisplay tft;
  tft.init();
  g_panel.clearTraffic();

  tft.display(true);
  CHECK_EQ(stub_i2c_log.size(), 0u, "display() with a clean framebuffer sends nothing");

  // One text row (y = 8..15) lives in page 1 only.
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(0, 8);
  tft.print("Hi");
  CHECK_EQ(tft.dirtyPages(), 1 << 1, "one text row dirties exactly one of the eight pages");
  const size_t tx_before = stub_i2c_log.size();
  tft.display(true);
  const size_t tx_used = stub_i2c_log.size() - tx_before;
  CHECK_EQ(tx_used, 11u, "one dirty page costs 11 transactions (3 command + 8 data)");

  // Repainting the same pixels must not queue work again - menus redraw constantly.
  tft.setCursor(0, 8);
  tft.print("Hi");
  CHECK_EQ(tft.dirtyPages(), 0, "redrawing identical text dirties nothing");
  tft.display(true);
  CHECK_EQ(stub_i2c_log.size(), tx_before + tx_used, "no new traffic for no new pixels");

  // Throttle between frames.
  tft.drawPixel(40, 40, TFT_WHITE);
  const size_t after_draw = stub_i2c_log.size();
  delay(kFrameIntervalMs);   // the previous flush set the pacing baseline
  tft.display();
  const size_t after_first = stub_i2c_log.size();
  CHECK(after_first > after_draw, "a dirty page is flushed on the next display()");
  tft.drawPixel(41, 40, TFT_WHITE);
  tft.display();
  CHECK_EQ(stub_i2c_log.size(), after_first, "the next flush waits for kFrameIntervalMs");
  delay(kFrameIntervalMs);
  tft.display();
  CHECK(stub_i2c_log.size() > after_first, "the flush happens once the interval passed");
  std::printf("ok display: per-page dirty tracking sends 1 of 8 pages and skips no-ops\n");
}

// The slide must move a marker on the old page across the screen while the new page's
// marker enters from the other side - checked by decoding the panel, not the source.
static void testSlideForwardGeometry() {
  stubResetArduino();
  g_panel = Panel();
  PoomDisplay tft;
  tft.init();

  // Page A: a lit column at x=100 (page 1). Page B: a lit column at x=20 (page 2).
  tft.drawPixel(100, 8, TFT_WHITE);
  tft.display(true);
  g_panel.drain();
  g_panel.clearTraffic();
  CHECK_EQ(g_panel.firstLitColumn(1), 100, "page A marker starts at column 100");

  tft.beginSlide(+1);
  CHECK(tft.sliding(), "beginSlide(+1) starts a slide");
  tft.fillScreen(TFT_BLACK);
  tft.drawPixel(20, 16, TFT_WHITE);

  int prev_a_col = 101;
  int prev_b_col = -1;
  int frames = 0;
  const uint32_t t0 = millis();
  uint32_t bus_ms = 0;
  while (tft.sliding()) {
    const size_t before = stub_i2c_log.size();
    const uint32_t tb = millis();
    tft.display();
    bus_ms += millis() - tb;
    if (stub_i2c_log.size() != before) {
      g_panel.drain();
      g_panel.clearTraffic();
      const int a_col = g_panel.firstLitColumn(1);
      const int b_col = g_panel.firstLitColumn(2);
      if (a_col >= 0) {
        CHECK(a_col < prev_a_col, "the old page moves left, never back");
        prev_a_col = a_col;
      }
      if (b_col >= 0) {
        if (prev_b_col >= 0) CHECK(b_col < prev_b_col, "the new page enters from the right and moves left");
        prev_b_col = b_col;
      }
      frames++;
    }
    delay(1);
  }
  const uint32_t slide_ms = millis() - t0;
  CHECK(frames >= 5 && frames <= 12, "a slide is about 6-10 composed frames");
  CHECK(prev_b_col >= 0, "the new page entered the screen during the slide");
  const uint32_t occupancy = slide_ms ? bus_ms * 100u / slide_ms : 100u;
  CHECK(occupancy <= 75u, "the I2C flush takes no more than 75% of the loop while sliding");
  std::printf("ok slide: %d frames in %lu ms (~%u fps), bus busy %u%% of that time\n", frames,
              (unsigned long)slide_ms, (unsigned)(frames * 1000u / (slide_ms ? slide_ms : 1)),
              (unsigned)occupancy);

  settle(tft);
  g_panel.drain();
  CHECK_EQ(g_panel.firstLitColumn(1), -1, "old page content is gone at the end of the slide");
  CHECK_EQ(g_panel.firstLitColumn(2), 20, "panel ends exactly on the new page");
  CHECK_EQ(tft.dirtyPages(), 0, "nothing owed to the panel once the slide is over");
}

static void testSlideBackGeometry() {
  stubResetArduino();
  g_panel = Panel();
  PoomDisplay tft;
  tft.init();
  tft.drawPixel(20, 8, TFT_WHITE);      // old page marker, left side
  tft.display(true);
  g_panel.clearTraffic();

  tft.beginSlide(-1);
  tft.fillScreen(TFT_BLACK);
  tft.drawPixel(100, 16, TFT_WHITE);    // new (parent) page marker, right side

  int prev_a_col = 19;
  int frames = 0;
  while (tft.sliding()) {
    const size_t before = stub_i2c_log.size();
    tft.display();
    if (stub_i2c_log.size() != before) {
      g_panel.drain();
      g_panel.clearTraffic();
      const int a_col = g_panel.firstLitColumn(1);
      if (a_col >= 0) {
        CHECK(a_col > prev_a_col, "back navigation pushes the old page to the right");
        prev_a_col = a_col;
      }
      frames++;
    }
    delay(1);
  }
  settle(tft);
  g_panel.drain();
  CHECK(frames >= 5, "back slide is animated too");
  CHECK_EQ(g_panel.firstLitColumn(2), 100, "back slide ends on the parent page");
  std::printf("ok slide: back navigation animates %d frames and lands on the parent page\n", frames);
}

static void testInterruptedSlide() {
  stubResetArduino();
  g_panel = Panel();
  PoomDisplay tft;
  tft.init();
  tft.drawPixel(10, 8, TFT_WHITE);
  tft.display(true);
  tft.beginSlide(+1);
  delay(10);
  tft.fillScreen(TFT_BLACK);
  tft.drawPixel(20, 8, TFT_WHITE);
  delay(30);
  tft.display();
  // Key repeat can change the page again mid-slide; the driver must not get stuck.
  tft.beginSlide(+1);
  tft.fillScreen(TFT_BLACK);
  tft.drawPixel(30, 8, TFT_WHITE);
  uint32_t guard = 0;
  while (tft.sliding() && guard++ < 10000) { delay(1); tft.display(); }
  CHECK(!tft.sliding(), "a re-triggered slide still finishes");
  settle(tft);
  g_panel.drain();
  CHECK_EQ(g_panel.firstLitColumn(1), 30, "the last requested page wins");
  std::printf("ok slide: re-triggering mid-slide settles on the newest page\n");
}

static void testPanelAbsent() {
  stubResetArduino();
  g_panel = Panel();
  stubWireSetNackAll(true);
  PoomDisplay tft;
  tft.init();                        // a missing panel must not block setup()
  tft.fillScreen(TFT_BLACK);
  tft.drawString("NO PANEL", 10, 10);
  tft.beginSlide(+1);
  tft.fillScreen(TFT_BLACK);
  uint32_t guard = 0;
  while (tft.sliding() && guard++ < 10000) { delay(1); tft.display(); }
  tft.display(true);
  CHECK(!tft.sliding(), "the slide finishes even with a dead bus");
  CHECK(stubWireSawActivity(), "the bus was still driven (NACK path exercised)");
  stubWireSetNackAll(false);
  std::printf("ok display: a missing or NACKing panel cannot hang boot or the loop\n");
}

int main() {
  testPinsAndInit();
  testFullFrameCost();
  testPartialFlushAndNoops();
  testSlideForwardGeometry();
  testSlideBackGeometry();
  testInterruptedSlide();
  testPanelAbsent();
  if (g_failures) {
    std::printf("%d of %d checks FAILED\n", g_failures, g_checks);
    return 1;
  }
  std::printf("ok display: all %d checks passed\n", g_checks);
  std::printf("ALL PASSED\n");
  return 0;
}
