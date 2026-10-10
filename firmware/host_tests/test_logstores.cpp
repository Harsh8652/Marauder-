// Executes the real LogStores.cpp (SPIFFS + SD rotating logs) against the in-memory
// filesystem in stubs/FS.h. Everything here mirrors what happens on the board: mount,
// boot record, rotation at the cap, resume after a reboot, an SD card present or absent,
// and writes that fail because the medium is full or dying.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "Arduino.h"
#include "FS.h"
#include "SPIFFS.h"
#include "SD.h"
#include "configs.h"
#include "LogStores.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, what)                                          \
  do {                                                             \
    g_checks++;                                                    \
    if (!(cond)) {                                                 \
      g_failures++;                                                \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, (what)); \
    }                                                              \
  } while (0)

#define CHECK_EQ(a, b, what)                                                                  \
  do {                                                                                         \
    g_checks++;                                                                                \
    const long long _x = (long long)(a), _y = (long long)(b);                                  \
    if (_x != _y) {                                                                             \
      g_failures++;                                                                             \
      std::printf("FAIL %s:%d  %s (got %lld, want %lld)\n", __FILE__, __LINE__, (what),         \
                  _x, _y);                                                                      \
    }                                                                                           \
  } while (0)

static std::map<std::string, std::string> snapshot(const stubfs::Volume& v, const char* prefix) {
  std::map<std::string, std::string> out;
  for (const auto& kv : v.files)
    if (kv.first.compare(0, strlen(prefix), prefix) == 0)
      out[kv.first] = std::string(kv.second.begin(), kv.second.end());
  return out;
}

// Concatenation of every log file in a directory, in segment order.
static std::string logText(const stubfs::Volume& v, const char* dir) {
  std::string out;
  for (const auto& kv : snapshot(v, dir)) out += kv.second;
  return out;
}

static size_t countLines(const std::string& s) {
  return std::count(s.begin(), s.end(), '\n');
}

static void writeRecords(int n, const char* tag, const char* body, size_t pad = 0) {
  const std::string filler(pad, 'x');
  char msg[256];
  for (int i = 0; i < n; ++i) {
    std::snprintf(msg, sizeof(msg), "%s %d%s", body, i, filler.c_str());
    marauderLog(tag, msg);
  }
}

static void testBootWithNoCard() {
  stubResetArduino();
  SD.stubRemove();
  CHECK(SPIFFS.begin(true), "SPIFFS mounts");
  logInit();

  CHECK_EQ(logInternalCapBytes(), 256u * 1024u, "internal log cap is 256 KB");
  CHECK_EQ(logSdCapBytes(), 0, "no SD card -> no SD log");
  CHECK(!logSdPresent(), "logSdPresent() is false without a card");

  marauderLog("boot", "hello");
  const auto files = snapshot(stubfs::spiffs(), "/logs/");
  CHECK_EQ(files.size(), 1u, "the first record creates exactly one segment");
  CHECK(files.count("/logs/00000000.log") == 1, "segment names are /logs/%08lu.log");
  const std::string text = logText(stubfs::spiffs(), "/logs/");
  CHECK(text.find("hello") != std::string::npos, "the record is on disk");
  CHECK(text.compare(0, 1, "[") == 0, "records are prefixed with a uptime timestamp");
  CHECK(text.find("] boot: hello\n") != std::string::npos, "format is [<millis>] tag: message\\n");
  std::printf("ok logs: boot record lands in /logs/00000000.log, no SD log without a card\n");
}

static void testCardTooSmallForOneSegment() {
  stubResetArduino();
  SPIFFS.begin(true);
  SD.stubInsert(CARD_SD, 40u * 1024u);   // 40 KB card: 95% of it is under one 64 KB segment
  SD.begin();
  logInit();
  CHECK(!logSdPresent(), "a card that cannot hold one segment gets no SD log");
  CHECK_EQ(logSdCapBytes(), 0, "and it does not claim a cap the UI could not honour");
  marauderLog("boot", "still logs to flash");
  CHECK_EQ(snapshot(stubfs::spiffs(), "/logs/").size(), 1u, "the internal log keeps working");
  CHECK_EQ(snapshot(stubfs::sd(), "/marauder_logs/").size(), 0u, "nothing is written to the card");
  std::printf("ok logs: a card smaller than one segment is declined, not half-adopted\n");
}

