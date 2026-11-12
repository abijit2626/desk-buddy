// Dewy's connection to its Cloudflare server and the internet.
// A background task on core 0 does all the slow HTTPS work (sync, weather, firmware download), so the face
// on core 1 never stutters. The two sides only meet in the small "inbox" below, guarded by a lock.
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "mbedtls/sha256.h"
#include "cacerts.h"

// ---------------------------------------------------------------- settings (from the phone app)
struct DewyAlarm { uint8_t h, m, dayMask; bool on; };   // dayMask: bit 0 = Sunday ... bit 6 = Saturday; 0 = every day
struct DewySettings {
  char tz[64];
  bool hasCity; float lat, lon;
  DewyAlarm alarms[10]; uint8_t nAlarms;
  uint16_t water, stretch; uint8_t from, to;
  uint16_t study;
  uint8_t bdM, bdD;
  uint16_t gY; uint8_t gM, gD;
  bool powerOn;                   // false = switched off in the app: Dewy rests until switched on
};
static DewySettings S;            // the main loop's copy

static void settingsDefaults() {
  memset(&S, 0, sizeof S);
  strncpy(S.tz, TIMEZONE, sizeof S.tz - 1);
  S.hasCity = true; S.lat = WEATHER_LAT; S.lon = WEATHER_LON;
  if (strlen(ALARM_TIME) == 5) { S.alarms[0] = { uint8_t(atoi(ALARM_TIME)), uint8_t(atoi(ALARM_TIME + 3)), 0, true }; S.nAlarms = 1; }
  S.water = REMIND_WATER_MIN; S.stretch = REMIND_STRETCH_MIN; S.from = REMIND_FROM_HOUR; S.to = REMIND_TO_HOUR;
  S.study = STUDY_MINUTES;
  S.bdM = BIRTHDAY_MONTH; S.bdD = BIRTHDAY_DAY;
  S.gY = GIFT_YEAR; S.gM = GIFT_MONTH; S.gD = GIFT_DAY;
  S.powerOn = true;
}

// The server already checks everything; this is a second, defensive check.
static bool settingsFromJson(const char* json) {
  JsonDocument d;
  if (deserializeJson(d, json)) return false;
  DewySettings n = S;
  const char* tz = d["timezone"] | "";
  if (strlen(tz) > 0 && strlen(tz) < sizeof n.tz) strcpy(n.tz, tz);
  JsonVariant c = d["city"];
  n.hasCity = c.is<JsonObject>();
  if (n.hasCity) { n.lat = c["lat"] | 0.0f; n.lon = c["lon"] | 0.0f; }
  n.nAlarms = 0;
  for (JsonObject a : d["alarms"].as<JsonArray>()) {
    if (n.nAlarms >= 10) break;
    const char* tm = a["time"] | "";
    if (strlen(tm) != 5) continue;
    uint8_t mask = 0;
    for (int day : a["days"].as<JsonArray>()) if (day >= 0 && day <= 6) mask |= uint8_t(1 << day);
    n.alarms[n.nAlarms++] = { uint8_t(atoi(tm) % 24), uint8_t(atoi(tm + 3) % 60), mask, a["on"] | false };
  }
  n.water = constrain(int(d["reminders"]["water"] | 0), 0, 480);
  n.stretch = constrain(int(d["reminders"]["stretch"] | 0), 0, 480);
  n.from = constrain(int(d["reminders"]["from"] | 9), 0, 23);
  n.to = constrain(int(d["reminders"]["to"] | 21), 1, 24);
  n.study = constrain(int(d["studyMinutes"] | 25), 1, 180);
  n.bdM = d["birthday"]["month"] | 0; n.bdD = d["birthday"]["day"] | 0;
  n.gY = d["giftDate"]["year"] | 0; n.gM = d["giftDate"]["month"] | 0; n.gD = d["giftDate"]["day"] | 0;
  n.powerOn = d["powerOn"] | true;         // older settings without the switch mean "on"
  S = n;
  return true;
}

