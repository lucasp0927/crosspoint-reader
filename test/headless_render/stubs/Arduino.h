#pragma once

// Host stub for Arduino.h — the Arduino/FreeRTOS surface the Epub engine,
// GfxRenderer, and font code reach for. Pulled in directly by CssParser.cpp
// and transitively by the Logging.h stub (mirroring how the device gets it).

#include <cassert>  // device gets assert transitively through Arduino.h
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>

#include "Print.h"
#include "WString.h"

// --- attributes / flash placement (all no-ops on host) ---
#define PROGMEM
#define IRAM_ATTR
#define DRAM_ATTR
#define F(x) (x)
#define PSTR(x) (x)

inline uint8_t pgm_read_byte(const void* p) { return *reinterpret_cast<const uint8_t*>(p); }
inline uint16_t pgm_read_word(const void* p) {
  uint16_t v;
  memcpy(&v, p, sizeof(v));
  return v;
}
inline uint32_t pgm_read_dword(const void* p) {
  uint32_t v;
  memcpy(&v, p, sizeof(v));
  return v;
}
#define memcpy_P memcpy

// --- timing ---
inline uint32_t millis() {
  using namespace std::chrono;
  static const auto t0 = steady_clock::now();
  return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - t0).count());
}
inline uint64_t micros() {
  using namespace std::chrono;
  static const auto t0 = steady_clock::now();
  return static_cast<uint64_t>(duration_cast<microseconds>(steady_clock::now() - t0).count());
}
inline void delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
inline void delayMicroseconds(uint32_t us) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }
inline void yield() {}

// --- FreeRTOS shims ---
typedef uint32_t TickType_t;
#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (ms)
#endif
inline void vTaskDelay(TickType_t) {}
inline unsigned uxTaskGetStackHighWaterMark(void*) { return 0xFFFF; }

// --- ESP class ---
// Free heap is scripted large so the SD-font 40KB retention floor and any
// build-time heap guards never fire on host; heap pressure is a device
// question, not a layout question.
struct EspClass {
  uint32_t getFreeHeap() const { return 64 * 1024 * 1024; }
  uint32_t getMaxAllocHeap() const { return 64 * 1024 * 1024; }
  uint32_t getMinFreeHeap() const { return 64 * 1024 * 1024; }
  void restart() {
    fprintf(stderr, "[host] ESP.restart() called — aborting\n");
    abort();
  }
};
inline EspClass ESP;
