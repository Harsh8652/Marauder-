// Host stub: the slice of Arduino's WString that the OLED code paths use.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

class String {
 public:
  String() = default;
  String(const char* cstr) : s_(cstr ? cstr : "") {}
  String(const std::string& s) : s_(s) {}
  String(const String&) = default;
  String(String&&) noexcept = default;
  String(char c) : s_(1, c) {}
  String(int v) : s_(std::to_string(v)) {}
  String(unsigned v) : s_(std::to_string(v)) {}
  String(long v) : s_(std::to_string(v)) {}
  String(unsigned long v) : s_(std::to_string(v)) {}
  String(long long v) : s_(std::to_string(v)) {}
  String(unsigned long long v) : s_(std::to_string(v)) {}
  String(float v, uint8_t decimals = 2) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", static_cast<int>(decimals), static_cast<double>(v));
    s_ = buf;
  }
  String(double v, uint8_t decimals = 2) : String(static_cast<float>(v), decimals) {}

  String& operator=(const String&) = default;
  String& operator=(const char* cstr) { s_ = cstr ? cstr : ""; return *this; }

  String operator+(const String& rhs) const { return String(s_ + rhs.s_); }
  String operator+(const char* rhs) const { return String(s_ + (rhs ? rhs : "")); }
  String& operator+=(const String& rhs) { s_ += rhs.s_; return *this; }
  String& operator+=(const char* rhs) { s_ += (rhs ? rhs : ""); return *this; }
  String& operator+=(char c) { s_ += c; return *this; }
  String& operator+=(int v) { s_ += std::to_string(v); return *this; }
  String& operator+=(unsigned long v) { s_ += std::to_string(v); return *this; }

  bool operator==(const String& rhs) const { return s_ == rhs.s_; }
  bool operator!=(const String& rhs) const { return s_ != rhs.s_; }
  bool operator==(const char* rhs) const { return s_ == (rhs ? rhs : ""); }
  bool operator!=(const char* rhs) const { return !(*this == rhs); }
  bool operator<(const String& rhs) const { return s_ < rhs.s_; }

  char operator[](unsigned i) const { return i < s_.size() ? s_[i] : '\0'; }
  char charAt(unsigned i) const { return (*this)[i]; }
  char operator[](int i) const { return (*this)[static_cast<unsigned>(i)]; }

  unsigned int length() const { return static_cast<unsigned int>(s_.size()); }
  const char* c_str() const { return s_.c_str(); }

  int indexOf(char c, unsigned from = 0) const {
    const size_t p = s_.find(c, from);
    return p == std::string::npos ? -1 : static_cast<int>(p);
  }
  int indexOf(const String& s, unsigned from = 0) const {
    const size_t p = s_.find(s.s_, from);
    return p == std::string::npos ? -1 : static_cast<int>(p);
  }
  int lastIndexOf(char c) const {
    const size_t p = s_.rfind(c);
    return p == std::string::npos ? -1 : static_cast<int>(p);
  }
  bool startsWith(const String& p) const { return s_.rfind(p.s_, 0) == 0; }
  bool endsWith(const String& p) const {
    return p.s_.size() <= s_.size() && s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0;
  }
  String substring(unsigned from) const { return String(s_.substr(std::min<size_t>(from, s_.size()))); }
  String substring(unsigned from, unsigned to) const {
    if (from > s_.size() || to < from) return String("");
    return String(s_.substr(from, to - from));
  }
  void trim() {
    const size_t b = s_.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) { s_.clear(); return; }
    const size_t e = s_.find_last_not_of(" \t\r\n");
    s_ = s_.substr(b, e - b + 1);
  }
  void toLowerCase() { std::transform(s_.begin(), s_.end(), s_.begin(), ::tolower); }
  void toUpperCase() { std::transform(s_.begin(), s_.end(), s_.begin(), ::toupper); }
  int toInt() const { return std::atoi(s_.c_str()); }
  long toLong() const { return std::atol(s_.c_str()); }
  float toFloat() const { return std::strtof(s_.c_str(), nullptr); }
  bool equals(const String& rhs) const { return s_ == rhs.s_; }
  bool equalsIgnoreCase(const String& rhs) const {
    if (s_.size() != rhs.s_.size()) return false;
    for (size_t i = 0; i < s_.size(); ++i)
      if (std::tolower((unsigned char)s_[i]) != std::tolower((unsigned char)rhs.s_[i])) return false;
    return true;
  }
  void reserve(unsigned n) { s_.reserve(n); }
  void getBytes(uint8_t* buf, unsigned n) const {
    const size_t len = std::min<size_t>(n - 1, s_.size());
    std::memcpy(buf, s_.data(), len);
    buf[len] = 0;
  }

 private:
  std::string s_;
};

inline String operator+(const char* lhs, const String& rhs) { return String(lhs) + rhs; }
