#include <cassert>
#include <cstdio>
#include <cstring>
#include "../upstream/esp32_marauder/OledAnim.h"
#include "../upstream/esp32_marauder/OledSlide.h"

int main() {
  uint8_t prev[1024], next[1024], dst[1024];
  for (int i = 0; i < 1024; i++) { prev[i] = uint8_t(i); next[i] = uint8_t(255 - i); }

  composeSlide(dst, prev, next, 0);
  assert(std::memcmp(dst, prev, 1024) == 0);          // at rest: old page
  composeSlide(dst, prev, next, 128);
  assert(std::memcmp(dst, next, 1024) == 0);          // finished: new page

  composeSlide(dst, prev, next, 32);                  // mid-slide spot check
  // page 0, column 0 shows prev column 32; column 100 shows next column 4 (100+32-128)
  assert(dst[0] == prev[32]);
  assert(dst[100] == next[4]);
  // page 3 is independent of page 0 (no bleed across rows)
  assert(dst[3 * 128 + 0] == prev[3 * 128 + 32]);
  assert(dst[3 * 128 + 127] == next[3 * 128 + 31]);

  // Drive the slide with the easing curve for 200 ms at 30 fps.
  int frames = 0, last = -1;
  for (uint32_t t = 0; t <= 200; t += frameIntervalMs(30)) {
    int shift = 128 - slideOffset(128, t, 200, 1);     // new page offset 128->0 => shift 0->128
    assert(shift >= last);                             // never goes backwards
    last = shift;
    composeSlide(dst, prev, next, shift);
    frames++;
  }
  assert(last == 128 || last >= 120);                  // reaches (near) the end
  std::printf("ok slide: %d frames at 30 fps over 200 ms, endpoints exact\n", frames);
  std::printf("ALL PASSED\n");
}
