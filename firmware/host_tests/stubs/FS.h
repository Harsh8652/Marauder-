// Host stub of ESP32's FS layer (fs::FS / File) backed by an in-memory volume, so the
// log code in LogStores.cpp can actually be executed on a PC: rotation, resume after a
// reboot, split records, directory scans, write failures.
//
// Semantics copied from the ESP32 core:
//   - File::name() returns the FULL path (core >= 2.0), switchable to a bare name so the
//     code is proven against both.
//   - open() on a directory returns a handle whose openNextFile() yields the children.
//   - opening with FILE_APPEND creates the file if it does not exist; "w" truncates.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Print.h"

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

namespace stubfs {

struct Volume {
  std::map<std::string, std::vector<uint8_t>> files;  // full path -> bytes
  std::set<std::string> dirs;                         // explicit directories
  uint64_t capacity = 0;                              // bytes, feeds SD.totalBytes()
  bool present = true;                                // false = no card / mount failed
  bool fail_writes = false;                           // simulate a full or dying card
  bool bare_names = false;                            // File::name() -> "00000000.log"
};

Volume& spiffs();
Volume& sd();
void resetAll();

inline uint64_t usedBytes(const Volume& v) {
  uint64_t n = 0;
  for (const auto& kv : v.files) n += kv.second.size();
  return n;
}

}  // namespace stubfs

namespace fs {

struct FileImpl {
  stubfs::Volume* vol = nullptr;
  std::string path;
  bool is_dir = false;
  bool writable = false;
  bool open_ok = false;
  size_t pos = 0;
  std::vector<std::string> children;   // for directory handles
  size_t child = 0;
};

class FS;

class File : public Print {
 public:
  File() = default;
  explicit File(std::shared_ptr<FileImpl> impl) : impl_(std::move(impl)) {}

  explicit operator bool() const { return impl_ && impl_->open_ok; }
  bool operator!() const { return !(impl_ && impl_->open_ok); }

  const char* name() const {
    if (!impl_) return "";
    if (impl_->vol->bare_names) {
      const size_t slash = impl_->path.find_last_of('/');
      bare_cache_ = (slash == std::string::npos) ? impl_->path : impl_->path.substr(slash + 1);
      return bare_cache_.c_str();
    }
    return impl_->path.c_str();
  }
  bool isDirectory() const { return impl_ && impl_->is_dir; }
  size_t size() const {
    if (!impl_ || impl_->is_dir) return 0;
    return impl_->vol->files[impl_->path].size();
  }
  size_t available() const {
    if (!impl_ || impl_->is_dir) return 0;
    const std::vector<uint8_t>& data = impl_->vol->files[impl_->path];
    return data.size() - std::min(impl_->pos, data.size());
  }
  int read() {
    if (!impl_ || impl_->is_dir) return -1;
    std::vector<uint8_t>& data = impl_->vol->files[impl_->path];
    if (impl_->pos >= data.size()) return -1;
    return data[impl_->pos++];
  }
  size_t read(uint8_t* buf, size_t n) {
    size_t i = 0;
    for (; i < n; ++i) {
      const int c = read();
      if (c < 0) break;
      buf[i] = static_cast<uint8_t>(c);
    }
    return i;
  }
  std::string readAll() {
    std::string out;
    int c;
    while ((c = read()) >= 0) out.push_back(static_cast<char>(c));
    return out;
  }

  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* data, size_t len) override {
    if (!impl_ || !impl_->open_ok || impl_->is_dir || !impl_->writable) return 0;
    if (impl_->vol->fail_writes) return 0;
    std::vector<uint8_t>& out = impl_->vol->files[impl_->path];
    out.insert(out.end(), data, data + len);
    impl_->pos = out.size();
    return len;
  }
  void close() { impl_.reset(); }

