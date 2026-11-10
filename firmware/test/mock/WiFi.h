#pragma once
#include "Arduino.h"
enum wl_status_t { WL_IDLE_STATUS = 0, WL_CONNECTED = 3, WL_DISCONNECTED = 6 };
enum wifi_mode_t { WIFI_STA = 1 };
struct IPAddress { String toString() const { return String("192.168.1.50"); } };
struct WiFiClass {
  bool mode(wifi_mode_t) { return true; } bool setHostname(const char*) { return true; } bool setAutoReconnect(bool) { return true; }
  int begin(const char*, const char*) { return 0; } wl_status_t status() { return WL_CONNECTED; } IPAddress localIP() { return {}; }
};
inline WiFiClass WiFi;
struct WiFiClient {};
