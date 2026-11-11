// Wi-Fi stand-in with one virtual router. The browser decides whether the router is up; the "chip" connects to it
// after a short delay when the saved name and password match, and loses it when it goes down.
#pragma once
#include "Arduino.h"
#include <string>
enum wl_status_t { WL_IDLE_STATUS = 0, WL_CONNECTED = 3, WL_DISCONNECTED = 6 };
enum wifi_mode_t { WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 };
#define WIFI_SCAN_FAILED (-2)

struct HostWifi {
  bool routerUp = true;
  std::string routerSsid = "Virtual-Home", routerPass = "dewy1234";   // the pretend router (fake, not anybody's Wi-Fi)
  std::string ssid, pass;
  bool wantStation = false, apOn = false;
  int status = WL_DISCONNECTED;
  uint32_t beganAt = 0;
  int mode = 0;
};
inline HostWifi g_wifi;
inline const char* const HOST_SCAN_LIST[] = { "Virtual-Home", "Neighbour-5G", "Cafe <Guest>" };

struct IPAddress { bool ap = false; String toString() const { return String(ap ? "192.168.4.1" : "192.168.1.50"); } };
struct WiFiClass {
  bool mode(wifi_mode_t m) { g_wifi.mode = m; return true; }
  bool setHostname(const char*) { return true; }
  bool setAutoReconnect(bool) { return true; }
  int begin(const char* s, const char* p) {
    g_wifi.ssid = s ? s : ""; g_wifi.pass = p ? p : ""; g_wifi.wantStation = true;
    g_wifi.status = WL_IDLE_STATUS; g_wifi.beganAt = millis();
    return 0;
  }
  wl_status_t status() { return wl_status_t(g_wifi.status); }
  IPAddress localIP() { return {}; }
  bool softAP(const char*) { g_wifi.apOn = true; return true; }
  IPAddress softAPIP() { IPAddress a; a.ap = true; return a; }
  bool softAPdisconnect(bool) { g_wifi.apOn = false; return true; }
  int scanNetworks(bool = false) { return 3; }
  int scanComplete() { return 3; }
  String SSID(int i) { return String(i >= 0 && i < 3 ? HOST_SCAN_LIST[i] : ""); }
};
inline WiFiClass WiFi;
struct WiFiClient {
  int pos = 0; std::string data;
  size_t available() { return data.size() - pos; }
  int readBytes(uint8_t* b, size_t n) { size_t k = std::min(n, available()); memcpy(b, data.data() + pos, k); pos += int(k); return int(k); }
};

// called every tick by the host: moves the connection along
inline void hostWifiTick(uint32_t now) {
  HostWifi& w = g_wifi;
  bool credsOk = w.ssid == w.routerSsid && w.pass == w.routerPass;
  if (!w.wantStation || w.ssid.empty()) return;
  if (w.status == WL_CONNECTED) { if (!w.routerUp) w.status = WL_DISCONNECTED; return; }
  if (!w.routerUp) { w.status = WL_DISCONNECTED; w.beganAt = now; return; }
  if (w.status == WL_DISCONNECTED) { w.status = WL_IDLE_STATUS; w.beganAt = now; }   // auto-reconnect: try again
  if (now - w.beganAt >= 1500) w.status = credsOk ? WL_CONNECTED : WL_DISCONNECTED;
  if (!credsOk && w.status == WL_DISCONNECTED) w.beganAt = now + 8000;                // wrong password: retry slowly
}
