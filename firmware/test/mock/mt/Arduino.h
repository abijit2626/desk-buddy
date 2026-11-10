#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cstdarg>
inline uint32_t millis() { return 0; }
inline void delay(uint32_t) {}
inline uint32_t esp_random() { return 1; }
struct String {
  std::string s; String() {} String(const char* c) : s(c) {}
  void trim() {} void toLowerCase() {} unsigned length() const { return s.size(); }
  bool startsWith(const char* p) const { return s.rfind(p, 0) == 0; }
  String substring(int a, int b) const { String r; r.s = s.substr(a, b - a); return r; }
  long toInt() const { return atol(s.c_str()); }
  bool operator==(const char* o) const { return s == o; }
  String& operator+=(char c) { s += c; return *this; }
  String& operator=(const char* c) { s = c; return *this; }
};
struct SerialT { void begin(int) {} void println(const char*) {} int printf(const char*, ...) { return 0; } int available() { return 0; } int read() { return 0; } };
extern SerialT Serial;
