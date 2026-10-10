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
    have_cur_ = true;
    cur_used_ = seg_bytes_;  // force a new segment on the next write
    return trim();
  }

  // Append one record (a '\n' is added). Splits across segments without loss.
  bool write(const std::string& line) {
    std::string rec = line + "\n";
    size_t off = 0;
    while (off < rec.size()) {
      if (!have_cur_ || cur_used_ >= seg_bytes_) {
        if (!advance()) return false;
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
  uint32_t liveCount() const { return have_cur_ ? (cur_ - oldest_ + 1) : 0; }
  uint32_t maxSegments() const { return max_segs_; }

 private:
  // Open the next segment, making room first. `have_cur_` distinguishes "no segment has
  // been written yet" from "segment 0 is open"; without it the very first advance() would
  // keep writing into segment 0 and quietly double its size (and the total cap).
  bool advance() {
    if (!have_cur_) {
      oldest_ = cur_;
      have_cur_ = true;
      cur_used_ = 0;
      return true;
    }
    while (liveCount() + 1 > max_segs_) {
      if (!store_.remove(oldest_)) return false;
      oldest_++;
    }
    cur_++;
    cur_used_ = 0;
    return true;
  }

  bool trim() {
    while (liveCount() > max_segs_) {
      if (!store_.remove(oldest_)) return false;
      oldest_++;
    }
    return true;
  }

  LogStore& store_;
  size_t seg_bytes_;
  uint32_t max_segs_;
  uint32_t oldest_ = 0;
  uint32_t cur_ = 0;
  size_t cur_used_ = 0;
  bool have_cur_ = false;
};
