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
  for (uint32_t t = 0; t <= kSlideDurationMs; t += frameIntervalMs(30)) {
    int shift = 128 - slideOffset(128, t, kSlideDurationMs, 1);     // new page offset 128->0 => shift 0->128
    assert(shift >= last);                             // never goes backwards
    last = shift;
    composeSlide(dst, prev, next, shift);
    frames++;
  }
  assert(last == 128 || last >= 120);                  // reaches (near) the end
  assert(frames >= 9);                                 // ~10 frames at 30 fps
  std::printf("ok slide: %d frames at 30 fps over %u ms, endpoints exact\n", frames, (unsigned)kSlideDurationMs);

  // When is a transition worth animating at all? Same-menu refreshes must stay still,
  // the first page has nothing to slide away, and going up slides the other way.
  assert(slideDirection(false, true, false) == 0);   // refresh of the current menu
  assert(slideDirection(true, false, false) == 0);   // first page after boot
  assert(slideDirection(false, false, false) == 0);  // nothing at all
  assert(slideDirection(true, true, false) == 1);    // into a submenu: forward
  assert(slideDirection(true, true, true) == -1);    // to the parent: back
  // The direction feeds the compositor, so check the two ends agree on its meaning.
  {
    uint8_t a[1024], b[1024], f[1024];
    memset(a, 0x0F, sizeof(a));
    memset(b, 0xF0, sizeof(b));
    for (uint32_t t = 0; t <= kSlideDurationMs; t += frameIntervalMs(30)) {
      const int off = slideOffset(128, t, kSlideDurationMs, slideDirection(true, true, false));
      const int shift = 128 - (off < 0 ? -off : off);
      composeSlide(f, a, b, shift);
      assert(f[0] == (shift < 128 ? a[shift] : b[0]));
      const int off_back = slideOffset(128, t, kSlideDurationMs, slideDirection(true, true, true));
      assert(off_back <= 0);
      composeSlideBack(f, a, b, 128 - (-off_back));
      assert(f[127] == (shift < 128 ? a[127 - shift] : b[127]));
    }
  }
  std::printf("ok slide: animate/skip and forward/back rule matches the compositor\n");
  std::printf("ALL PASSED\n");
}

// Reverse slide checks (runs from main via a second entry point below).
struct ReverseCheck {
  ReverseCheck() {
    uint8_t prev[1024], next[1024], dst[1024];
    for (int i = 0; i < 1024; i++) { prev[i] = uint8_t(i); next[i] = uint8_t(255 - i); }
    composeSlideBack(dst, prev, next, 0);
    assert(std::memcmp(dst, prev, 1024) == 0);
    composeSlideBack(dst, prev, next, 128);
    assert(std::memcmp(dst, next, 1024) == 0);
    composeSlideBack(dst, prev, next, 32);
    // column 40 shows prev column 8; column 10 (< shift) shows next column 138-... i.e. next[10-32+128]
    assert(dst[40] == prev[8]);
    assert(dst[10] == next[106]);
    assert(dst[2 * 128 + 0] == next[2 * 128 + 96]);
    std::printf("ok reverse slide: endpoints exact, new page enters from the left\n");
  }
} gReverseCheck;
