#pragma once
#include "WiFi.h"
struct HTTPClient {
  bool begin(WiFiClient&, const char*) { return true; } int GET() { return 200; }
  String getString() { return String("{\"current_units\":{\"weather_code\":\"wmo code\"},\"current\":{\"time\":\"2026-11-10T12:00\",\"interval\":900,\"weather_code\":61,\"is_day\":1}}"); }
  void end() {}
};
