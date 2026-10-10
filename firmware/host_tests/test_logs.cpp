#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include "../upstream/esp32_marauder/RotatingLog.h"

struct MemStore : LogStore {
  std::map<uint32_t, std::string> segs;
  bool append(uint32_t s, const char* d, size_t n) override { segs[s].append(d, n); return true; }
  bool remove(uint32_t s) override { return segs.erase(s) == 1; }
};

static std::string all(const MemStore& st) {
  std::string out;
  for (auto& kv : st.segs) out += kv.second;
  return out;
}

static void testRotationKeepsCap() {
  MemStore st;
  RotatingLog log(st, 64, 3);
  for (int i = 0; i < 300; i++) assert(log.write("line " + std::to_string(i)));
  assert(st.segs.size() == 3);
  assert(log.liveCount() == 3);
  for (auto& kv : st.segs) assert(kv.second.size() <= 64);
  std::string s = all(st);
  assert(s.find("line 299\n") != std::string::npos);  // newest kept
  assert(s.find("line 0\n") == std::string::npos);    // oldest dropped
  std::printf("ok rotation: %zu segments kept, newest intact\n", st.segs.size());
}

static void testFirstRotationDoesNotDoubleSegmentZero() {
  // Regression: advance() used to treat a fresh log as "not started", so the first
  // rotation kept writing into segment 0. Segment 0 then held 2x its slice and the log
  // stayed over its cap for the whole life of the card (resume() trims by segment COUNT,
  // never by bytes, so the oversized file was never noticed).
  MemStore st;
  RotatingLog log(st, 64, 8);
  size_t written = 0;
  for (int i = 0; i < 30; i++) {
    const std::string rec = std::string("0123456789") + std::to_string(i);
    written += rec.size() + 1;   // +1 for the newline
    assert(log.write(rec));
  }
  for (auto& kv : st.segs) assert(kv.second.size() <= 64);
  assert(all(st).size() == written);
  assert(log.oldest() == 0 && log.newest() == 5);    // ~380 B spread over 6 x 64 B
  assert(st.segs.size() == 6);
  std::printf("ok rotation: fresh log uses %zu segments, none over its 64 B slice\n", st.segs.size());
}

static void testNoLossAcrossSegments() {
  MemStore st;
  RotatingLog log(st, 10, 100);
  assert(log.write("0123456789ABCDEF"));
  assert(all(st) == "0123456789ABCDEF\n");
  std::printf("ok split: record spans segments, no loss\n");
}

static void testResumeTrimsToCap() {
  MemStore st;
  for (uint32_t i = 10; i <= 20; i++) st.segs[i] = "x";
  RotatingLog log(st, 64, 4);
  assert(log.resume(10, 20));
  assert(st.segs.size() == 4);
  assert(st.segs.begin()->first == 17);  // oldest 7 deleted, keep 17..20
  assert(log.write("after restart"));
  assert(log.newest() == 21 && log.liveCount() == 4 && st.segs.size() == 4);
  std::printf("ok resume: trimmed to cap and continued at segment %u\n", log.newest());
}

static void testCapFromCardSize() {
  // 16 GB card, 64 KB segments, no reserve -> uses all of it.
  uint64_t sixteenGB = 16ULL * 1000 * 1000 * 1000;
  uint32_t n = computeMaxSegments(sixteenGB, 64 * 1024, 0);
  assert(n == sixteenGB / (64 * 1024));
  assert(computeMaxSegments(sixteenGB, 64 * 1024, 10) < n);   // reserve reduces cap
  // 5% reserve (what the SD log uses): 95% of the card, rounded down to whole segments.
  assert(computeMaxSegments(sixteenGB, 64 * 1024, 5) == (sixteenGB * 95 / 100) / (64 * 1024));
  assert(computeMaxSegments(0, 64 * 1024, 0) == 0);
  assert(computeMaxSegments(1000, 0, 0) == 0);
  std::printf("ok cap: 16 GB -> %u segments of 64 KB (all of card)\n", n);
}

static void testInternal256K() {
  // Internal log: 8 segments x 32 KB = 256 KB cap.
  assert(computeMaxSegments(256 * 1024, 32 * 1024, 0) == 8);
  std::printf("ok internal: 256 KB = 8 x 32 KB\n");
}

int main() {
  testRotationKeepsCap();
  testFirstRotationDoesNotDoubleSegmentZero();
  testNoLossAcrossSegments();
  testResumeTrimsToCap();
  testCapFromCardSize();
  testInternal256K();
  std::printf("ALL PASSED\n");
}
