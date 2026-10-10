// Renders the OLED menu slide with the real driver, so the animation can be looked at
// without hardware. It draws two menu pages with the same calls and geometry the firmware
// uses on a 128x64 panel (status bar 0-7, five 10 px rows from y=8), asks the driver to
// slide between them, and writes a PPM for every frame the driver pushes to the panel.
//
//   sh firmware/tools/render_preview.sh /tmp/oled
//   -> /tmp/oled/frame-00.ppm ... and /tmp/oled/slide.gif when ImageMagick is installed
#include <cstdio>
#include <cstring>
#include <string>

#include "Arduino.h"
#include "PanelSim.h"
#include "Wire.h"
#include "configs.h"
#include "OledSlide.h"
#include "PoomDisplay.h"

namespace {

const char* kMainItems[] = {"Scan Networks", "Beacon Flood", "Deauth Detector", "Evil Portal",
                            "Wardrive"};
const char* kAttackItems[] = {"Beacon Storm", "Probe Request", "Association Flood", "EAPOL Start",
                              "Quit Attack"};

// Mirrors Display::drawStatusBar() for MARAUDER_OLED128X64 (the three fields the OLED shows).
void drawStatusBar(PoomDisplay& tft, int channel, int dram_percent, bool sd) {
  tft.fillRect(0, 0, 44, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
  tft.fillRect(46, 0, 34, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
  tft.fillRect(100, 0, 60, STATUS_BAR_WIDTH, STATUSBAR_COLOR);
  tft.setTextColor(TFT_WHITE, STATUSBAR_COLOR);
  tft.drawString("C" + (String)channel, 0, 0, 1);
  tft.drawString((String)dram_percent + "%", 46, 0, 1);
  tft.drawString(sd ? "SD" : "--", 116, 0, 1);
}

// Mirrors MenuFunctions::displayCurrentMenu() + drawMiniMenuButton() on this board:
// banner at y=8, then one KEY_H-tall row per item at y = KEY_Y - KEY_H/2 + b*(KEY_H + spacing).
void drawMenu(PoomDisplay& tft, const char* name, const char* const* items, int count, int selected) {
  tft.fillScreen(TFT_BLACK);
  drawStatusBar(tft, 6, 31, true);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString(name, SCREEN_WIDTH / 2, STATUS_BAR_WIDTH, 1);
  for (int b = 0; b < count; ++b) {
    const int x = KEY_X - (KEY_W / 2);
    const int y = (KEY_Y + (b * (KEY_H + KEY_SPACING_Y))) - (KEY_H / 2);
    const bool sel = (b == selected);
    tft.fillRect(x, y, KEY_W, KEY_H, sel ? TFT_LIGHTGREY : TFT_BLACK);
    tft.setTextColor(sel ? TFT_BLACK : TFT_WHITE, sel ? TFT_LIGHTGREY : TFT_BLACK);
    tft.setCursor(x + BUTTON_PADDING, y + 1);
    tft.print(items[b]);
  }
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
}

}  // namespace

int main(int argc, char** argv) {
  const char* out_dir = (argc > 1) ? argv[1] : ".";
  stubResetArduino();
  PanelSim panel;
  PoomDisplay tft;
  tft.init();
  panel.drain();

  int frame = 0;
  char path[256];

  // Page 1: the top-level menu, first item selected.
  drawMenu(tft, "MARAUDER", kMainItems, 5, 0);
  tft.display(true);
  panel.drain();
  std::snprintf(path, sizeof(path), "%s/frame-%02d.ppm", out_dir, frame++);
  panel.writePpm(path, 2);

  // Forward slide into the attack submenu (what changeMenu does when you press OK).
  tft.beginSlide(+1);
  drawMenu(tft, "ATTACKS", kAttackItems, 5, 1);
  while (tft.sliding()) {
    const size_t before = stub_i2c_log.size();
    tft.display();
    if (stub_i2c_log.size() != before) {
      panel.drain();
      std::snprintf(path, sizeof(path), "%s/frame-%02d.ppm", out_dir, frame++);
      panel.writePpm(path, 2);
    }
    delay(1);
  }
  delay(kFrameIntervalMs + 1);
  tft.display(true);
  panel.drain();
  std::snprintf(path, sizeof(path), "%s/frame-%02d.ppm", out_dir, frame++);
  panel.writePpm(path, 2);

  // Back slide to the parent menu (B button).
  tft.beginSlide(-1);
  drawMenu(tft, "MARAUDER", kMainItems, 5, 0);
  while (tft.sliding()) {
    const size_t before = stub_i2c_log.size();
    tft.display();
    if (stub_i2c_log.size() != before) {
      panel.drain();
      std::snprintf(path, sizeof(path), "%s/frame-%02d.ppm", out_dir, frame++);
      panel.writePpm(path, 2);
    }
    delay(1);
  }
  delay(kFrameIntervalMs + 1);
  tft.display(true);
  panel.drain();
  std::snprintf(path, sizeof(path), "%s/frame-%02d.ppm", out_dir, frame++);
  panel.writePpm(path, 2);

  std::printf("wrote %d frames to %s (128x64, scaled x2)\n", frame, out_dir);
  return 0;
}
