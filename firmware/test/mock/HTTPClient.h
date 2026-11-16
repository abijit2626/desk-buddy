#pragma once
#include "WiFi.h"
#include <functional>
#include <map>
#include <string>
enum followRedirects_t { HTTPC_DISABLE_FOLLOW_REDIRECTS, HTTPC_STRICT_FOLLOW_REDIRECTS, HTTPC_FORCE_FOLLOW_REDIRECTS };
// test hook: (method, url, body, headers) -> reply. headers: extra response headers. chunked: send the body in 700-byte chunks
// with no Content-Length, the way a server may.
struct MockReply { int code; std::string body; std::map<std::string, std::string> headers = {}; bool chunked = false; };
inline std::function<MockReply(const std::string&, const std::string&, const std::string&, const std::string&)> g_http =
  [](const std::string&, const std::string&, const std::string&, const std::string&) {
    return MockReply{ 200, "{\"current\":{\"weather_code\":61,\"is_day\":1}}" };
  };
inline size_t g_lastPostBytes = 0;   // size of the last binary POST body
struct HTTPClient {
  static String errorToString(int) { return String("connection refused"); }
  std::string url, headers, resp, ua = "ESP32HTTPClient"; std::map<std::string, std::string> rh; bool chunked = false; WiFiClient* cl = nullptr;
  bool begin(WiFiClient& c, const char* u) { cl = &c; url = u; headers.clear(); return true; }
  bool begin(WiFiClient& c, const String& u) { return begin(c, u.c_str()); }
  void addHeader(const char* k, const String& v) { headers += std::string(k) + ": " + v.s + "\n"; }
  void setUserAgent(const char* u) { ua = u; }   // kept across begin(), like the real one
  void collectHeaders(const char**, size_t) {}
  String header(const char* k) {
    std::string want = k; for (auto& c : want) c = char(tolower(c));
    for (auto& kv : rh) { std::string have = kv.first; for (auto& c : have) c = char(tolower(c)); if (have == want) return String(kv.second); }
    return String("");
  }
  void setTimeout(int) {} void setFollowRedirects(followRedirects_t) {}
  int run(const char* m, const char* body, size_t n = 0) {
    MockReply r = g_http(m, url, body ? std::string(body, n ? n : strlen(body)) : "", headers + "User-Agent: " + ua + "\n");
    resp = r.body; rh = r.headers; chunked = r.chunked;
    if (chunked) {
      std::string enc;
      for (size_t i = 0; i < r.body.size(); i += 700) { size_t k = std::min<size_t>(700, r.body.size() - i); char h[16]; snprintf(h, sizeof h, "%zx\r\n", k); enc += h + r.body.substr(i, k) + "\r\n"; }
      enc += "0\r\n\r\n"; cl->data = enc; rh["Transfer-Encoding"] = "chunked";
    } else cl->data = r.body;
    cl->pos = 0; return r.code;
  }
  int GET() { return run("GET", nullptr); }
  int POST(const char* body) { return run("POST", body); }
  int POST(const String& body) { return run("POST", body.c_str()); }
  int POST(uint8_t* body, size_t n) { g_lastPostBytes = n; return run("POST", (const char*)body, n); }
  String getString() { return String(resp); }
  int getSize() { return chunked ? -1 : int(resp.size()); }
  WiFiClient* getStreamPtr() { return cl; }
  void end() {}
};
