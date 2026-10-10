// A software SSD1306/SH1106 panel. It decodes the I2C traffic the driver produces exactly
// the way the chip does (0x00-prefixed transactions are commands, 0x40-prefixed are pixel
// data written at the current page/column with auto-increment) and keeps GDDRAM, so tests
// and the preview tool assert on what the display would really show, not on internals.
//
// Shared by host_tests/test_display.cpp and tools/render_preview.cpp.
#pragma once

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Wire.h"

struct PanelSim {
  static const int kCols = 132;     // SH1106 has 132 columns; the 128-wide panel uses 2..129
  static const int kColOffset = 2;
  uint8_t mem[8][kCols];
  int page = 0;
  int col = 0;
  int display_on = 0;
  int display_off = 0;
  int data_tx = 0;
  int overflow = 0;                 // writes past the last column: pixels the driver lost

  void onTx(const StubI2cTransaction& t) {
    if (t.bytes.empty() || t.address != 0x3C) return;
    if (t.bytes[0] == 0x00) {
      for (size_t i = 1; i < t.bytes.size(); ++i) command(t.bytes[i]);
    } else if (t.bytes[0] == 0x40) {
      data_tx++;
      for (size_t i = 1; i < t.bytes.size(); ++i) {
        if (col >= kCols) { overflow++; continue; }
        mem[page][col++] = t.bytes[i];
      }
    }
  }

  void command(uint8_t c) {
    if ((c & 0xF8) == 0xB0) page = c & 7;
    else if ((c & 0xF0) == 0x00) col = (col & 0x30) | (c & 0x0F);
    else if ((c & 0xF0) == 0x10) col = (col & 0x0F) | ((c & 0x0F) << 4);
    else if (c == 0xAF) display_on++;
    else if (c == 0xAE) display_off++;
  }

  void drain() {
    for (const auto& tx : stub_i2c_log) onTx(tx);
  }
  void clearTraffic() { stub_i2c_log.clear(); data_tx = 0; }

  // The visible 128x64 area, in the driver's framebuffer layout.
  void grab(uint8_t* out) const {
    for (int p = 0; p < 8; ++p)
      for (int x = 0; x < 128; ++x) out[p * 128 + x] = mem[p][x + kColOffset];
  }

  // Column of the first lit byte in a page, or -1 when the page is blank.
  int firstLitColumn(int page_index) const {
    for (int x = 0; x < 128; ++x)
      if (mem[page_index][x + kColOffset]) return x;
    return -1;
  }

  // PPM (P3, scaled) so the frames can be watched or converted to a GIF.
  bool writePpm(const char* path, int scale) const {
    uint8_t fb[1024];
    grab(fb);
    const int w = 128 * scale, h = 64 * scale;
    std::string s;
    s.reserve((size_t)w * h * 3 + 64);
    s += "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
    for (int y = 0; y < h; ++y) {
      const int row = y / scale;
      for (int x = 0; x < w; ++x) {
        const int colx = x / scale;
        const bool lit = fb[colx + (row >> 3) * 128] & (1U << (row & 7));
        const char v = lit ? (char)0xD0 : (char)0x18;   // OLED-ish: pale on near-black
        s.push_back(v);
        s.push_back(v);
        s.push_back(v);
      }
    }
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const size_t n = std::fwrite(s.data(), 1, s.size(), f);
    std::fclose(f);
    return n == s.size();
  }
};
