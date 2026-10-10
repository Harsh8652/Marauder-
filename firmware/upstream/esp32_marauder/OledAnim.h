// Frame-timed easing and slide transitions for the small OLED (128x64).
// Pure math, no hardware calls, so it can be unit tested on the host.
#pragma once
#include <cmath>
#include <cstdint>

// Ease-out cubic: fast start, soft landing. t in [0,1] -> [0,1].
inline float easeOutCubic(float t) {
  if (t <= 0.f) return 0.f;
  if (t >= 1.f) return 1.f;
  float u = 1.f - t;
  return 1.f - u * u * u;
}

// Horizontal slide between two menu pages.
// Returns the pixel offset of the NEW page's left edge relative to the screen,
// going from `width` (off-screen right) to 0. `dir` = +1 slides left (next),
// -1 slides right (back).
inline int slideOffset(int width, uint32_t elapsed_ms, uint32_t duration_ms, int dir) {
  float t = duration_ms ? float(elapsed_ms) / float(duration_ms) : 1.f;
  float e = easeOutCubic(t);
  int off = int(std::lround((1.f - e) * width));
  return dir >= 0 ? off : -off;
}

// Fixed-step frame scheduler: how many ms to wait until the next frame at `fps`.
inline uint32_t frameIntervalMs(uint32_t fps) { return fps ? 1000u / fps : 1000u; }
