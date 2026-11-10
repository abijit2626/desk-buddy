#pragma once
#include <functional>
enum ota_error_t { OTA_AUTH_ERROR, OTA_BEGIN_ERROR, OTA_CONNECT_ERROR, OTA_RECEIVE_ERROR, OTA_END_ERROR };
struct ArduinoOTAClass {
  std::function<void()> s, e; std::function<void(unsigned, unsigned)> p; std::function<void(ota_error_t)> er;
  ArduinoOTAClass& setHostname(const char*) { return *this; } ArduinoOTAClass& setPassword(const char*) { return *this; }
  ArduinoOTAClass& onStart(std::function<void()> f) { s = f; return *this; } ArduinoOTAClass& onEnd(std::function<void()> f) { e = f; return *this; }
  ArduinoOTAClass& onProgress(std::function<void(unsigned, unsigned)> f) { p = f; return *this; } ArduinoOTAClass& onError(std::function<void(ota_error_t)> f) { er = f; return *this; }
  void begin() {} void handle() {}
};
inline ArduinoOTAClass ArduinoOTA;
