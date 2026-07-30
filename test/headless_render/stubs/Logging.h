#pragma once

// Host stub for lib/Logging/Logging.h. Same LOG_* macro shapes and LOG_LEVEL
// gating as the real header, printed to stderr. Includes the Arduino shim so
// every engine TU sees millis()/ESP/vTaskDelay transitively, exactly as the
// real Logging.h supplies them on device.

#include <cstdarg>
#include <cstdio>
#include <string>

#include "Arduino.h"

#ifndef LOG_LEVEL
#define LOG_LEVEL 1  // ERR + INF by default; -DLOG_LEVEL=2 for DBG
#endif

inline void logPrintf(const char* level, const char* origin, const char* format, ...) {
  va_list args;
  va_start(args, format);
  fprintf(stderr, "[%s][%s] ", level, origin);
  vfprintf(stderr, format, args);
  va_end(args);
}

#if LOG_LEVEL >= 0
#define LOG_ERR(origin, format, ...) logPrintf("ERR", origin, format "\n", ##__VA_ARGS__)
#else
#define LOG_ERR(origin, format, ...)
#endif

#if LOG_LEVEL >= 1
#define LOG_INF(origin, format, ...) logPrintf("INF", origin, format "\n", ##__VA_ARGS__)
#else
#define LOG_INF(origin, format, ...)
#endif

#if LOG_LEVEL >= 2
#define LOG_DBG(origin, format, ...) logPrintf("DBG", origin, format "\n", ##__VA_ARGS__)
#else
#define LOG_DBG(origin, format, ...)
#endif

inline std::string getLastLogs() { return {}; }
inline void clearLastLogs() {}
inline bool sanitizeLogHead() { return false; }
