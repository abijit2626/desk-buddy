// Flash storage stand-in. One shared store (like the real NVS: both Preferences objects in the firmware use the
// "buddy" namespace), which the browser can save and restore so settings survive a restart or a page reload.
#pragma once
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include "Arduino.h"
inline std::map<std::string, std::vector<uint8_t>> g_nvs;
struct Preferences {
  bool begin(const char*, bool) { return true; }
  size_t getBytesLength(const char* k) { auto i = g_nvs.find(k); return i == g_nvs.end() ? 0 : i->second.size(); }
  size_t getBytes(const char* k, void* b, size_t n) { auto i = g_nvs.find(k); if (i == g_nvs.end()) return 0; n = std::min(n, i->second.size()); memcpy(b, i->second.data(), n); return n; }
  size_t putBytes(const char* k, const void* b, size_t n) { g_nvs[k].assign((const uint8_t*)b, (const uint8_t*)b + n); return n; }
  float getFloat(const char* k, float d) { float v = d; if (getBytesLength(k) == 4) getBytes(k, &v, 4); return v; }
  size_t putFloat(const char* k, float v) { return putBytes(k, &v, 4); }
  uint32_t getUInt(const char* k, uint32_t d) { uint32_t v = d; if (getBytesLength(k) == 4) getBytes(k, &v, 4); return v; }
  size_t putUInt(const char* k, uint32_t v) { return putBytes(k, &v, 4); }
  String getString(const char* k, const char* d = "") { auto i = g_nvs.find(k); return i == g_nvs.end() ? String(d) : String(std::string(i->second.begin(), i->second.end())); }
  size_t putString(const char* k, const String& v) { g_nvs[k].assign(v.s.begin(), v.s.end()); return v.s.size(); }
  bool remove(const char* k) { return g_nvs.erase(k) > 0; }
};
