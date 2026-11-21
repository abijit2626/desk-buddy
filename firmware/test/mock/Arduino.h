#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cstdarg>
#include <ctime>
inline uint32_t millis() { static uint32_t t = 0; return t += 7; }
inline uint32_t micros() { static uint32_t t = 0; return t += 7000; }
inline int g_delayCalls = 0;   // tests count how often the code really sleeps
inline void delay(uint32_t) { g_delayCalls++; }
inline uint32_t esp_random() { return 1; }
struct String {
  std::string s; String() {} String(const char* c) : s(c ? c : "") {} String(const std::string& x) : s(x) {}
  void trim() { size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n"); s = a == std::string::npos ? "" : s.substr(a, b - a + 1); }
  void toLowerCase() { for (auto& c : s) c = char(tolower(c)); }
  unsigned length() const { return s.size(); }
  char operator[](size_t i) const { return s[i]; }
  const char* c_str() const { return s.c_str(); }
  bool startsWith(const char* p) const { return s.rfind(p, 0) == 0; }
  String substring(int a, int b) const { String r; r.s = s.substr(a, b - a); return r; }
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
#include <algorithm>
using std::min; using std::max;
struct EspClass { void restart() { printf("[mock] ESP.restart()\n"); } };
inline EspClass ESP;
// FreeRTOS mutexes (the PC tests run on one thread)
typedef void* SemaphoreHandle_t;
#define portMAX_DELAY 0xffffffffu
inline SemaphoreHandle_t xSemaphoreCreateMutex() { static int m; return &m; }
inline SemaphoreHandle_t xSemaphoreCreateBinary() { static int b; return &b; }
#define pdTRUE 1
inline int g_semTakes = 0, g_semGives = 0; inline bool g_semFail = false;   // tests count them, or make Take time out
inline int xSemaphoreTake(SemaphoreHandle_t, uint32_t) { if (g_semFail) return 0; g_semTakes++; return 1; }
inline int xSemaphoreGive(SemaphoreHandle_t) { g_semGives++; return 1; }
#if !defined(__GLIBC__) || __GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 38)
inline size_t strlcpy(char* d, const char* s, size_t n) { size_t l = strlen(s); if (n) { size_t c = l < n - 1 ? l : n - 1; memcpy(d, s, c); d[c] = 0; } return l; }
#endif
struct SerialT { void begin(int) {} void println(const char* s) { printf("%s\n", s); } int printf(const char* f, ...) { va_list a; va_start(a, f); int n = vprintf(f, a); va_end(a); return n; } int available() { return 0; } int read() { return 0; } };
extern SerialT Serial;
inline uint32_t analogReadMilliVolts(uint8_t) { return 1900; }
inline void configTzTime(const char* tz, const char*, const char* = nullptr, const char* = nullptr) { setenv("TZ", tz, 1); tzset(); }
// FreeRTOS bits
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
inline void vTaskDelay(TickType_t) {}
inline int xTaskCreatePinnedToCore(void (*)(void*), const char*, uint32_t, void*, int, void*, int) { return 1; }
typedef void* TaskHandle_t;
enum eTaskState { eRunning = 0, eReady, eBlocked, eSuspended, eDeleted, eInvalid };
inline TaskHandle_t xTaskGetCurrentTaskHandleForCore(int) { return nullptr; }
inline eTaskState eTaskGetState(TaskHandle_t) { return eReady; }
inline const char* pcTaskGetName(TaskHandle_t) { return "?"; }
inline unsigned uxTaskPriorityGet(TaskHandle_t) { return 0; }