// ---------------------------------------------------------------- what the two sides share
struct NetEvent { int id; char kind[16]; char text[128]; char from[24]; int minutes; };
struct NetInbox {
  bool hasSettings; int rev; char settingsJson[2048];
  NetEvent ev[10]; int nev;
  bool pairedKnown, paired; char pairCode[12];
  bool fwOffer; char fwVersion[16]; char fwUrl[420]; char fwSha[65]; uint32_t fwSize;
  int weather;                       // -1 = nothing new
};
static NetInbox inbox;
static SemaphoreHandle_t netLock = nullptr;
struct NetLock {
  NetLock() { xSemaphoreTake(netLock, portMAX_DELAY); }
  ~NetLock() { xSemaphoreGive(netLock); }
};
// main loop -> network task
static volatile int ackId = 0;               // the newest event the face has handled
static volatile bool wantPair = false;       // ask for a new pairing code now
static volatile bool updateAllowed = false;  // a firmware update may start (nothing important on screen)
static volatile bool dewyAsleep = false;     // check in less often while asleep
static volatile bool dewyResting = false;    // switched off in the app: no voice, no reminders
static volatile int bondReport = -1;         // bond 0..1000 for the phone app, -1 = not known yet
static volatile float cityLat = 0, cityLon = 0;
static volatile bool cityOn = false;
// network task -> main loop
static volatile float fwProgress = -1;       // 0..1 while downloading new firmware
static volatile bool cloudReady = false;     // registered with the server

static bool cloudOn() { return strlen(DEWY_SERVER) > 8; }

// ---------------------------------------------------------------- one HTTPS conversation at a time
// Each TLS connection needs about 45 KB of internal RAM. The network task (sync, weather, updates) and the voice task
// both talk HTTPS, so they take turns: whoever holds the lock is the only one with a connection open.
static SemaphoreHandle_t tlsLock = nullptr;
struct TlsGuard {
  bool ok;
  explicit TlsGuard(uint32_t waitMs = 0xFFFFFFFFu) {
    ok = !tlsLock || xSemaphoreTake(tlsLock, waitMs == 0xFFFFFFFFu ? portMAX_DELAY : pdMS_TO_TICKS(waitMs)) == pdTRUE;
  }
  ~TlsGuard() { if (ok && tlsLock) xSemaphoreGive(tlsLock); }
  TlsGuard(const TlsGuard&) = delete;
  TlsGuard& operator=(const TlsGuard&) = delete;
};

// ---------------------------------------------------------------- HTTPS helper
static char deviceToken[64] = "";

static int httpsJson(const char* method, const String& url, const char* body, JsonDocument* out, bool sendToken = true, const char* regKey = nullptr) {
  TlsGuard tls;
  WiFiClientSecure client;
  client.setCACert(CA_BUNDLE);
  client.setTimeout(15);
  HTTPClient http;
  http.setUserAgent("Dewy-ESP32/1.0");
  http.setTimeout(15000);
  if (!http.begin(client, url)) return -1;
  if (sendToken && deviceToken[0]) http.addHeader("Authorization", String("Bearer ") + deviceToken);
  if (regKey) http.addHeader("X-Register-Key", regKey);
  http.addHeader("X-Dewy-Version", DEWY_VERSION);
  int code;
  if (strcmp(method, "GET") == 0) code = http.GET();
  else { http.addHeader("Content-Type", "application/json"); code = http.POST(body ? body : "{}"); }
  if (code > 0 && out) {
    String payload = http.getString();
    if (deserializeJson(*out, payload.c_str())) out->clear();
  }
  http.end();
  return code;
}

// ---------------------------------------------------------------- the network task's jobs
static Preferences netPrefs;
static int knownRev = 0, lastQueued = 0;

static bool cloudRegister() {
  JsonDocument d;
  String body = String("{\"fw\":\"") + DEWY_VERSION + "\"}";
  int code = httpsJson("POST", String(DEWY_SERVER) + "/api/device/register", body.c_str(), &d, false, DEWY_REGISTER_KEY);
  const char* tok = d["token"] | "";
  if (code != 201 || strlen(tok) < 20 || strlen(tok) >= sizeof deviceToken) {
    Serial.printf("[cloud] couldn't register (HTTP %d)%s\n", code, code == 403 ? ": DEWY_REGISTER_KEY doesn't match the server's REGISTER_KEY" : "");
    return false;
  }
  strcpy(deviceToken, tok);
  netPrefs.putString("token", deviceToken);
  Serial.println("[cloud] registered with the server");
  return true;
}

