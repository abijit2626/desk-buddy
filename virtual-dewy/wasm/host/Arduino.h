// Stand-in for the Arduino core + a few ESP32 bits, for running Dewy's real firmware inside WebAssembly.
// Based on firmware/test/mock/Arduino.h (which the repo's own PC tests use); changed so that time comes from the
// host and Serial goes to and from the browser.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <deque>
#include <cstdarg>
#include <ctime>
#include <algorithm>

// ---- time and randomness (host_millis comes from hosttime.h)
inline uint32_t millis() { return host_millis; }
inline void delay(uint32_t) {}
inline uint32_t host_rng = 1;
inline uint32_t esp_random() { host_rng ^= host_rng << 13; host_rng ^= host_rng >> 17; host_rng ^= host_rng << 5; return host_rng; }

// ---- String: the subset of Arduino's String that the firmware uses
struct String {
  std::string s; String() {} String(const char* c) : s(c ? c : "") {} String(const std::string& x) : s(x) {}
  void trim() { size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n"); s = a == std::string::npos ? "" : s.substr(a, b - a + 1); }
  void toLowerCase() { for (auto& c : s) c = char(tolower(c)); }
  unsigned length() const { return unsigned(s.size()); }
  const char* c_str() const { return s.c_str(); }
  bool startsWith(const char* p) const { return s.rfind(p, 0) == 0; }
  String substring(int a, int b) const { String r; if (a < 0) a = 0; if (b > int(s.size())) b = int(s.size()); if (b > a) r.s = s.substr(a, b - a); return r; }
  long toInt() const { return atol(s.c_str()); }
  float toFloat() const { return float(atof(s.c_str())); }
  bool operator==(const char* o) const { return s == o; }
  String& operator+=(char c) { s += c; return *this; }
  String& operator=(const char* c) { s = c; return *this; }
  String& operator+=(const char* c) { s += c; return *this; }
  String& operator+=(const String& o) { s += o.s; return *this; }
  bool operator==(const String& o) const { return s == o.s; }
  friend String operator+(const String& a, const String& b) { return String(a.s + b.s); }
  friend String operator+(const String& a, const char* b) { return String(a.s + b); }
  friend String operator+(const char* a, const String& b) { return String(std::string(a) + b.s); }
};
#ifndef constrain
#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#endif
using std::min; using std::max;

// ---- ESP, FreeRTOS pieces (the net task is stepped by the host instead, see host.cpp)
inline int host_restart_requested = 0;
struct EspClass { void restart() { host_restart_requested = 1; } };
inline EspClass ESP;
typedef void* SemaphoreHandle_t;
#define portMAX_DELAY 0xffffffffu
inline SemaphoreHandle_t xSemaphoreCreateMutex() { static int m; return &m; }
inline int xSemaphoreTake(SemaphoreHandle_t, uint32_t) { return 1; }
inline int xSemaphoreGive(SemaphoreHandle_t) { return 1; }
inline size_t strlcpy(char* d, const char* s, size_t n) { size_t l = strlen(s); if (n) { size_t c = l < n - 1 ? l : n - 1; memcpy(d, s, c); d[c] = 0; } return l; }
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
inline void vTaskDelay(TickType_t) {}
inline int xTaskCreatePinnedToCore(void (*)(void*), const char*, uint32_t, void*, int, void*, int) { return 1; }

// ---- Serial: text out to the browser's serial monitor, text in from it
inline std::string g_serialOut;
inline std::deque<char> g_serialIn;
inline void host_serial_write(const char* s, size_t n) {
  g_serialOut.append(s, n);
  if (g_serialOut.size() > (256u << 10)) g_serialOut.erase(0, g_serialOut.size() - (128u << 10));   // never grow without limit
}
struct SerialT {
  void begin(int) {}
  void println(const char* s) { host_serial_write(s, strlen(s)); host_serial_write("\n", 1); }
  int printf(const char* f, ...) {
    char buf[512];
    va_list a; va_start(a, f); int n = vsnprintf(buf, sizeof buf, f, a); va_end(a);
    if (n < 0) return n;
    host_serial_write(buf, size_t(n) < sizeof buf ? size_t(n) : sizeof buf - 1);
    return n;
  }
  int available() { return int(g_serialIn.size()); }
  int read() { if (g_serialIn.empty()) return -1; char c = g_serialIn.front(); g_serialIn.pop_front(); return (unsigned char)c; }
};
inline SerialT Serial;

// battery pin: the sim reports a healthy cell (BATTERY_ENABLED is 0 in config.h anyway)
inline uint32_t analogReadMilliVolts(uint8_t) { return 1900; }

// SNTP: the host sets the clock a moment after Wi-Fi connects (see host.cpp)
inline int host_sntp_pending = 0;
inline void configTzTime(const char* tz, const char*, const char* = nullptr, const char* = nullptr) {
  setenv("TZ", tz, 1); tzset(); host_sntp_pending = 1;
}
