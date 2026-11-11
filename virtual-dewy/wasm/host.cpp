// Browser host for Dewy's real firmware.
//
// This file #includes the real dewy.ino (from a build copy of firmware/dewy, never the original folder) and gives it
// virtual hardware through the stand-in headers in wasm/host/. Everything the firmware does (the face, the serial
// commands, tap and pet timing, shake and pick-up detection, the 8-ball, idle moods, timers, the cloud code) is the
// firmware's own code. What this file adds:
//   - a few exported functions the browser calls (dewy_boot, dewy_tick, dewy_serial_in, ...),
//   - hostNetStep(): the firmware runs its network code in a FreeRTOS task on the second core; WebAssembly has no
//     second core, so the browser's tick calls the same functions with the same timing instead,
//   - status and pin reports so the page can show what the firmware is doing and which pins it really uses.
#include "Arduino.h"
#include "Wire.h"
#include "dewy.ino"

// ---------------------------------------------------------------- the 18 pt and 24 pt fonts from the repo's own test fonts
#define PROGMEM
namespace hostfont {
struct GFXglyph { uint32_t bitmapOffset; uint8_t width, height, xAdvance; int8_t xOffset, yOffset; };
struct GFXfont { uint8_t* bitmap; GFXglyph* glyph; uint16_t first, last; uint8_t yAdvance; };
#include "FreeSansBold18pt7b.h"
#include "FreeSansBold24pt7b.h"
}
#undef PROGMEM
namespace lgfx { inline namespace v1 { namespace fonts {
  const GFXfont FreeSansBold18pt7b = { hostfont::FreeSansBold18pt7b.bitmap, (GFXglyph*)hostfont::FreeSansBold18pt7b.glyph, hostfont::FreeSansBold18pt7b.first, hostfont::FreeSansBold18pt7b.last, hostfont::FreeSansBold18pt7b.yAdvance, 1.f };
  const GFXfont FreeSansBold24pt7b = { hostfont::FreeSansBold24pt7b.bitmap, (GFXglyph*)hostfont::FreeSansBold24pt7b.glyph, hostfont::FreeSansBold24pt7b.first, hostfont::FreeSansBold24pt7b.last, hostfont::FreeSansBold24pt7b.yAdvance, 1.f };
  // APPROXIMATE: the clock's date line really uses a 12 pt font; this is the 18 pt one drawn at two thirds size
  const GFXfont FreeSansBold12pt7b = { hostfont::FreeSansBold18pt7b.bitmap, (GFXglyph*)hostfont::FreeSansBold18pt7b.glyph, hostfont::FreeSansBold18pt7b.first, hostfont::FreeSansBold18pt7b.last, hostfont::FreeSansBold18pt7b.yAdvance, 12.f / 18.f };
}}}

#define DEWY_EXPORT(name) extern "C" __attribute__((export_name(name)))

// ---------------------------------------------------------------- the network task, stepped by the browser
// Mirrors the loop body of netTask() in cloud.h (same functions, same intervals: it runs every 250 ms there too).
static uint32_t nxSync = 0, nxWeather = 0, nxPair = 0, nxRegister = 0, nxRun = 0;
static void hostNetBoot() {
  netPrefs.begin("buddy", false);
  String tok = netPrefs.getString("token", "");
  if (tok.length() > 0 && tok.length() < sizeof deviceToken) strcpy(deviceToken, tok.c_str());
}
static void hostNetStep(uint32_t now) {
  if (now - nxRun < 250) return;
  nxRun = now;
  if (WiFi.status() != WL_CONNECTED) return;
  if (cloudOn()) {
    if (!deviceToken[0] && int32_t(now - nxRegister) >= 0) { if (!cloudRegister()) nxRegister = now + 10UL * 60UL * 1000UL; }
    if (deviceToken[0] && int32_t(now - nxSync) >= 0) {
      cloudSync();
      nxSync = millis() + (dewyAsleep ? SYNC_ASLEEP_MS : SYNC_AWAKE_MS);
    }
    bool needCode;
    { NetLock lock; needCode = inbox.pairedKnown && !inbox.paired && !inbox.pairCode[0]; }
    if (deviceToken[0] && (wantPair || (needCode && int32_t(now - nxPair) >= 0))) {
      wantPair = false; cloudRequestPair(); nxPair = millis() + 9UL * 60UL * 1000UL;
    }
    bool offer;
    { NetLock lock; offer = inbox.fwOffer; }
    if (offer && updateAllowed) installFirmware();
  }
  if (cityOn && int32_t(now - nxWeather) >= 0) nxWeather = millis() + (fetchWeather() ? 30UL : 1UL) * 60UL * 1000UL;
}

