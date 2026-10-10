// Horizontal slide composition for the 128x64 SSD1306/SH1106 framebuffer
// (layout: byte index = page*128 + column, 8 vertical pixels per byte).
// Hardware-free so it can be unit tested on the host.
#pragma once
#include <cstddef>
#include <cstdint>

// dst = frame shown during the slide.
//   shift = 0   -> exactly `prev` (old page, at rest)
//   shift = 128 -> exactly `next` (new page, at rest)
// The old page moves left by `shift` columns; the new page enters from the right.
// dst, prev and next must each hold 1024 bytes and must not overlap.
inline void composeSlide(uint8_t* dst, const uint8_t* prev, const uint8_t* next, int shift) {
  if (shift < 0) shift = 0;
  if (shift > 128) shift = 128;
  for (int page = 0; page < 8; ++page) {
    const int row = page * 128;
    for (int x = 0; x < 128; ++x) {
      const int src = x + shift;
      dst[row + x] = (src < 128) ? prev[row + src] : next[row + (src - 128)];
    }
  }
}

// Reverse slide (used for "back"): the old page moves right by `shift` columns and
// the new page enters from the left. shift 0 -> prev, shift 128 -> next.
inline void composeSlideBack(uint8_t* dst, const uint8_t* prev, const uint8_t* next, int shift) {
  if (shift < 0) shift = 0;
  if (shift > 128) shift = 128;
  for (int page = 0; page < 8; ++page) {
    const int row = page * 128;
    for (int x = 0; x < 128; ++x) {
      const int src = x - shift;
      dst[row + x] = (src >= 0) ? prev[row + src] : next[row + (src + 128)];
    }
  }
}

// Slide length. 300 ms gives about 10 frames at 30 fps (200 ms only gives ~7).
constexpr uint32_t kSlideDurationMs = 300;
