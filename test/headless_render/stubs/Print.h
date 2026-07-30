#pragma once

// Host stub for Arduino's Print base class. HalFile derives from this, and
// Epub::readItemContentsToStream() streams chapter HTML through a Print&.

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

class Print {
 public:
  virtual ~Print() = default;

  virtual size_t write(uint8_t b) = 0;
  virtual size_t write(const uint8_t* buffer, size_t size) {
    size_t n = 0;
    while (n < size) {
      if (write(buffer[n]) != 1) break;
      n++;
    }
    return n;
  }
  size_t write(const char* str) {
    if (!str) return 0;
    return write(reinterpret_cast<const uint8_t*>(str), strlen(str));
  }

  size_t print(const char* s) { return write(s); }
  size_t print(char c) { return write(static_cast<uint8_t>(c)); }
  size_t print(int v) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", v);
    return write(buf);
  }
  size_t println(const char* s) { return write(s) + write("\n"); }
  size_t println() { return write("\n"); }

  size_t printf(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n <= 0) return 0;
    return write(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(n) < sizeof(buf) ? n : sizeof(buf) - 1);
  }

  virtual void flush() {}
};
