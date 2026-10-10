// Horizontal slide composition for the 128x64 SSD1306/SH1106 framebuffer
// (layout: byte index = page*128 + column, 8 vertical pixels per byte).
// Hardware-free so it can be unit tested on the host.
#pragma once
#include <cstddef>
#include <cstdint>

// One panel page (128 columns of 8 vertical pixels) of the slide.
// shift = 0 -> `prev` exactly, shift = 128 -> `next` exactly.
// forward: the old page leaves to the left and the new page enters from the right.
inline void composePage(uint8_t* dst, const uint8_t* prev, const uint8_t* next,
                        int shift, bool forward) {
  if (shift < 0) shift = 0;
  if (shift > 128) shift = 128;
  for (int x = 0; x < 128; ++x) {
    const int src = forward ? (x + shift) : (x - shift);
    if (forward)
      dst[x] = (src < 128) ? prev[src] : next[src - 128];
    else
      dst[x] = (src >= 0) ? prev[src] : next[src + 128];
  }
}

// dst = frame shown during the slide.
//   shift = 0   -> exactly `prev` (old page, at rest)
//   shift = 128 -> exactly `next` (new page, at rest)
// The old page moves left by `shift` columns; the new page enters from the right.
// dst, prev and next must each hold 1024 bytes and must not overlap.
inline void composeSlide(uint8_t* dst, const uint8_t* prev, const uint8_t* next, int shift) {
  for (int page = 0; page < 8; ++page)
    composePage(dst + page * 128, prev + page * 128, next + page * 128, shift, true);
}

// Reverse slide (used for "back"): the old page moves right by `shift` columns and
// the new page enters from the left. shift 0 -> prev, shift 128 -> next.
inline void composeSlideBack(uint8_t* dst, const uint8_t* prev, const uint8_t* next, int shift) {
  for (int page = 0; page < 8; ++page)
    composePage(dst + page * 128, prev + page * 128, next + page * 128, shift, false);
}

// Which way a page transition should slide, or 0 for "do not animate".
// A refresh of the same menu must not slide (the menus redraw often), and the very first
// page has nothing to slide from. "Back" is detected by the target being the parent menu,
// which is how the menu tree actually encodes going up.
inline int slideDirection(bool page_changed, bool had_previous_page, bool target_is_parent) {
  if (!page_changed || !had_previous_page) return 0;
  return target_is_parent ? -1 : 1;
}

// Slide length. 300 ms gives about 10 frames at 30 fps (200 ms only gives ~7).
constexpr uint32_t kSlideDurationMs = 300;

// Pacing, in ms between two flushes. A full 1024-byte frame costs ~27 ms on a 400 kHz
// I2C bus (measured by host_tests/test_display.cpp), and the flush blocks the main loop,
// so a slide is paced at 20 fps: the bus then occupies ~55% of the loop instead of ~80%.
// Ordinary updates are cheap (only the pages that changed are sent), so they keep 30 fps.
constexpr uint32_t kSlideFrameIntervalMs = 50;
constexpr uint32_t kFrameIntervalMs = 33;
