// Wi-Fi setup through Dewy's own hotspot.
// With no Wi-Fi saved (or if the saved one can't be found for two minutes), Dewy opens a Wi-Fi network
// called "Dewy-Setup". Joining it on an iPhone pops up a page to pick the home Wi-Fi and type its password.
#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

static WebServer portal(80);
static DNSServer portalDns;
static bool portalOn = false;
static char portalMsg[96] = "";
static uint32_t portalTryAt = 0;       // when we started trying the Wi-Fi typed on the page
static char portalTrying[33] = "";

// SSIDs can contain any characters, so everything shown on the page is escaped
static String htmlEscape(const char* s) {
  String o;
  for (; *s; s++) {
    switch (*s) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      case '\'': o += "&#39;"; break;
      default: o += *s;
    }
  }
  return o;
}

static const char PORTAL_HEAD[] =
  "<!doctype html><html lang=en><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
  "<title>Dewy setup</title><style>"
  "body{font-family:-apple-system,system-ui,sans-serif;background:#FFF6F9;color:#2A1520;margin:0;padding:24px}"
  "main{max-width:420px;margin:auto}h1{color:#C8106B;margin:0 0 8px}p{line-height:1.45}"
  "label{display:block;font-weight:700;margin:16px 0 6px}"
  "select,input{width:100%;font-size:17px;padding:12px;border-radius:12px;border:1px solid #F0CFDC;background:#fff;box-sizing:border-box}"
  "button{margin-top:20px;width:100%;font-size:18px;font-weight:800;padding:14px;border:0;border-radius:999px;background:#E0457B;color:#fff}"
  ".msg{background:#FCE3EE;padding:10px 12px;border-radius:12px}.small{color:#7A5C6A;font-size:14px}"
  "</style><main><h1>Hi, I'm Dewy</h1>";

static void portalRoot() {
  String page = PORTAL_HEAD;
  page += "<p>Pick your home Wi-Fi and type its password, and I'll connect.</p>";
  if (portalMsg[0]) page += String("<p class=msg>") + htmlEscape(portalMsg) + "</p>";
  page += "<form method=post action=/save><label for=s>Wi-Fi network</label><select id=s name=s>";
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) WiFi.scanNetworks(true);    // start a fresh scan for next time
  for (int i = 0; i < n && i < 20; i++) {
    String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;
    String e = htmlEscape(ssid.c_str());
    page += "<option value=\"" + e + "\">" + e + "</option>";
  }
  page += "<option value=''>Other: type it below</option></select>"
          "<label for=o>Or type the network name</label><input id=o name=o maxlength=32 autocomplete=off autocapitalize=none autocorrect=off>"
          "<label for=p>Password</label><input id=p name=p type=password maxlength=63 autocomplete=current-password>"
          "<button>Connect</button></form>"
          "<p class=small>Don't see your network? Reload this page in a few seconds.</p></main>";
  portal.sendHeader("Cache-Control", "no-store");
  portal.send(200, "text/html", page);
}

static void portalSave() {
  String ssid = portal.arg("o").length() ? portal.arg("o") : portal.arg("s");
  String pass = portal.arg("p");
  ssid.trim();
  if (ssid.length() < 1 || ssid.length() > 32 || pass.length() > 63 || (pass.length() > 0 && pass.length() < 8)) {
    snprintf(portalMsg, sizeof portalMsg, "That didn't look right: Wi-Fi passwords are 8 to 63 characters.");
    portal.sendHeader("Location", "/"); portal.send(303); return;
  }
  prefs.putString("wifi_ssid", ssid);
  prefs.putString("wifi_pass", pass);
  strlcpy(portalTrying, ssid.c_str(), sizeof portalTrying);
  portalMsg[0] = 0;
  String page = PORTAL_HEAD;
  page += "<p>Connecting to <b>" + htmlEscape(ssid.c_str()) + "</b>...</p>"
          "<p>When I'm online I'll smile and this Wi-Fi called Dewy-Setup will go away. If it's still here in a minute, "
          "join it again: the page will say what went wrong.</p></main>";
  portal.send(200, "text/html", page);
  delay(300);
  WiFi.begin(ssid.c_str(), pass.c_str());
  portalTryAt = millis();
}

// iPhones check a few addresses to see if they're online; sending them our page makes the setup sheet pop up
static void portalRedirect() {
  portal.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
  portal.send(302);
}

static void startPortal() {
  if (portalOn) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(SETUP_AP_NAME);
  delay(100);
  portalDns.start(53, "*", WiFi.softAPIP());
  portal.on("/", HTTP_GET, portalRoot);
  portal.on("/save", HTTP_POST, portalSave);
  portal.onNotFound(portalRedirect);
  portal.begin();
  WiFi.scanNetworks(true);
  portalOn = true;
  Serial.printf("[wifi] setup hotspot \"%s\" is open\n", SETUP_AP_NAME);
}

static void stopPortal() {
  if (!portalOn) return;
  portal.stop();
  portalDns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  portalOn = false;
  Serial.println("[wifi] setup hotspot closed");
}

static void pollPortal() {
  if (!portalOn) return;
  portalDns.processNextRequest();
  portal.handleClient();
  // the Wi-Fi typed on the page didn't connect within 25 s: say so on the page
  if (portalTryAt && WiFi.status() != WL_CONNECTED && millis() - portalTryAt > 25000) {
    snprintf(portalMsg, sizeof portalMsg, "Couldn't connect to \"%s\". Check the password and try again.", portalTrying);
    portalTryAt = 0;
  }
  if (WiFi.status() == WL_CONNECTED) portalTryAt = 0;
}
