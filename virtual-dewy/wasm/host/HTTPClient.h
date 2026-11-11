// HTTPClient stand-in: every request goes to the browser's mock server (a JavaScript function, answered at once).
// Nothing ever leaves the page: there is no network access from here.
#pragma once
#include "WiFi.h"
#include <string>
enum followRedirects_t { HTTPC_DISABLE_FOLLOW_REDIRECTS, HTTPC_STRICT_FOLLOW_REDIRECTS, HTTPC_FORCE_FOLLOW_REDIRECTS };

extern "C" {
__attribute__((import_module("env"), import_name("host_http")))
int host_http(const char* method, const char* url, const char* body, const char* headers);
__attribute__((import_module("env"), import_name("host_http_body")))
int host_http_body(char* dst, int cap);   // copies the last response body, returns its length
}

struct HTTPClient {
  std::string url, headers, resp; WiFiClient* cl = nullptr;
  bool begin(WiFiClient& c, const char* u) { cl = &c; url = u; headers.clear(); return true; }
  bool begin(WiFiClient& c, const String& u) { return begin(c, u.c_str()); }
  void addHeader(const char* k, const String& v) { headers += std::string(k) + ": " + v.s + "\n"; }
  void setTimeout(int) {} void setFollowRedirects(followRedirects_t) {}
  int run(const char* m, const char* body) {
    int code = host_http(m, url.c_str(), body ? body : "", headers.c_str());
    int n = host_http_body(nullptr, 0);
    resp.assign(size_t(n), '\0');
    if (n > 0) host_http_body(&resp[0], n);
    cl->data = resp; cl->pos = 0;
    return code;
  }
  int GET() { return run("GET", nullptr); }
  int POST(const char* body) { return run("POST", body); }
  int POST(const String& body) { return run("POST", body.c_str()); }
  String getString() { return String(resp); }
  int getSize() { return int(resp.size()); }
  WiFiClient* getStreamPtr() { return cl; }
  void end() {}
};
