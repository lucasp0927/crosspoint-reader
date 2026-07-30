#pragma once

// Host stub for lib/hal/HalStorage.h — full read/write surface over stdio,
// with every device path remapped into a sandbox root so "/.crosspoint/..."
// and "/.fonts/..." land under HOST_SD_ROOT (default ./sd_root) instead of
// the real filesystem root. Storage.remove()/removeDir() can therefore never
// touch anything outside the sandbox.
//
// Semantics mirrored from the device build:
//  - DESTRUCTOR_CLOSES_FILE: HalFile closes in its destructor.
//  - Writes are flushed eagerly so a second read handle on the same path
//    (Section::loadPageDuringBuild reads the half-written .part) sees them.
//  - openFileForWrite creates parent directories; on device the callers
//    mkdir first, so this only forgives ordering, it does not change layout.

#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "Print.h"
#include "WString.h"

namespace halstub {

inline const std::string& sdRoot() {
  static const std::string root = [] {
    const char* env = std::getenv("HOST_SD_ROOT");
    std::string r = (env && *env) ? env : "./sd_root";
    std::error_code ec;
    std::filesystem::create_directories(r, ec);
    return r;
  }();
  return root;
}

inline std::string mapPath(const char* devicePath) {
  std::string p = devicePath ? devicePath : "";
  if (!p.empty() && p[0] == '/') return sdRoot() + p;
  return sdRoot() + "/" + p;
}

}  // namespace halstub

class HalFile : public Print {
 public:
  HalFile() = default;
  ~HalFile() override { close(); }
  HalFile(HalFile&& o) noexcept : fp_(o.fp_), path_(std::move(o.path_)) { o.fp_ = nullptr; }
  HalFile& operator=(HalFile&& o) noexcept {
    if (this != &o) {
      close();
      fp_ = o.fp_;
      path_ = std::move(o.path_);
      o.fp_ = nullptr;
    }
    return *this;
  }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  bool openHostPath(const char* hostPath, const char* mode) {
    close();
    fp_ = std::fopen(hostPath, mode);
    if (fp_) path_ = hostPath;
    return fp_ != nullptr;
  }

  void flush() override {
    if (fp_) std::fflush(fp_);
  }

  size_t getName(char* name, size_t len) {
    if (!name || len == 0) return 0;
    const auto base = std::filesystem::path(path_).filename().string();
    std::snprintf(name, len, "%s", base.c_str());
    return std::strlen(name);
  }

  size_t size() { return fileSize(); }
  size_t fileSize() {
    if (!fp_) return 0;
    const long cur = std::ftell(fp_);
    std::fseek(fp_, 0, SEEK_END);
    const long end = std::ftell(fp_);
    std::fseek(fp_, cur, SEEK_SET);
    return end < 0 ? 0 : static_cast<size_t>(end);
  }
  uint64_t fileSize64() { return fileSize(); }

  bool seek(size_t pos) { return fp_ && std::fseek(fp_, static_cast<long>(pos), SEEK_SET) == 0; }
  bool seek64(uint64_t pos) { return seek(static_cast<size_t>(pos)); }
  bool seekSet(size_t pos) { return seek(pos); }
  bool seekCur(int64_t offset) { return fp_ && std::fseek(fp_, static_cast<long>(offset), SEEK_CUR) == 0; }

  int available() const {
    if (!fp_) return 0;
    HalFile* self = const_cast<HalFile*>(this);
    const size_t sz = self->fileSize();
    const size_t pos = self->position();
    return pos < sz ? static_cast<int>(sz - pos) : 0;
  }
  size_t position() const {
    if (!fp_) return 0;
    const long pos = std::ftell(fp_);
    return pos < 0 ? 0 : static_cast<size_t>(pos);
  }

  int read(void* buf, size_t count) {
    if (!fp_) return -1;
    return static_cast<int>(std::fread(buf, 1, count, fp_));
  }
  int read() {
    if (!fp_) return -1;
    return std::fgetc(fp_);
  }