  File openNextFile() {
    File out;
    if (!impl_ || !impl_->is_dir || impl_->child >= impl_->children.size()) return out;
    auto next = std::make_shared<FileImpl>();
    next->vol = impl_->vol;
    next->path = impl_->children[impl_->child++];
    next->open_ok = true;
    next->is_dir = isDirectoryPath(next->vol, next->path);
    out = File(next);
    return out;
  }
  void rewindDirectory() {
    if (impl_) impl_->child = 0;
  }

 private:
  static bool isDirectoryPath(stubfs::Volume* v, const std::string& p) {
    if (v->dirs.count(p) == 0) return false;
    const std::string prefix = p + "/";
    for (const auto& kv : v->files)
      if (kv.first.size() > prefix.size() && kv.first.compare(0, prefix.size(), prefix) == 0) return true;
    return false;
  }

  std::shared_ptr<FileImpl> impl_;
  mutable std::string bare_cache_;

  friend class FS;
};

class FS {
 public:
  explicit FS(stubfs::Volume* vol) : vol_(vol) {}
  virtual ~FS() = default;

  bool exists(const char* path) const {
    if (!path) return false;
    const std::string p(path);
    return vol_->files.count(p) > 0 || vol_->dirs.count(p) > 0;
  }
  bool exists(const String& path) const { return exists(path.c_str()); }

  File open(const char* path, const char* mode = FILE_READ) {
    File out;
    if (!vol_->present) return out;
    const std::string p(path);
    const bool append = mode && (strchr(mode, 'a') || strchr(mode, '+'));
    const bool write_mode = mode && (strchr(mode, 'w') || append);
    const bool is_dir = vol_->dirs.count(p) > 0;
    if (is_dir && write_mode) return out;
    if (!is_dir && !write_mode && vol_->files.count(p) == 0) return out;
    auto impl = std::make_shared<FileImpl>();
    impl->vol = vol_;
    impl->path = p;
    impl->is_dir = is_dir;
    impl->writable = write_mode;
    impl->open_ok = true;
    if (!append) vol_->files[p].clear();          // "w" truncates, "a" keeps
    impl->pos = vol_->files[p].size();
    if (is_dir) collectChildren(p, impl->children);
    return File(impl);
  }
  File open(const String& path, const char* mode = FILE_READ) { return open(path.c_str(), mode); }

  bool mkdir(const char* path, bool = true) {
    if (!vol_->present) return false;
    vol_->dirs.insert(path);
    return true;
  }
  bool remove(const char* path) {
    if (!vol_->present) return false;
    const size_t n = vol_->files.erase(path);
    vol_->dirs.erase(path);
    return n > 0;
  }
  bool rmdir(const char* path) { return vol_->dirs.erase(path) > 0; }
  bool rename(const char* from, const char* to) {
    const auto it = vol_->files.find(from);
    if (it == vol_->files.end()) return false;
    vol_->files[to] = it->second;
    vol_->files.erase(it);
    return true;
  }
  uint64_t totalBytes() const { return vol_->capacity; }
  uint64_t usedBytes() const { return stubfs::usedBytes(*vol_); }

  stubfs::Volume* volume() const { return vol_; }

 private:
  void collectChildren(const std::string& dir, std::vector<std::string>& out) const {
    const std::string prefix = dir == "/" ? "/" : dir + "/";
    std::set<std::string> subdirs;
    for (const auto& kv : vol_->files) {
      if (kv.first.size() <= prefix.size() || kv.first.compare(0, prefix.size(), prefix) != 0) continue;
      const std::string rest = kv.first.substr(prefix.size());
      if (rest.find('/') == std::string::npos) out.push_back(kv.first);
      else subdirs.insert(prefix + rest.substr(0, rest.find('/')));
    }
    for (const auto& s : vol_->dirs) {
      if (s.size() <= prefix.size() || s.compare(0, prefix.size(), prefix) != 0) continue;
      if (s.substr(prefix.size()).find('/') == std::string::npos) subdirs.insert(s);
    }
    out.insert(out.end(), subdirs.begin(), subdirs.end());
  }

  stubfs::Volume* vol_;
};

}  // namespace fs

using fs::File;
using fs::FS;
