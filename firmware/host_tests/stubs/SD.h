// Host stub for <SD.h>: a card that exists or not, with a size the test sets.
// LogStores.cpp decides the SD log cap from SD.totalBytes(), so this stub is what lets a
// host test prove the cap math for a real card size (e.g. 16 GB) without a card.
#pragma once

#include "FS.h"

typedef enum {
  CARD_NONE = 0,
  CARD_MMC = 1,
  CARD_SD = 2,
  CARD_SDHC = 3,
  CARD_UNKNOWN = 4
} sd_card_t;

class SdFs : public fs::FS {
 public:
  SdFs() : fs::FS(&stubfs::sd()) {}
  bool begin() { return mounted_ = stubfs::sd().present; }
  bool begin(int /*cs*/, int /*frequency*/ = -1) { return begin(); }
  void end() { mounted_ = false; }
  uint8_t cardType() const { return mounted_ ? static_cast<uint8_t>(card_type_) : static_cast<uint8_t>(CARD_NONE); }
  uint64_t cardSize() const { return mounted_ ? stubfs::sd().capacity : 0; }
  uint64_t totalBytes() const { return mounted_ ? stubfs::sd().capacity : 0; }
  uint64_t usedBytes() const { return mounted_ ? fs::FS::usedBytes() : 0; }

  // Test control
  void stubInsert(sd_card_t type, uint64_t capacity_bytes) {
    stubfs::sd().present = true;
    card_type_ = type;
    mounted_ = true;
    if (capacity_bytes) stubfs::sd().capacity = capacity_bytes;
  }
  void stubRemove() {
    stubfs::sd().present = false;
    mounted_ = false;
  }

 private:
  sd_card_t card_type_ = CARD_NONE;
  bool mounted_ = false;
};

extern SdFs SD;
