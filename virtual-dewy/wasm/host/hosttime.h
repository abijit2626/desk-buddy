// Force-included before every other header (see wasm/build.mjs).
//
// WASI has no time zones and no settimeofday(), and the browser decides what "now" is, so the firmware's calls to
// time(), localtime_r(), mktime(), settimeofday() and tzset() are routed to a virtual clock:
//   wall clock = host_epoch_base_ms + host_millis,  shifted by the POSIX TZ string the firmware sets ("IST-5:30").
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

// set by the host every tick
inline uint32_t host_millis = 0;
// epoch milliseconds that correspond to host_millis == 0
inline int64_t host_epoch_base_ms = 0;

// seconds EAST of UTC, worked out from the TZ the firmware set. POSIX counts hours WEST, so "IST-5:30" is +5:30.
inline int host_tz_offset_sec = 0;
inline void host_parse_tz(const char* tz) {
  host_tz_offset_sec = 0;
  if (!tz) return;
  const char* p = tz;
  if (*p == '<') { while (*p && *p != '>') p++; if (*p) p++; }
  else while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) p++;
  int sign = 1;
  if (*p == '-') { sign = -1; p++; } else if (*p == '+') p++;
  if (*p < '0' || *p > '9') return;
  int h = atoi(p), m = 0;
  while (*p >= '0' && *p <= '9') p++;
  if (*p == ':') m = atoi(p + 1);
  host_tz_offset_sec = -sign * (h * 3600 + m * 60);
}
inline void tzset() { host_parse_tz(getenv("TZ")); }

inline int settimeofday(const struct timeval* tv, const void*) {
  host_epoch_base_ms = int64_t(tv->tv_sec) * 1000 + tv->tv_usec / 1000 - int64_t(host_millis);
  return 0;
}
inline time_t host_time(time_t* out) {
  time_t t = time_t((host_epoch_base_ms + int64_t(host_millis)) / 1000);
  if (out) *out = t;
  return t;
}
inline struct tm* host_localtime_r(const time_t* t, struct tm* out) {
  time_t shifted = *t + host_tz_offset_sec;
  return gmtime_r(&shifted, out);
}
inline time_t host_mktime(struct tm* tmv) {
  time_t t = timegm(tmv) - host_tz_offset_sec;
  host_localtime_r(&t, tmv);   // normalise the fields, like mktime does
  return t;
}
#define time(x) host_time(x)
#define localtime_r(t, o) host_localtime_r(t, o)
#define mktime(x) host_mktime(x)