// ---------------------------------------------------------------- exports
static double g_realEpochMs = 0;
static std::string g_serialSnap, g_prefsSnap, g_pageSnap;
static uint8_t g_rgba[320 * 240 * 4];
static int32_t g_status[32];
static int32_t g_pinReport[16];
static int32_t g_cfgReport[16];
static char g_io[65536];   // scratch memory the browser writes strings into (serial text, Wi-Fi names, saved settings)

DEWY_EXPORT("dewy_io") char* dewy_io() { return g_io; }
DEWY_EXPORT("dewy_io_size") int dewy_io_size() { return int(sizeof g_io); }

// hw: bit 0 = the motion sensor answers on the I2C bus, bit 1 = the touch chip is wired
DEWY_EXPORT("dewy_boot") void dewy_boot(uint32_t seed, uint32_t hw, uint32_t now_ms, double epoch_ms) {
  host_rng = seed ? seed : 1;
  g_imuPresent = (hw & 1) ? 1 : 0;
  g_touchPresent = (hw & 2) ? 1 : 0;
  host_millis = now_ms;
  g_realEpochMs = epoch_ms;
  host_restart_requested = 0;
  setup();
  hostNetBoot();
}

DEWY_EXPORT("dewy_tick") void dewy_tick(uint32_t now_ms, double epoch_ms) {
  host_millis = now_ms;
  g_realEpochMs = epoch_ms;
  hostWifiTick(now_ms);
  // the clock from the internet arrives a moment after Wi-Fi connects (the real chip asks pool.ntp.org)
  static uint32_t sntpSince = 0;
  if (host_sntp_pending && g_wifi.status == WL_CONNECTED) {
    if (!sntpSince) sntpSince = now_ms ? now_ms : 1;
    if (now_ms - sntpSince > 800) {
      sntpSince = 0; host_sntp_pending = 0;
      struct timeval tv; tv.tv_sec = time_t(g_realEpochMs / 1000.0); tv.tv_usec = 0;
      settimeofday(&tv, nullptr);
      if (g_sntpCb) g_sntpCb(&tv);
    }
  }
  loop();
  hostNetStep(now_ms);
}

// serial: text typed into the monitor goes in, what the firmware prints comes out
DEWY_EXPORT("dewy_serial_in") void dewy_serial_in(const char* s, int n) { for (int i = 0; i < n; i++) g_serialIn.push_back(s[i]); }
DEWY_EXPORT("dewy_serial_take") int dewy_serial_take() { g_serialSnap.clear(); g_serialSnap.swap(g_serialOut); return int(g_serialSnap.size()); }
DEWY_EXPORT("dewy_serial_ptr") const char* dewy_serial_ptr() { return g_serialSnap.data(); }

// the screen
DEWY_EXPORT("dewy_frame") const uint8_t* dewy_frame() {
  for (int i = 0; i < face::W * face::H; i++) {
    face::RGB c = face::unpack(g_fb565[i]);
    g_rgba[i * 4] = uint8_t(c.r + 0.5f); g_rgba[i * 4 + 1] = uint8_t(c.g + 0.5f); g_rgba[i * 4 + 2] = uint8_t(c.b + 0.5f); g_rgba[i * 4 + 3] = 255;
  }
  return g_rgba;
}

// inputs from the virtual hardware
DEWY_EXPORT("dewy_touch") void dewy_touch(int down, int x, int y) { g_touchDown = down; g_touchX = x; g_touchY = y; }
DEWY_EXPORT("dewy_accel") void dewy_accel(float x, float y, float z) { g_accel[0] = x; g_accel[1] = y; g_accel[2] = z; }
DEWY_EXPORT("dewy_set_router") void dewy_set_router(int up) { g_wifi.routerUp = up != 0; }
DEWY_EXPORT("dewy_set_router_creds") void dewy_set_router_creds(const char* ssid, const char* pass) { g_wifi.routerSsid = ssid; g_wifi.routerPass = pass; }

