// Host stub of Arduino's Print base class (the part the OLED code paths rely on).
#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "WString.h"

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t b) = 0;
  virtual size_t write(const uint8_t* buffer, size_t size) {
    size_t n = 0;
    while (size--) n += write(*buffer++);
    return n;
  }
  size_t write(const char* str) { return str ? write(reinterpret_cast<const uint8_t*>(str), strlen(str)) : 0; }

 protected:
  size_t writeChars(const char* str, int len) {
    if (len <= 0) return 0;
    return write(reinterpret_cast<const uint8_t*>(str), static_cast<size_t>(len));
  }

 public:

  size_t print(const String& s) { return write(reinterpret_cast<const uint8_t*>(s.c_str()), s.length()); }
  size_t print(const char* s) { return write(s); }
  size_t print(char c) { return write(static_cast<uint8_t>(c)); }
  size_t print(int n) { char b[16]; return writeChars(b, std::snprintf(b, sizeof(b), "%d", n)); }
  size_t print(unsigned int n) { char b[16]; return writeChars(b, std::snprintf(b, sizeof(b), "%u", n)); }
  size_t print(long n) { char b[24]; return writeChars(b, std::snprintf(b, sizeof(b), "%ld", n)); }
  size_t print(unsigned long n) { char b[24]; return writeChars(b, std::snprintf(b, sizeof(b), "%lu", n)); }
  size_t print(float n, int decimals = 2) {
    char b[32];
    return writeChars(b, std::snprintf(b, sizeof(b), "%.*f", decimals, static_cast<double>(n)));
  }

  size_t println() { return write(reinterpret_cast<const uint8_t*>("\r\n"), 2); }
  template <typename T> size_t println(T v) { size_t n = print(v); return n + println(); }
  size_t println(const char* s) { size_t n = print(s); return n + println(); }

  size_t printf(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    const int k = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return writeChars(buf, k);
  }
};
