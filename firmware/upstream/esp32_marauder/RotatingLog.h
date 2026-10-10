// Segmented rotating log (hardware-free, host-testable).
//
// Records go into numbered segments (0,1,2,...). At most `max_segments` are kept.
// When a new segment is needed and the limit is reached, the OLDEST segment is
// deleted first, so the newest data is always kept and the storage never grows
// past the cap. Only two integers are kept in RAM, so the cap can be very large
// (for example an SD card with millions of segments).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

struct LogStore {
  virtual ~LogStore() = default;
  // Append bytes to segment `seg` (create it if missing). Returns false on error.
  virtual bool append(uint32_t seg, const char* data, size_t len) = 0;
  // Delete segment `seg` if it exists. Returns false on error.
  virtual bool remove(uint32_t seg) = 0;
};

// How many segments fit in `total_bytes` after keeping `reserve_percent` free.
// reserve_percent = 0 uses the whole capacity (as requested for the SD card).
inline uint32_t computeMaxSegments(uint64_t total_bytes, size_t segment_bytes,
                                   unsigned reserve_percent) {
  if (segment_bytes == 0) return 0;
  if (reserve_percent > 100) reserve_percent = 100;
  uint64_t usable = total_bytes * (100 - reserve_percent) / 100;
  uint64_t n = usable / segment_bytes;
  return n > 0xFFFFFFFFULL ? 0xFFFFFFFFu : static_cast<uint32_t>(n);
}

class RotatingLog {
 public:
  RotatingLog(LogStore& store, size_t segment_bytes, uint32_t max_segments)
      : store_(store), seg_bytes_(segment_bytes), max_segs_(max_segments) {}

  // Resume from existing storage: segments [oldest, newest] exist on the medium.
  // Extra old segments beyond the cap are deleted immediately.
  bool resume(uint32_t oldest, uint32_t newest) {
    oldest_ = oldest;
    cur_ = newest;
    cur_used_ = seg_bytes_;  // force a new segment on the next write
    started_ = true;
    while (liveCount() > max_segs_) {
      if (!store_.remove(oldest_)) return false;
      oldest_++;
    }
    return true;
  }

  // Append one record (a '\n' is added). Splits across segments without loss.
  bool write(const std::string& line) {
    std::string rec = line + "\n";
    size_t off = 0;
    while (off < rec.size()) {
      if (cur_used_ >= seg_bytes_) {
        if (!rotate()) return false;
      }
      size_t room = seg_bytes_ - cur_used_;
      size_t n = (rec.size() - off < room) ? rec.size() - off : room;
      if (!store_.append(cur_, rec.data() + off, n)) return false;
      cur_used_ += n;
      off += n;
    }
    return true;
  }

  uint32_t oldest() const { return oldest_; }
  uint32_t newest() const { return cur_; }
  uint32_t liveCount() const { return cur_ - oldest_ + 1; }
  uint32_t maxSegments() const { return max_segs_; }

 private:
  bool rotate() {
    // Starting a new segment: if the cap would be exceeded, drop the oldest first.
    if (started_ && liveCount() >= max_segs_) {
      if (!store_.remove(oldest_)) return false;
      oldest_++;
    }
    if (started_) cur_++;
    started_ = true;
    cur_used_ = 0;
    return true;
  }

  LogStore& store_;
  size_t seg_bytes_;
  uint32_t max_segs_;
  uint32_t oldest_ = 0;
  uint32_t cur_ = 0;
  size_t cur_used_ = 0;
  bool started_ = false;
};