static void testRotationStaysUnderCap() {
  stubResetArduino();
  SPIFFS.begin(true);
  logInit();

  writeRecords(12000, "scan", "ap seen");  // ~320 KB offered to a 256 KB cap
  const auto files = snapshot(stubfs::spiffs(), "/logs/");
  CHECK_EQ(files.size(), 8u, "the internal log keeps exactly 8 x 32 KB segments");
  uint64_t used = 0;
  for (const auto& kv : files) {
    used += kv.second.size();
    CHECK(kv.second.size() <= 32u * 1024u, "no segment grows past its 32 KB slice");
  }
  CHECK(used <= logInternalCapBytes(), "bytes on flash never exceed the 256 KB cap");

  const std::string text = logText(stubfs::spiffs(), "/logs/");
  CHECK(text.find("ap seen 11999") != std::string::npos, "the newest record is kept");
  CHECK(text.find("ap seen 0\n") == std::string::npos, "the oldest records were dropped");
  std::printf("ok logs: rotation holds the cap (%llu B in %zu files)\n",
              (unsigned long long)used, files.size());
}

static void testRecordSplitsWithoutLoss() {
  stubResetArduino();
  SPIFFS.begin(true);
  logInit();
  // A record long enough to straddle a segment boundary must not be dropped or reordered.
  writeRecords(900, "beacon", "long record", 200);   // ~230 KB of 256-byte records
  const std::string text = logText(stubfs::spiffs(), "/logs/");
  CHECK_EQ(countLines(text), 900u, "every record appears exactly once, even split across segments");
  // Read the sequence numbers back out in file order: they must be 0..899 with no gaps,
  // which is what proves a record split over a segment boundary lost nothing.
  bool in_order = true;
  int expected = 0;
  for (size_t pos = 0; pos < text.size();) {
    const size_t eol = text.find('\n', pos);
    const std::string line = text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
    const size_t tag = line.find("long record ");
    if (tag == std::string::npos) { in_order = false; break; }
    const int got = std::atoi(line.c_str() + tag + 12);
    if (got != expected) { in_order = false; break; }
    ++expected;
    pos = (eol == std::string::npos) ? text.size() : eol + 1;
  }
  CHECK(in_order && expected == 900, "records stay in order across all 900 writes");
  std::printf("ok logs: 900 padded records, split across segments, none lost\n");
}

static void testResumeAfterReboot() {
  stubResetArduino();
  SPIFFS.begin(true);
  logInit();
  writeRecords(60, "run", "before-reboot");
  const auto before = snapshot(stubfs::spiffs(), "/logs/");
  const std::string before_text = logText(stubfs::spiffs(), "/logs/");

  // A reboot is just a remount plus logInit() over the same medium.
  logInit();
  marauderLog("boot", "after-reboot");
  const auto after = snapshot(stubfs::spiffs(), "/logs/");
  const std::string after_text = logText(stubfs::spiffs(), "/logs/");

  CHECK(after.size() >= before.size(), "resume does not delete anything");
  CHECK(after_text.find("before-reboot 59") != std::string::npos, "old records survive a reboot");
  CHECK(after_text.find("after-reboot") != std::string::npos, "writing continues after a reboot");
  CHECK(after_text.size() > before_text.size(), "the resumed log appends instead of truncating");
  std::printf("ok logs: resume keeps %zu files and appends to a fresh segment\n", after.size());
}

static void testForeignFilesIgnored() {
  stubResetArduino();
  SPIFFS.begin(true);
  SPIFFS.mkdir("/logs");
  {
    File f = SPIFFS.open("/logs/readme.txt", FILE_WRITE);
    f.print("not a segment");
    f.close();
    File g = SPIFFS.open("/logs/00000007.log", FILE_WRITE);
    g.print("old");
    g.close();
    File h = SPIFFS.open("/logs/00000003.log", FILE_WRITE);
    h.print("older");
    h.close();
  }
  logInit();
  marauderLog("boot", "after stray files");
  const auto files = snapshot(stubfs::spiffs(), "/logs/");
  CHECK(files.count("/logs/readme.txt") == 1, "a stray file in the log dir is left alone");
  CHECK(files.count("/logs/00000003.log") == 1, "an old segment inside the cap is kept");
  CHECK(files.count("/logs/00000008.log") == 1, "numbering continues after the highest segment found");
  std::printf("ok logs: non-segment files are ignored and numbering continues at 8\n");
}

