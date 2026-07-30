#pragma once

// Host stub for Arduino's WString.h. Minimal String backed by std::string —
// only the surface the Epub engine + FsHelpers actually touch.

#include <cstring>
#include <string>

class String {
 public:
  String() = default;
  String(const char* s) : s_(s ? s : "") {}  // NOLINT: implicit like Arduino's
  // explicit: Arduino String has no std::string ctor; an implicit one makes
  // hasJpgExtension(std::string) ambiguous vs the string_view overload.
  explicit String(const std::string& s) : s_(s) {}
  explicit String(int v) : s_(std::to_string(v)) {}

  const char* c_str() const { return s_.c_str(); }
  unsigned int length() const { return static_cast<unsigned int>(s_.size()); }
  bool isEmpty() const { return s_.empty(); }

  String& operator+=(const char* rhs) {
    s_ += rhs ? rhs : "";
    return *this;
  }
  String& operator+=(const String& rhs) {
    s_ += rhs.s_;
    return *this;
  }
  String& operator+=(char c) {
    s_ += c;
    return *this;
  }
  friend String operator+(String lhs, const String& rhs) {
    lhs += rhs;
    return lhs;
  }
  bool operator==(const String& rhs) const { return s_ == rhs.s_; }
  bool operator==(const char* rhs) const { return s_ == (rhs ? rhs : ""); }
  char operator[](unsigned int i) const { return s_[i]; }

  const std::string& str() const { return s_; }

 private:
  std::string s_;
};
