#include "LogStores.h"

#ifdef MARAUDER_OLED128X64

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <SD.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "RotatingLog.h"

namespace {

// Internal log: 256 KB total = 8 segments x 32 KB.
constexpr size_t kInternalSegBytes = 32 * 1024;
constexpr uint32_t kInternalSegs = 8;

// SD log: 64 KB segments. Cap is computed from the card at boot.
constexpr size_t kSdSegBytes = 64 * 1024;
// Keep this share of the card free so FAT has room for writes near the cap.
constexpr unsigned kSdReservePercent = 5;

constexpr const char* kInternalDir = "/logs";
constexpr const char* kSdDir = "/marauder_logs";

std::string segPath(const char* dir, uint32_t seg) {
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%s/%08lu.log", dir, static_cast<unsigned long>(seg));
  return buf;
}

// Parse "/dir/00000012.log" -> 12. Returns false if the name does not match.
bool parseSeg(const String& name, uint32_t& out) {
  const char* s = name.c_str();
  const char* slash = strrchr(s, '/');
  const char* base = slash ? slash + 1 : s;
  unsigned long v = 0;
  int digits = 0;
  for (const char* p = base; *p && *p != '.'; ++p) {
    if (*p < '0' || *p > '9') return false;
    v = v * 10 + (*p - '0');
    digits++;
  }
  if (digits == 0) return false;
  out = static_cast<uint32_t>(v);
  return true;
}

// Find the oldest and newest existing segment in `dir`.
bool scanRange(fs::FS& fs, const char* dir, uint32_t& oldest, uint32_t& newest) {
  bool any = false;
  if (!fs.exists(dir)) return false;
  File d = fs.open(dir);
  if (!d || !d.isDirectory()) return false;
  File f = d.openNextFile();
  while (f) {
    uint32_t n;
    if (!f.isDirectory() && parseSeg(String(f.name()), n)) {
      if (!any || n < oldest) oldest = n;
      if (!any || n > newest) newest = n;
      any = true;
    }
    f = d.openNextFile();
  }
  d.close();
  return any;
}

class FsLogStore : public LogStore {
 public:
  FsLogStore(fs::FS& fs, const char* dir) : fs_(fs), dir_(dir) {}

  bool append(uint32_t seg, const char* data, size_t len) override {
    if (!fs_.exists(dir_)) fs_.mkdir(dir_);
    File f = fs_.open(segPath(dir_, seg).c_str(), FILE_APPEND);
    if (!f) return false;
    size_t w = f.write(reinterpret_cast<const uint8_t*>(data), len);
    f.close();
    return w == len;
  }

  bool remove(uint32_t seg) override {
    return fs_.remove(segPath(dir_, seg).c_str());
  }

 private:
  fs::FS& fs_;
  const char* dir_;
};

FsLogStore* gInternalStore = nullptr;
FsLogStore* gSdStore = nullptr;
RotatingLog* gInternal = nullptr;
RotatingLog* gSd = nullptr;
unsigned long long gSdCap = 0;

}  // namespace

unsigned long logInternalCapBytes() { return kInternalSegBytes * kInternalSegs; }

unsigned long long logSdCapBytes() { return gSdCap; }

void logInit() {
  // Internal log (SPIFFS). The cap is fixed at 256 KB.
  if (SPIFFS.begin(true)) {
    gInternalStore = new FsLogStore(SPIFFS, kInternalDir);
    gInternal = new RotatingLog(*gInternalStore, kInternalSegBytes, kInternalSegs);
    uint32_t lo = 0, hi = 0;
    if (scanRange(SPIFFS, kInternalDir, lo, hi)) gInternal->resume(lo, hi);
  }

  // SD log (optional). The cap is the card's real volume size, all of it (no reserve).
  if (SD.cardType() != CARD_NONE) {
    gSdCap = SD.totalBytes();
    uint32_t maxSegs = computeMaxSegments(gSdCap, kSdSegBytes, kSdReservePercent);
    if (maxSegs > 0) {
      gSdStore = new FsLogStore(SD, kSdDir);
      gSd = new RotatingLog(*gSdStore, kSdSegBytes, maxSegs);
      uint32_t lo = 0, hi = 0;
      if (scanRange(SD, kSdDir, lo, hi)) gSd->resume(lo, hi);
    }
  } else {
    gSdCap = 0;
  }
}

void marauderLog(const char* tag, const char* message) {
  char line[256];
  std::snprintf(line, sizeof(line), "[%lu] %s: %s", millis(), tag, message);
  if (gInternal) gInternal->write(line);
  if (gSd) gSd->write(line);
}

#endif  // MARAUDER_OLED128X64
