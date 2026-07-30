#pragma once

// Host-test stub for lib/hal/HalStorage.h, backed by real files on the host
// filesystem instead of SdFat, plus the two Arduino globals SdCardFont.cpp
// reaches for through it (ESP.getFreeHeap and millis).
//
// Every SD operation is counted. That is the point of this stub: the cost of
// serving a glyph from an SD font is not a timing question, it is a question of
// how many file opens/seeks/reads it takes, and that IS deterministic and
// therefore assertable on the host.
//
// Only the surface SdCardFont.cpp actually uses is implemented:
//   Storage.openFileForRead(module, path, HalFile&)
//   file.seekSet(off) / file.read(buf, len) / file.close() / file.fileSize()

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace sdstub {

struct Counters {
  int opens = 0;
  int seeks = 0;
  int reads = 0;
  size_t bytesRead = 0;

  void reset() { *this = Counters{}; }
};

inline Counters& counters() {
  static Counters c;
  return c;
}

// Scripted free-heap value so tests can drive the retention branch in
// SdCardFont::resetStyleMiniData (which frees the mini cache below 40KB).
inline uint32_t& freeHeap() {
  static uint32_t h = 200 * 1024;
  return h;
}

}  // namespace sdstub

class HalFile {
 public:
  HalFile() = default;
  ~HalFile() { close(); }
  HalFile(HalFile&& o) noexcept : fp_(o.fp_) { o.fp_ = nullptr; }
  HalFile& operator=(HalFile&& o) noexcept {
    if (this != &o) {
      close();
      fp_ = o.fp_;
      o.fp_ = nullptr;
    }
    return *this;
  }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  bool openPath(const char* path) {
    close();
    fp_ = std::fopen(path, "rb");
    return fp_ != nullptr;
  }

  bool seekSet(size_t off) {
    if (!fp_) return false;
    sdstub::counters().seeks++;
    return std::fseek(fp_, static_cast<long>(off), SEEK_SET) == 0;
  }

  int read(void* buf, size_t count) {
    if (!fp_) return -1;
    sdstub::counters().reads++;
    const size_t n = std::fread(buf, 1, count, fp_);
    sdstub::counters().bytesRead += n;
    return static_cast<int>(n);
  }

  size_t fileSize() {
    if (!fp_) return 0;
    const long cur = std::ftell(fp_);
    std::fseek(fp_, 0, SEEK_END);
    const long end = std::ftell(fp_);
    std::fseek(fp_, cur, SEEK_SET);
    return static_cast<size_t>(end);
  }

  bool close() {
    if (!fp_) return false;
    std::fclose(fp_);
    fp_ = nullptr;
    return true;
  }

  bool isOpen() const { return fp_ != nullptr; }
  explicit operator bool() const { return fp_ != nullptr; }

 private:
  std::FILE* fp_ = nullptr;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage s;
    return s;
  }

  bool openFileForRead(const char* /*module*/, const char* path, HalFile& file) {
    sdstub::counters().opens++;
    return file.openPath(path);
  }
  bool openFileForRead(const char* m, const std::string& path, HalFile& file) {
    return openFileForRead(m, path.c_str(), file);
  }
};

#define Storage HalStorage::getInstance()

// --- Arduino globals SdCardFont.cpp uses ---
struct EspStub {
  uint32_t getFreeHeap() const { return sdstub::freeHeap(); }
  uint32_t getMaxAllocHeap() const { return sdstub::freeHeap(); }
};
inline EspStub ESP;

inline uint32_t millis() { return 0; }