static void cloudSync() {
  char path[112];
  int b = bondReport;
  if (b >= 0) snprintf(path, sizeof path, "/api/device/sync?rev=%d&ack=%d&bond=%d", knownRev, int(ackId), b);
  else snprintf(path, sizeof path, "/api/device/sync?rev=%d&ack=%d", knownRev, int(ackId));
  JsonDocument d;
  int code = httpsJson("GET", String(DEWY_SERVER) + path, nullptr, &d);
  if (code == 401) {                         // the server doesn't know this Dewy any more: start over
    deviceToken[0] = 0; netPrefs.remove("token"); cloudReady = false;
    Serial.println("[cloud] the server forgot this Dewy; registering again");
    return;
  }
  if (code != 200) { Serial.printf("[cloud] sync failed (HTTP %d)\n", code); return; }
  cloudReady = true;
  NetLock lock;
  int rev = d["rev"] | knownRev;
  if (d["settings"].is<JsonObject>()) {
    size_t n = serializeJson(d["settings"], inbox.settingsJson, sizeof inbox.settingsJson);
    if (n > 0 && n < sizeof inbox.settingsJson - 1) { inbox.hasSettings = true; inbox.rev = rev; }
  }
  knownRev = rev;
  for (JsonObject e : d["events"].as<JsonArray>()) {
    int id = e["id"] | 0;
    if (id <= lastQueued || inbox.nev >= 10) continue;
    NetEvent& ev = inbox.ev[inbox.nev++];
    ev.id = id;
    strlcpy(ev.kind, e["kind"] | "", sizeof ev.kind);
    strlcpy(ev.text, e["text"] | "", sizeof ev.text);
    strlcpy(ev.from, e["from"] | "", sizeof ev.from);
    ev.minutes = e["minutes"] | 0;
    lastQueued = id;
  }
  inbox.pairedKnown = true;
  inbox.paired = d["paired"] | false;
  strlcpy(inbox.pairCode, d["pairCode"] | "", sizeof inbox.pairCode);
  JsonVariant fw = d["firmware"];
  if (fw.is<JsonObject>() && !inbox.fwOffer) {
    strlcpy(inbox.fwVersion, fw["version"] | "", sizeof inbox.fwVersion);
    strlcpy(inbox.fwUrl, fw["url"] | "", sizeof inbox.fwUrl);
    strlcpy(inbox.fwSha, fw["sha256"] | "", sizeof inbox.fwSha);
    inbox.fwSize = fw["size"] | 0;
    inbox.fwOffer = strlen(inbox.fwSha) == 64 && strncmp(inbox.fwUrl, "https://", 8) == 0 && inbox.fwSize > 0;
    if (inbox.fwOffer) Serial.printf("[update] version %s is available\n", inbox.fwVersion);
  }
}

static void cloudRequestPair() {
  JsonDocument d;
  int code = httpsJson("POST", String(DEWY_SERVER) + "/api/device/pair", "{}", &d);
  if (code != 200) { Serial.printf("[cloud] couldn't get a pairing code (HTTP %d)\n", code); return; }
  NetLock lock;
  strlcpy(inbox.pairCode, d["code"] | "", sizeof inbox.pairCode);
}

// Open-Meteo weather codes: 0-1 clear, 2-3 cloudy, 45/48 fog, 51-67 drizzle and rain, 71-77 snow,
// 80-82 showers, 85-86 snow showers, 95-99 thunder
static int parseWeather(const char* s) {
  const char* cur = strstr(s, "\"current\":{");
  if (!cur) return -1;
  const char* wc = strstr(cur, "\"weather_code\":");
  const char* dy = strstr(cur, "\"is_day\":");
  if (!wc) return -1;
  int code = atoi(wc + 15), day = dy ? atoi(dy + 9) : 1;
  if (code <= 1) return day ? face::WEATHER_SUN : face::WEATHER_NONE;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82) || code >= 95) return face::WEATHER_RAIN;
  return face::WEATHER_CLOUD;
}
static bool fetchWeather() {
  TlsGuard tls;
  WiFiClientSecure client;
  client.setCACert(CA_BUNDLE);
  HTTPClient http;
  http.setUserAgent("Dewy-ESP32/1.0");
  char url[180];
  snprintf(url, sizeof url, "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=weather_code,is_day", float(cityLat), float(cityLon));
  bool ok = false;
  if (http.begin(client, url)) {
    if (http.GET() == 200) {
      int w = parseWeather(http.getString().c_str());
      if (w >= 0) { NetLock lock; inbox.weather = w; ok = true; }
    }
    http.end();
  }
  return ok;
}

