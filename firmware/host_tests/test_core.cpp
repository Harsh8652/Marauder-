#include <cassert>
#include <cstdio>
#include <map>
#include "../upstream/esp32_marauder/RotatingLog.h"
#include "../upstream/esp32_marauder/OledAnim.h"

struct MemStore : LogStore {
  std::map<uint32_t, std::string> segs;
  bool append(uint32_t s, const char* d, size_t n) override { segs[s].append(d, n); return true; }
  bool remove(uint32_t s) override { return segs.erase(s) == 1; }
};

static void testRotationKeepsLimit() {
  MemStore st;
  RotatingLog log(st, 64, 3);
  for (int i = 0; i < 100; i++) assert(log.write("line " + std::to_string(i)));
  assert(st.segs.size() == 3);                 // never more than 3 files
  assert(log.liveCount() == 3);
  // The newest data must survive; the oldest must be gone.
  std::string all;
  for (auto& kv : st.segs) all += kv.second;
  assert(all.find("line 99") != std::string::npos);
  assert(all.find("line 0\n") == std::string::npos);
  for (auto& kv : st.segs) assert(kv.second.size() <= 64);
  std::printf("ok rotation: kept %zu segments, newest intact\n", st.segs.size());
}

static void testNoRecordSplitLoss() {
  MemStore st;
  RotatingLog log(st, 10, 100);
  assert(log.write("0123456789ABCDEF"));
  std::string all;
  for (auto& kv : st.segs) all += kv.second;
  assert(all == "0123456789ABCDEF\n");        // bytes preserved across segments
  std::printf("ok split: record spans segments without loss\n");
}

static void testEaseAndSlide() {
  assert(easeOutCubic(0.f) == 0.f && easeOutCubic(1.f) == 1.f);
  assert(easeOutCubic(0.5f) > 0.5f);           // ease-out: front-loaded
  assert(slideOffset(128, 0, 200, 1) == 128);
  assert(slideOffset(128, 200, 200, 1) == 0);
  int prev = 129;
  for (uint32_t t = 0; t <= 200; t += 16) {    // monotonic toward 0
    int o = slideOffset(128, t, 200, 1);
    assert(o <= prev);
    prev = o;
  }
  assert(slideOffset(128, 0, 200, -1) == -128);
  assert(frameIntervalMs(30) == 33);
  std::printf("ok anim: easing monotonic, endpoints correct, 30fps=%ums\n", frameIntervalMs(30));
}

int main() {
  testRotationKeepsLimit();
  testNoRecordSplitLoss();
  testEaseAndSlide();
  std::printf("ALL PASSED\n");
}
