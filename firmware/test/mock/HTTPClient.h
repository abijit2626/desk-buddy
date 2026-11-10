#pragma once
#include "WiFi.h"
#include <functional>
#include <string>
enum followRedirects_t { HTTPC_DISABLE_FOLLOW_REDIRECTS, HTTPC_STRICT_FOLLOW_REDIRECTS, HTTPC_FORCE_FOLLOW_REDIRECTS };
// test hook: (method, url, body, headers) -> (status, response body)
struct MockReply { int code; std::string body; };
inline std::function<MockReply(const std::string&, const std::string&, const std::string&, const std::string&)> g_http =
  [](const std::string&, const std::string&, const std::string&, const std::string&) {
    return MockReply{ 200, "{\"current\":{\"weather_code\":61,\"is_day\":1}}" };
  };
struct HTTPClient {
  std::string url, headers, resp; WiFiClient* cl = nullptr;
  bool begin(WiFiClient& c, const char* u) { cl = &c; url = u; headers.clear(); return true; }
  bool begin(WiFiClient& c, const String& u) { return begin(c, u.c_str()); }
  void addHeader(const char* k, const String& v) { headers += std::string(k) + ": " + v.s + "\n"; }
  void setTimeout(int) {} void setFollowRedirects(followRedirects_t) {}
  int run(const char* m, const char* body) { MockReply r = g_http(m, url, body ? body : "", headers); resp = r.body; cl->data = r.body; cl->pos = 0; return r.code; }
  int GET() { return run("GET", nullptr); }
  int POST(const char* body) { return run("POST", body); }
  int POST(const String& body) { return run("POST", body.c_str()); }
  String getString() { return String(resp); }
  int getSize() { return int(resp.size()); }
  WiFiClient* getStreamPtr() { return cl; }
  void end() {}
};
