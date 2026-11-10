#pragma once
#include <map>
#include <string>
#include <vector>
#include <cstring>
struct Preferences {
  std::map<std::string, std::vector<uint8_t>> m;
  bool begin(const char*, bool) { return true; }
  size_t getBytesLength(const char* k) { auto i = m.find(k); return i == m.end() ? 0 : i->second.size(); }
  size_t getBytes(const char* k, void* b, size_t n) { auto i = m.find(k); if (i == m.end()) return 0; memcpy(b, i->second.data(), n); return n; }
  size_t putBytes(const char* k, const void* b, size_t n) { m[k].assign((const uint8_t*)b, (const uint8_t*)b + n); return n; }
  float getFloat(const char* k, float d) { float v = d; if (getBytesLength(k) == 4) getBytes(k, &v, 4); return v; }
  size_t putFloat(const char* k, float v) { return putBytes(k, &v, 4); }
  uint32_t getUInt(const char* k, uint32_t d) { uint32_t v = d; if (getBytesLength(k) == 4) getBytes(k, &v, 4); return v; }
  size_t putUInt(const char* k, uint32_t v) { return putBytes(k, &v, 4); }
};