// what the firmware is doing right now (indices are documented in src/sim/wasm.ts)
DEWY_EXPORT("dewy_status") const int32_t* dewy_status() {
  int32_t* s = g_status;
  s[0] = int32_t(g_fbSerial); s[1] = g_brightness; s[2] = int(buddy.mood());
  s[3] = buddy.faceDown(); s[4] = buddy.upsideDown(); s[5] = buddy.timerActive();
  s[6] = g_wifi.status; s[7] = portalOn; s[8] = cloudReady; s[9] = pairedKnown; s[10] = paired;
  s[11] = int(banner); s[12] = host_restart_requested; s[13] = clockIsReal; s[14] = motion.available(); s[15] = calOk;
  s[16] = tiltShown; s[17] = int32_t(tiltX * 1000); s[18] = int32_t(tiltY * 1000);
  s[19] = buddy.busy(); s[20] = buddy.overlayActive(); s[21] = g_wifi.wantStation; s[22] = g_wifi.apOn; s[23] = cloudOn();
  s[24] = buddy.mouth; s[25] = buddy.hasPendingNote(); s[26] = noteQn; s[27] = int(buddy.weather());
  return s;
}
DEWY_EXPORT("dewy_mood_name") const char* dewy_mood_name(int m) { return (m >= 0 && m < face::NUM_MOODS) ? face::MOOD_NAMES[m] : "?"; }
DEWY_EXPORT("dewy_mood_count") int dewy_mood_count() { return face::NUM_MOODS; }

// the pin numbers the firmware's own code handed to the drivers (display.h after config.h's macros, and Wire.begin)
DEWY_EXPORT("dewy_pins") const int32_t* dewy_pins() {
  int32_t* p = g_pinReport;
  p[0] = g_i2cSda; p[1] = g_i2cScl;
  p[2] = g_pins.sclk; p[3] = g_pins.mosi; p[4] = g_pins.miso; p[5] = g_pins.dc; p[6] = g_pins.cs; p[7] = g_pins.rst; p[8] = g_pins.bl;
  p[9] = g_pins.tcs; p[10] = g_pins.tirq; p[11] = g_pins.tsclk; p[12] = g_pins.tmosi; p[13] = g_pins.tmiso;
  p[14] = BATTERY_ENABLED ? PIN_BATTERY : -1;
  return p;
}
// a few settings from config.h worth showing next to the simulation
DEWY_EXPORT("dewy_config") const int32_t* dewy_config() {
  int32_t* c = g_cfgReport;
  c[0] = QUICK_TIMINGS; c[1] = int32_t(BORED_AFTER_MS); c[2] = int32_t(SLEEP_AFTER_MS); c[3] = TAP_MAX_MS; c[4] = PET_HOLD_MS;
  c[5] = EIGHT_BALL_WINDOW_MS; c[6] = FRAME_MS; c[7] = TOUCH_ENABLED; c[8] = SCREEN_ROTATION; c[9] = BATTERY_ENABLED;
  return c;
}
DEWY_EXPORT("dewy_version") const char* dewy_version() { return DEWY_VERSION; }

// flash storage, so the browser can keep it between restarts
DEWY_EXPORT("dewy_prefs_dump") int dewy_prefs_dump() {
  g_prefsSnap.clear();
  for (auto& kv : g_nvs) {
    g_prefsSnap += char(kv.first.size());
    g_prefsSnap += kv.first;
    uint32_t n = uint32_t(kv.second.size());
    g_prefsSnap.append(reinterpret_cast<const char*>(&n), 4);
    g_prefsSnap.append(reinterpret_cast<const char*>(kv.second.data()), n);
  }
  return int(g_prefsSnap.size());
}
DEWY_EXPORT("dewy_prefs_ptr") const char* dewy_prefs_ptr() { return g_prefsSnap.data(); }
DEWY_EXPORT("dewy_prefs_load") void dewy_prefs_load(const uint8_t* p, int n) {
  g_nvs.clear();
  int i = 0;
  while (i < n) {
    int kl = p[i++]; if (i + kl + 4 > n) break;
    std::string key(reinterpret_cast<const char*>(p + i), kl); i += kl;
    uint32_t vl; memcpy(&vl, p + i, 4); i += 4;
    if (i + int(vl) > n) break;
    g_nvs[key].assign(p + i, p + i + vl); i += int(vl);
  }
}

// the Dewy-Setup page: the browser can open it and submit the form, which runs the firmware's own handlers
DEWY_EXPORT("dewy_portal_page") const char* dewy_portal_page() {
  if (!portalOn || !portal.routes.count("/")) return "";
  portal.routes["/"]();
  g_pageSnap = portal.lastBody;
  return g_pageSnap.c_str();
}
DEWY_EXPORT("dewy_portal_save") int dewy_portal_save(const char* s, const char* o, const char* p) {
  if (!portalOn || !portal.routes.count("/save")) return -1;
  portal.args = { { "s", s }, { "o", o }, { "p", p } };
  portal.routes["/save"]();
  return portal.lastCode;
}
DEWY_EXPORT("dewy_portal_msg") const char* dewy_portal_msg() { return portalMsg; }
