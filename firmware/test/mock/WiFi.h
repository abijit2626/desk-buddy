#pragma once
#include "Arduino.h"
enum wl_status_t { WL_IDLE_STATUS = 0, WL_CONNECTED = 3, WL_DISCONNECTED = 6 };
enum wifi_mode_t { WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 };
#define WIFI_SCAN_FAILED (-2)
extern int g_wifiStatus;
struct IPAddress { String toString() const { return String("192.168.1.50"); } };
struct WiFiClass {
  bool mode(wifi_mode_t) { return true; } bool setHostname(const char*) { return true; } bool setAutoReconnect(bool) { return true; }
  int begin(const char*, const char*) { return 0; } wl_status_t status() { return wl_status_t(g_wifiStatus); } IPAddress localIP() { return {}; }
  bool softAP(const char*) { return true; } IPAddress softAPIP() { return {}; } bool softAPdisconnect(bool) { return true; }
  int scanNetworks(bool = false) { return -1; } int scanComplete() { return 2; } String SSID(int i) { return String(i ? "Home <Wi-Fi>" : "Dewy's \"home\""); }
};
inline WiFiClass WiFi;
struct WiFiClient { int pos = 0; std::string data; size_t available() { return data.size() - pos; }
  bool connected() { return pos < int(data.size()); }
  int read(uint8_t* b, size_t n) { return readBytes(b, n); } int readBytes(uint8_t* b, size_t n) { size_t k = std::min(n, available()); memcpy(b, data.data() + pos, k); pos += int(k); return int(k); } };