// Downloads new firmware, checks its SHA-256 against what the server said, installs it and restarts.
static char fwFailed[16] = "";
static void installFirmware() {
  char url[420], sha[65], version[16]; uint32_t size;
  { NetLock lock; strcpy(url, inbox.fwUrl); strcpy(sha, inbox.fwSha); strcpy(version, inbox.fwVersion); size = inbox.fwSize; inbox.fwOffer = false; }
  if (strcmp(version, fwFailed) == 0) return;   // already failed once since starting up
  Serial.printf("[update] installing %s (%u bytes)\n", version, unsigned(size));
  fwProgress = 0;
  WiFiClientSecure client;
  client.setCACert(CA_BUNDLE);
  HTTPClient http;
  http.setUserAgent("Dewy-ESP32/1.0");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);   // GitHub sends release files from another address
  http.setTimeout(20000);
  bool ok = false;
  if (http.begin(client, url) && http.GET() == 200 && uint32_t(http.getSize()) == size && Update.begin(size)) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    WiFiClient* stream = http.getStreamPtr();
    static uint8_t buf[4096];
    uint32_t done = 0, lastData = millis();
    while (done < size && millis() - lastData < 20000) {
      size_t avail = stream->available();
      if (!avail) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
      int n = stream->readBytes(buf, min(avail, min(sizeof buf, size_t(size - done))));
      if (n <= 0) continue;
      mbedtls_sha256_update(&ctx, buf, n);
      if (Update.write(buf, n) != size_t(n)) break;
      done += n; lastData = millis();
      fwProgress = float(done) / float(size);
    }
    uint8_t digest[32];
    mbedtls_sha256_finish(&ctx, digest);
    mbedtls_sha256_free(&ctx);
    char hex[65];
    for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    if (done != size) Serial.println("[update] download stopped part-way");
    else if (strcmp(hex, sha) != 0) Serial.println("[update] the file's SHA-256 doesn't match: not installing it");
    else ok = Update.end(true);
    if (!ok) Update.abort();
  } else {
    Serial.println("[update] couldn't download the new firmware");
  }
  http.end();
  if (!ok) { strlcpy(fwFailed, version, sizeof fwFailed); fwProgress = -1; return; }
  Serial.println("[update] installed; restarting");
  fwProgress = 1;
  vTaskDelay(pdMS_TO_TICKS(2500));
  ESP.restart();
}

static void netTask(void*) {
  netPrefs.begin("buddy", false);
  String tok = netPrefs.getString("token", "");
  if (tok.length() > 0 && tok.length() < sizeof deviceToken) strcpy(deviceToken, tok.c_str());
  uint32_t nextSync = 0, nextWeather = 0, nextPair = 0, nextRegister = 0;
  for (;;) {
    uint32_t now = millis();
    if (WiFi.status() == WL_CONNECTED) {
      if (cloudOn()) {
        if (!deviceToken[0] && int32_t(now - nextRegister) >= 0) { if (!cloudRegister()) nextRegister = now + 10UL * 60UL * 1000UL; }
        if (deviceToken[0] && int32_t(now - nextSync) >= 0) {
          cloudSync();
          nextSync = millis() + (dewyAsleep ? SYNC_ASLEEP_MS : SYNC_AWAKE_MS);
        }
        bool needCode;
        { NetLock lock; needCode = inbox.pairedKnown && !inbox.paired && !inbox.pairCode[0]; }
        if (deviceToken[0] && (wantPair || (needCode && int32_t(now - nextPair) >= 0))) {
          wantPair = false; cloudRequestPair(); nextPair = millis() + 9UL * 60UL * 1000UL;
        }
        bool offer;
        { NetLock lock; offer = inbox.fwOffer; }
        if (offer && updateAllowed) {
          TlsGuard tls;                                   // wait for a voice turn that is talking to the server
          if (updateAllowed) installFirmware();           // and look again: it may have started a conversation meanwhile
        }
      }
      if (cityOn && int32_t(now - nextWeather) >= 0) nextWeather = millis() + (fetchWeather() ? 30UL : 1UL) * 60UL * 1000UL;
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

static void startNetTask() {
  netLock = xSemaphoreCreateMutex();
  tlsLock = xSemaphoreCreateMutex();
  memset(&inbox, 0, sizeof inbox);
  inbox.weather = -1;
  xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, nullptr, 0);
}