static void testBareNamesMode() {
  // Older cores return only the file name from File::name(); the scan must work either way.
  stubResetArduino();
  SPIFFS.begin(true);
  stubfs::spiffs().bare_names = true;
  logInit();
  writeRecords(20000, "scan", "row");   // > 256 KB, so the cap must bite
  logInit();   // reboot, still with bare-name directory listings
  marauderLog("boot", "bare names");
  const std::string text = logText(stubfs::spiffs(), "/logs/");
  CHECK(text.find("bare names") != std::string::npos, "resume works when name() is a bare name");
  CHECK_EQ(snapshot(stubfs::spiffs(), "/logs/").size(), 8u, "the cap is still enforced");
  std::printf("ok logs: directory scanning works with full paths and bare names\n");
}

static void testSdCardCapAndMirroring() {
  stubResetArduino();
  SPIFFS.begin(true);
  const uint64_t gb16 = 16ull * 1024ull * 1024ull * 1024ull;
  SD.stubInsert(CARD_SDHC, gb16);
  SD.begin();
  logInit();

  CHECK_EQ(logSdCapBytes(), (long long)gb16, "logSdCapBytes() reports the card volume size");
  CHECK(logSdPresent(), "logSdPresent() is true once the SD log exists");
  marauderLog("boot", "mirrored");
  const auto sd_files = snapshot(stubfs::sd(), "/marauder_logs/");
  CHECK_EQ(sd_files.size(), 1u, "the SD log has its own directory and segment");
  CHECK(sd_files.count("/marauder_logs/00000000.log") == 1, "SD segments live in /marauder_logs");
  CHECK(logText(stubfs::sd(), "/marauder_logs/").find("mirrored") != std::string::npos,
        "the record lands on the card");
  CHECK(logText(stubfs::spiffs(), "/logs/").find("mirrored") != std::string::npos,
        "the same record still goes to flash");
  std::printf("ok logs: 16 GB card accepted, records mirrored to /marauder_logs\n");
}

static void testSdRotationRespectsReserve() {
  stubResetArduino();
  SPIFFS.begin(true);
  // 140 KB card: 95% usable = 133 KB -> two 64 KB segments, and the reserve is the margin
  // that keeps the last write from failing on an otherwise full card.
  const uint64_t kb140 = 140ull * 1024ull;
  SD.stubInsert(CARD_SD, kb140);
  SD.begin();
  logInit();
  writeRecords(900, "sd", "record", 200);   // ~230 KB offered, ~128 KB kept

  const auto sd_files = snapshot(stubfs::sd(), "/marauder_logs/");
  CHECK_EQ(sd_files.size(), 2u, "a 140 KB card keeps 2 x 64 KB segments after the 5% reserve");
  uint64_t used = stubfs::usedBytes(stubfs::sd());
  CHECK(used <= kb140 * 95ull / 100ull, "the log never uses the reserved 5% of the card");
  const std::string text = logText(stubfs::sd(), "/marauder_logs/");
  CHECK(text.find("record 899") != std::string::npos, "the newest SD record is kept");
  std::printf("ok logs: SD rotation keeps 2 segments, %llu B used of %llu B (%u%% reserve)\n",
              (unsigned long long)used, (unsigned long long)kb140, 5u);
}

static void testFailingWritesDoNotCorrupt() {
  stubResetArduino();
  SPIFFS.begin(true);
  logInit();
  writeRecords(20, "ok", "written");
  const std::string good = logText(stubfs::spiffs(), "/logs/");
  stubfs::spiffs().fail_writes = true;
  writeRecords(500, "io", "must not appear");
  stubfs::spiffs().fail_writes = false;
  marauderLog("ok", "recovered");
  const std::string after = logText(stubfs::spiffs(), "/logs/");
  CHECK(after.find("written 19") != std::string::npos, "a failing medium does not destroy existing records");
  CHECK(after.find("must not appear") == std::string::npos, "failed writes stay absent");
  CHECK(after.find("recovered") != std::string::npos, "logging recovers as soon as writes work again");
  CHECK_EQ(countLines(after), countLines(good) + 1, "a storm of failures adds no partial records");
  std::printf("ok logs: write failures are absorbed without corrupting or duplicating records\n");
}

int main() {
  testBootWithNoCard();
  testRotationStaysUnderCap();
  testRecordSplitsWithoutLoss();
  testResumeAfterReboot();
  testForeignFilesIgnored();
  testBareNamesMode();
  testCardTooSmallForOneSegment();
  testSdCardCapAndMirroring();
  testSdRotationRespectsReserve();
  testFailingWritesDoNotCorrupt();
  if (g_failures) {
    std::printf("%d of %d checks FAILED\n", g_failures, g_checks);
    return 1;
  }
  std::printf("ok logs: all %d checks passed\n", g_checks);
  std::printf("ALL PASSED\n");
  return 0;
}
