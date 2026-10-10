// Host stub for <SPIFFS.h>: the global SPIFFS object, mounted on the in-memory volume.
#pragma once

#include "FS.h"

class SpiiffsFs : public fs::FS {
 public:
  SpiiffsFs() : fs::FS(&stubfs::spiffs()) {}
  bool begin(bool format_on_fail = false) {
    started_ = true;
    stubfs::Volume& v = stubfs::spiffs();
    if (!v.present && format_on_fail) {   // core reformats when the image is unusable
      v.present = true;
      v.files.clear();
      v.dirs.clear();
    }
    return v.present;
  }
  void end() { started_ = false; }
  bool format() {
    stubfs::Volume& v = stubfs::spiffs();
    v.files.clear();
    v.dirs.clear();
    v.present = true;
    return true;
  }
  bool started() const { return started_; }

 private:
  bool started_ = false;
};

extern SpiiffsFs SPIFFS;