  size_t write(const void* buf, size_t count) {
    if (!fp_) return 0;
    const size_t n = std::fwrite(buf, 1, count, fp_);
    std::fflush(fp_);  // keep concurrent read handles coherent
    return n;
  }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* buffer, size_t sz) override { return write(static_cast<const void*>(buffer), sz); }

  bool isDirectory() const { return isDir_; }

  bool close() {
    isDir_ = false;
    dirEntries_.clear();
    dirCursor_ = 0;
    if (!fp_) return false;
    std::fclose(fp_);
    fp_ = nullptr;
    return true;
  }

  bool isOpen() const { return fp_ != nullptr || isDir_; }
  operator bool() const { return fp_ != nullptr || isDir_; }

  // Directory iteration (SdCardFontRegistry::discover). Snapshot the listing
  // at open so iteration order is stable.
  bool openHostDir(const char* hostPath) {
    close();
    std::error_code ec;
    if (!std::filesystem::is_directory(hostPath, ec)) return false;
    path_ = hostPath;
    isDir_ = true;
    for (const auto& e : std::filesystem::directory_iterator(hostPath, ec)) {
      dirEntries_.push_back(e.path().string());
    }
    return true;
  }
  void rewindDirectory() { dirCursor_ = 0; }
  HalFile openNextFile() {
    HalFile f;
    while (isDir_ && dirCursor_ < dirEntries_.size()) {
      const std::string& p = dirEntries_[dirCursor_++];
      if (std::filesystem::is_directory(p)) {
        f.openHostDir(p.c_str());
        return f;
      }
      if (f.openHostPath(p.c_str(), "rb")) return f;
    }
    return f;
  }

 private:
  std::FILE* fp_ = nullptr;
  bool isDir_ = false;
  std::string path_;
  std::vector<std::string> dirEntries_;
  size_t dirCursor_ = 0;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage s;
    return s;
  }

  bool begin() { return true; }
  bool ready() const { return true; }

  // Directory-capable open (SdCardFontRegistry::discover walks font dirs).
  HalFile open(const char* path, int /*oflag*/ = 0) {
    HalFile f;
    const std::string host = halstub::mapPath(path);
    std::error_code ec;
    if (std::filesystem::is_directory(host, ec)) {
      f.openHostDir(host.c_str());
    } else {
      f.openHostPath(host.c_str(), "rb");
    }
    return f;
  }

  bool openFileForRead(const char* /*module*/, const char* path, HalFile& file) {
    return file.openHostPath(halstub::mapPath(path).c_str(), "rb");
  }
  bool openFileForRead(const char* m, const std::string& path, HalFile& file) {
    return openFileForRead(m, path.c_str(), file);
  }
  bool openFileForRead(const char* m, const String& path, HalFile& file) {
    return openFileForRead(m, path.c_str(), file);
  }

  bool openFileForWrite(const char* /*module*/, const char* path, HalFile& file) {
    const std::string host = halstub::mapPath(path);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(host).parent_path(), ec);
    return file.openHostPath(host.c_str(), "wb+");
  }
  bool openFileForWrite(const char* m, const std::string& path, HalFile& file) {
    return openFileForWrite(m, path.c_str(), file);
  }
  bool openFileForWrite(const char* m, const String& path, HalFile& file) {
    return openFileForWrite(m, path.c_str(), file);
  }

  bool mkdir(const char* path, bool /*pFlag*/ = true) {
    std::error_code ec;
    std::filesystem::create_directories(halstub::mapPath(path), ec);
    return !ec;
  }
  bool ensureDirectoryExists(const char* path) { return mkdir(path); }

  bool exists(const char* path) {
    std::error_code ec;
    return std::filesystem::exists(halstub::mapPath(path), ec);
  }
  bool remove(const char* path) {
    std::error_code ec;
    return std::filesystem::remove(halstub::mapPath(path), ec);
  }
  bool rename(const char* oldPath, const char* newPath) {
    std::error_code ec;
    std::filesystem::rename(halstub::mapPath(oldPath), halstub::mapPath(newPath), ec);
    return !ec;
  }
  bool rmdir(const char* path) { return remove(path); }
  bool removeDir(const char* path) {
    std::error_code ec;
    std::filesystem::remove_all(halstub::mapPath(path), ec);
    return !ec;
  }

  String readFile(const char* path) {
    HalFile f;
    if (!openFileForRead("STUB", path, f)) return String();
    std::string content(f.fileSize(), '\0');
    f.read(content.data(), content.size());
    return String(content);
  }
};

#define Storage HalStorage::getInstance()
