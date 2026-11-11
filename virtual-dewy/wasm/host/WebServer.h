// The Dewy-Setup page's web server stand-in. The browser can "visit" the page and "submit the form" (see host.cpp),
// which runs the firmware's real portalRoot() and portalSave().
#pragma once
#include "WiFi.h"
#include <functional>
#include <map>
enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_POST };
struct WebServer {
  std::map<std::string, std::function<void()>> routes; std::function<void()> nf; std::map<std::string, std::string> args;
  std::string lastBody; int lastCode = 0;
  explicit WebServer(int) {}
  void on(const char* p, HTTPMethod, std::function<void()> f) { routes[p] = f; }
  void on(const char* p, std::function<void()> f) { routes[p] = f; }
  void onNotFound(std::function<void()> f) { nf = f; }
  void begin() {} void stop() {} void handleClient() {}
  String arg(const char* k) { auto i = args.find(k); return String(i == args.end() ? std::string() : i->second); }
  void sendHeader(const char*, const String&) {}
  void send(int code, const char* = nullptr, const String& body = String()) { lastCode = code; lastBody = body.s; }
};
