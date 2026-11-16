/*
  Dewy, the desk buddy
  ESP32-S3 (N16R8) + 3.2" ILI9341 SPI screen with XPT2046 touch + GY-87 (MPU6050)

  What it does
  - Egg-shaped eyes that blink, look around and breathe, with 12 moods
  - Every time it wakes up: a new eye colour (46 sets, some rare and odd-eyed) and a new pupil shape
    (round 50%, acorn 20%, cat 30%)
  - Tap the screen to poke it (5 quick pokes = annoyed), hold your finger on it to pet it
  - Shake it = dizzy, pick it up = surprised, lean it = the pupils roll downhill
  - Lay it on its face = quiet time (asleep, screen off); upside down = it complains
  - Tap, then shake within 4 s = magic 8-ball
  - Gets bored when ignored, then falls asleep and dims the screen
  - With Wi-Fi: real time, weather in its eyes, updates over Wi-Fi
  - Study timer, water and stretch reminders, notes, birthdays, a personality that grows, low-battery face
  - Type commands in the Serial Monitor (115200 baud) to test everything: send "help"

  Libraries: LovyanGFX (Library Manager). Board settings are in README.md.
*/
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <esp_sntp.h>
#include <time.h>
#include <sys/time.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include "config.h"
#include "display.h"
#include "face.h"
#include "motion.h"
#include "types.h"
#include "bond.h"

static LGFX tft;
static face::Face buddy;
static face::Canvas canvas;
static Motion motion;
static Preferences prefs;
static face::Font font18, font24;

static uint32_t lastFrame = 0, lastInteract = 0, lastTapAt = 0;
static float brightness = 0;
static bool clockIsReal = false;          // true once the time came from the internet (or "settime")

// ---------------- clock (from the compile time until Wi-Fi sets the real time) ----------------
static void setClockFromCompileTime() {
  static const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = { __DATE__[0], __DATE__[1], __DATE__[2], 0 };
  struct tm tmv = {};
  tmv.tm_mon = (strstr(months, mon) - months) / 3;
  tmv.tm_mday = atoi(__DATE__ + 4);
  tmv.tm_year = atoi(__DATE__ + 7) - 1900;
  tmv.tm_hour = atoi(__TIME__);
  tmv.tm_min = atoi(__TIME__ + 3);
  tmv.tm_sec = atoi(__TIME__ + 6);
  tmv.tm_isdst = -1;
  setenv("TZ", TIMEZONE, 1); tzset();   // the compile time is local time
  time_t t = mktime(&tmv);
  struct timeval tv = { t, 0 };
  settimeofday(&tv, nullptr);
}
static void setClock(int hh, int mm) {
  time_t now = time(nullptr); struct tm tmv; localtime_r(&now, &tmv);
  tmv.tm_hour = hh; tmv.tm_min = mm; tmv.tm_sec = 0;
  struct timeval tv = { mktime(&tmv), 0 }; settimeofday(&tv, nullptr);
  clockIsReal = true;
}
static void onTimeSynced(struct timeval*) { clockIsReal = true; }

// The date line under the clock uses a real font. It is drawn once a minute into a small
// grey-scale mask, then blended onto the face in the eye colour.
static const int DATE_W = 240, DATE_H = 26;
static uint8_t dateMask[DATE_W * DATE_H];
static char dateShown[24] = "";
static void refreshDateMask() {
  static const char* days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
  static const char* mons[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
  time_t now = time(nullptr); struct tm tmv; localtime_r(&now, &tmv);
  char txt[24]; snprintf(txt, sizeof txt, "%s  %d %s", days[tmv.tm_wday], tmv.tm_mday, mons[tmv.tm_mon]);
  if (strcmp(txt, dateShown) == 0) return;
  strcpy(dateShown, txt);
  LGFX_Sprite spr(&tft);
  spr.setColorDepth(16);
  if (!spr.createSprite(DATE_W, DATE_H)) return;
  spr.fillScreen(TFT_BLACK);
  spr.setFont(&fonts::FreeSansBold12pt7b);
  spr.setTextDatum(textdatum_t::top_center);
  spr.setTextColor(TFT_WHITE);
  spr.drawString(txt, DATE_W / 2, 2);
  for (int y = 0; y < DATE_H; y++)
    for (int x = 0; x < DATE_W; x++) {
      uint16_t c = spr.readPixel(x, y);
      dateMask[y * DATE_W + x] = uint8_t(((c >> 5) & 63) * 255 / 63);   // green channel = brightness
    }
  spr.deleteSprite();
}
static void drawDate(face::Canvas& cv, face::RGB colour, float alpha, void*) {
  int ox = 160 - DATE_W / 2, oy = 168;
  for (int y = 0; y < DATE_H; y++)
    for (int x = 0; x < DATE_W; x++) {
      uint8_t m = dateMask[y * DATE_W + x];
      if (m) cv.put(ox + x + int(cv.tx), oy + y + int(cv.ty), colour, alpha * m / 255.f);
    }
}

// LovyanGFX's fonts, handed to the face for words on screen (same data layout)
static_assert(sizeof(face::Glyph) == sizeof(lgfx::GFXglyph), "font glyph layout differs");
static face::Font fontFrom(const lgfx::GFXfont& f) {
  return { f.bitmap, reinterpret_cast<const face::Glyph*>(f.glyph), f.first, f.last, f.yAdvance };
}

// ---------------- drawing one frame ----------------
static float drawCostMs = 0;    // the drawing part of it
static uint32_t profFrames = 0;   // frames since the last "prof reset"
// "bench <command>": runs the command, then times every frame for 6 s and prints the average per half second
static uint32_t benchStart = 0;
static float benchSum[12]; static int benchN[12];
static float frameCostMs = 0;   // drawing + sending one frame, averaged (the "mem" command shows it)
// The frame is drawn in strips of BAND_ROWS rows into two small buffers in fast internal RAM. While one strip goes
// to the screen by DMA, the next is drawn. A whole 150 KB frame in internal RAM left Wi-Fi too little memory, and
// sending one from PSRAM corrupted the picture (stripes in the eyes on the first board).
static uint16_t* bandBuf[2] = { nullptr, nullptr };
static void drawFrame() {
  uint32_t now = millis();
  float dt = float(now - lastFrame);
  lastFrame = now;
  buddy.update(dt);

  bool asleep = buddy.mood() == face::SLEEPY;
  float target = (buddy.faceDown() || (asleep && buddy.resting)) ? 0 : (asleep ? BRIGHTNESS_ASLEEP : BRIGHTNESS_AWAKE);
  brightness += (target - brightness) * 0.08f;
  tft.setBrightness(uint8_t(brightness + 0.5f));
  if (buddy.faceDown() && brightness < 0.5f) return;   // lying on its face with the light off: nothing to draw

  char hhmm[6] = "--:--";
  if (buddy.mood() == face::CLOCK) {
    time_t t = time(nullptr); struct tm tmv; localtime_r(&t, &tmv);
    snprintf(hhmm, sizeof hhmm, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    refreshDateMask();
  }
  uint32_t t0 = micros();
  bool flipped = buddy.upsideDown();
  uint32_t drawUs = 0;
  tft.startWrite();
  for (int k = 0, y0 = 0; y0 < face::H; k++, y0 += BAND_ROWS) {
    int rows = face::H - y0 < BAND_ROWS ? face::H - y0 : BAND_ROWS;
    int sy0 = flipped ? face::H - y0 - rows : y0;          // upside down: draw the mirrored strip, render() turns it round
    uint16_t* buf = bandBuf[k & 1];                         // the other buffer may still be going out by DMA
    canvas.fb = buf - sy0 * face::W;                        // so fb[y * W + x] lands in this strip for its rows
    canvas.bandY0 = sy0; canvas.bandY1 = sy0 + rows;
    uint32_t d0 = micros();
    buddy.render(canvas, hhmm);
    drawUs += micros() - d0;
#if SCREEN_DMA
    tft.waitDMA();                                          // the previous strip has finished sending
    tft.pushImageDMA(0, y0, face::W, rows, (lgfx::swap565_t*)buf);
#else
    tft.pushImage(0, y0, face::W, rows, (lgfx::swap565_t*)buf);
#endif
  }
  tft.endWrite();                                           // waits for the last strip
  profFrames++;
  frameCostMs += ((micros() - t0) / 1000.0f - frameCostMs) * 0.05f;
  drawCostMs += (drawUs / 1000.0f - drawCostMs) * 0.05f;
  if (benchStart) {
    uint32_t e = millis() - benchStart; int w = int(e / 500);
    if (w < 12) { benchSum[w] += drawUs / 1000.0f; benchN[w]++; }
    else {
      Serial.println("bench: drawing ms per frame, each half second after the command:");
      for (int i = 0; i < 12; i++) Serial.printf("  %4.1f-%4.1fs  %5.1f ms  (%d frames)\n", i * 0.5f, i * 0.5f + 0.5f, benchN[i] ? benchSum[i] / benchN[i] : 0.f, benchN[i]);
      benchStart = 0;
    }
  }
}

// ---------------- personality: the bond grows with pets, pokes and pick-ups (saved in flash, see bond.h) ----------------
static float affPoints = 0;
static bool affDirty = false;
static float bondBase = 0;        // bond points the last time you were together (fading starts from here)
static uint32_t seenEpoch = 0;    // real time you were last together, 0 = not known yet
static int levelUpTo = -1;        // a new bond level waiting to be celebrated
static float bondNow() { return fminf(1.f, affPoints / AFFECTION_FULL); }
static void applyBond() {
  float a = bondNow();
  buddy.setAffection(a);
  buddy.pokesToAnnoy = bond::levelOf(a) >= bond::BEST ? 8 : 5;   // a close buddy puts up with more poking
}
static void addAffection(float p) {
  bond::Level before = bond::levelOf(bondNow());
  affPoints += p; affDirty = true;
  bondBase = affPoints;
  applyBond();
  bond::Level after = bond::levelOf(bondNow());
  if (after > before && p > 0) levelUpTo = after;
}
static void saveAffection(uint32_t now, bool force = false) {
  static uint32_t last = 0;
  if (!affDirty || (!force && now - last < 5UL * 60UL * 1000UL)) return;
  last = now; affDirty = false;
  prefs.putFloat("aff", affPoints);
  prefs.putFloat("bbase", bondBase);
  prefs.putUInt("seen", seenEpoch);
}

// ---------------- which way up it is (needs "cal up" and "cal face" once) ----------------
static float dot3(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 cross3(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static Vec3 norm3(Vec3 a) { float l = sqrtf(dot3(a, a)); return l > 1e-6f ? Vec3{ a.x / l, a.y / l, a.z / l } : Vec3{ 0, 0, 0 }; }
static Vec3 calUp = { 0, 0, 0 }, calFace = { 0, 0, 0 }, accF = { 0, 0, 0 };
static Vec3 axisUp, axisBack, axisRight;   // worked out from the two calibration readings
static bool calOk = false, tiltShown = false;
static float tiltX = 0, tiltY = 0;

static void updateAxes() {
  calOk = dot3(calUp, calUp) > 0.25f && dot3(calFace, calFace) > 0.25f && fabsf(dot3(norm3(calUp), norm3(calFace))) < 0.5f;
  if (!calOk) return;
  axisUp = norm3(calUp);
  Vec3 b = norm3(calFace);                                  // gravity's direction when lying on its face
  float k = dot3(b, axisUp);
  axisBack = norm3({ b.x - k * axisUp.x, b.y - k * axisUp.y, b.z - k * axisUp.z });
  axisRight = norm3(cross3(axisBack, axisUp));
}
static void loadCalibration() {
  if (prefs.getBytesLength("calUp") == sizeof(Vec3)) prefs.getBytes("calUp", &calUp, sizeof(Vec3));
  if (prefs.getBytesLength("calFace") == sizeof(Vec3)) prefs.getBytes("calFace", &calFace, sizeof(Vec3));
  updateAxes();
}

static void interacted() { lastInteract = millis(); }

// Someone touched or picked Dewy up. Coming back after a while gets a welcome (see checkBond).
static uint32_t welcomeAt = 0; static float welcomeHours = 0;
static void personHere(uint32_t now) {
  int32_t idle = int32_t(now - lastInteract);
  double hours = idle > 0 ? idle / 3600000.0 : 0;
  if (clockIsReal && seenEpoch) {                      // the real clock also counts time it was switched off
    double realH = (double(time(nullptr)) - double(seenEpoch)) / 3600.0;
    if (realH > hours) hours = realH;
  }
  if (hours * 60 >= BOND_WELCOME_AFTER_MIN && !buddy.resting) { welcomeAt = now + 1700; welcomeHours = float(hours); }
  if (clockIsReal) { seenEpoch = uint32_t(time(nullptr)); bondBase = affPoints; affDirty = true; }
  if (buddy.isMissing()) buddy.setMissing(false);
  interacted();
}

static void pollOrientation(uint32_t now) {
  static uint32_t last = 0, downSince = 0, liftSince = 0, flipSince = 0;
  float a[3];
  if (now - last < 50 || !motion.latest(a)) return;
  last = now;
  Vec3 v = norm3({ a[0], a[1], a[2] });
  if (dot3(accF, accF) < 0.01f) accF = v;
  else accF = norm3({ accF.x + (v.x - accF.x) * 0.25f, accF.y + (v.y - accF.y) * 0.25f, accF.z + (v.z - accF.z) * 0.25f });
  if (!calOk) return;

  float up = dot3(accF, axisUp), back = dot3(accF, axisBack), right = dot3(accF, axisRight);

  // lying on its face for 0.8 s = quiet time; clearly off its face for 0.3 s = awake again
  if (!buddy.faceDown()) {
    if (back > 0.8f) { if (!downSince) downSince = now; else if (now - downSince > 800) { buddy.setFaceDown(true); downSince = 0; Serial.println("[face down] quiet time"); } }
    else downSince = 0;
  } else {
    if (back < 0.5f) { if (!liftSince) liftSince = now; else if (now - liftSince > 300) { buddy.setFaceDown(false); liftSince = 0; interacted(); } }
    else liftSince = 0;
  }
  // upside down for half a second = complain until it's turned back
  if (!buddy.upsideDown()) {
    if (up < -0.7f) { if (!flipSince) flipSince = now; else if (now - flipSince > 500) { buddy.setUpsideDown(true); flipSince = 0; interacted(); } }
    else flipSince = 0;
  } else if (up > -0.3f) { buddy.setUpsideDown(false); interacted(); }

  // leaning: left/right rolls the pupils downhill; back/forward keeps looking at you
  tiltX = -right * TILT_GAIN; tiltY = -back * TILT_GAIN;
  float mag = sqrtf(tiltX * tiltX + tiltY * tiltY);
  if (buddy.faceDown() || buddy.upsideDown()) tiltShown = false;
  else if (mag > TILT_ON) tiltShown = true;
  else if (mag < TILT_OFF) tiltShown = false;
  buddy.setTilt(tiltX, tiltY, tiltShown);
}

// ---------------- Wi-Fi, updates over Wi-Fi, the cloud ----------------
#include "cloud.h"
#include "portal.h"
#include "voice.h"

// The face builds its shapes in Path objects (about 2.6 KB each) on the stack, several per frame, more with the
// weather card or a note. The Arduino default of 8 KB for loop() overflowed on the first real board, so give it 32 KB.
#ifdef SET_LOOP_TASK_STACK_SIZE
SET_LOOP_TASK_STACK_SIZE(32 * 1024);
#endif

static bool otaOn = false;
static uint32_t wifiStartAt = 0;
static char wifiSsid[33] = "", wifiPass[64] = "";

static void startOta() {
  if (!strlen(OTA_PASSWORD)) { Serial.println("Updates over Wi-Fi from the Arduino IDE are off: set OTA_PASSWORD in secrets.h"); return; }
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { buddy.setUpdateProgress(0); drawFrame(); });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last > 120) { last = now; buddy.setUpdateProgress(total ? float(done) / float(total) : 0); drawFrame(); }
  });
  ArduinoOTA.onEnd([]() { buddy.setUpdateProgress(1); drawFrame(); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("Update failed (error %u)\n", unsigned(e)); buddy.setUpdateProgress(-1); buddy.reset(); });
  ArduinoOTA.begin();
  otaOn = true;
  Serial.printf("Updates over Wi-Fi from the Arduino IDE: on, as \"%s\"\n", OTA_HOSTNAME);
}

// Wi-Fi saved from the setup page wins over secrets.h
static void loadWifiCreds() {
  String ss = prefs.getString("wifi_ssid", ""), pw = prefs.getString("wifi_pass", "");
  if (ss.length()) { strlcpy(wifiSsid, ss.c_str(), sizeof wifiSsid); strlcpy(wifiPass, pw.c_str(), sizeof wifiPass); }
  else { strlcpy(wifiSsid, WIFI_SSID, sizeof wifiSsid); strlcpy(wifiPass, WIFI_PASSWORD, sizeof wifiPass); }
}

static void startWifi() {
  loadWifiCreds();
  WiFi.setHostname(OTA_HOSTNAME);   // before mode(), or some core versions ignore it
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  sntp_set_time_sync_notification_cb(onTimeSynced);
  if (wifiSsid[0]) { WiFi.begin(wifiSsid, wifiPass); Serial.printf("Connecting to Wi-Fi \"%s\"...\n", wifiSsid); }
  else if (SETUP_HOTSPOT) startPortal();
  else Serial.println("Wi-Fi is off (no WIFI_SSID in secrets.h)");
  wifiStartAt = millis();
  startNetTask();
}

static void pollWifi(uint32_t now) {
  static bool was = false;
  static uint32_t connectedAt = 0;
  bool on = WiFi.status() == WL_CONNECTED;
  if (on && !was) {
    Serial.printf("Wi-Fi connected, IP %s\n", WiFi.localIP().toString().c_str());
    configTzTime(S.tz, "pool.ntp.org", "time.google.com");
    if (!otaOn) startOta();
    connectedAt = now;
  }
  if (!on && was) Serial.println("Wi-Fi lost, reconnecting...");
  was = on;
  if (otaOn) ArduinoOTA.handle();
  // close the setup hotspot a few seconds after Wi-Fi works; open it if the saved Wi-Fi can't be found
  if (portalOn && on && now - connectedAt > 4000) stopPortal();
  if (SETUP_HOTSPOT && !portalOn && !on && now - wifiStartAt > 120000UL && (wifiSsid[0] || prefs.getString("wifi_ssid", "").length() == 0)) {
    Serial.println("[wifi] can't find the saved Wi-Fi; opening the setup hotspot too");
    startPortal();
  }
  pollPortal();
}

// ---------------- what comes from the phone app ----------------
static char noteQ[5][128], noteQFrom[5][24];
static int noteQn = 0;
static BannerKind banner = BANNER_NONE;
static uint32_t bannerHiddenUntil = 0;   // a tap hides the set-up card for a while
static bool paired = true, pairedKnown = false, pairRequested = false;
static char pairCode[12] = "";

static void applySettings() {
  setenv("TZ", S.tz, 1); tzset();
  cityLat = S.lat; cityLon = S.lon; cityOn = S.hasCity;
}
static void loadSavedSettings() {
  settingsDefaults();
  String js = prefs.getString("settings", "");
  if (js.length() && settingsFromJson(js.c_str())) Serial.println("Settings from the phone app loaded");
  applySettings();
}

static void showNextNote() {
  if (!noteQn) return;
  bool awake = buddy.mood() != face::SLEEPY && !buddy.faceDown();
  // asleep: hand one note over to keep for waking up; awake: show the next once the screen is free
  if ((!awake && !buddy.hasPendingNote()) || (awake && !buddy.overlayActive() && !buddy.busy())) {
    buddy.showNote(noteQ[0], noteQFrom[0]);
    for (int i = 1; i < noteQn; i++) { memcpy(noteQ[i - 1], noteQ[i], 128); memcpy(noteQFrom[i - 1], noteQFrom[i], 24); }
    noteQn--;
  }
}

static void setBanner(BannerKind k) {
  if (k == banner) return;
  banner = k;
  char code[12];
  if (k == BANNER_SETUP) buddy.showBanner("Set up my Wi-Fi", "Join this Wi-Fi on your phone", SETUP_AP_NAME);
  else if (k == BANNER_PAIR) { snprintf(code, sizeof code, "%.4s %.4s", pairCode, pairCode + 4); buddy.showBanner("Pair your phone", "Type it in the Dewy app", code); }
  else buddy.clearBanner();
}

static void pollCloud(uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 100) return;
  last = now;
  NetInbox in;
  bool gotSettings = false; int nev = 0; NetEvent ev[10]; int weather = -1;
  if (netLock) {
    NetLock lock;
    gotSettings = inbox.hasSettings;
    if (gotSettings) { memcpy(in.settingsJson, inbox.settingsJson, sizeof in.settingsJson); inbox.hasSettings = false; }
    nev = inbox.nev; memcpy(ev, inbox.ev, sizeof ev); inbox.nev = 0;
    pairedKnown = inbox.pairedKnown; paired = inbox.paired;
    strlcpy(pairCode, inbox.pairCode, sizeof pairCode);
    if (!pairCode[0] && inbox.pairedKnown) pairRequested = false;   // used up or run out
    weather = inbox.weather; inbox.weather = -1;
  }
  if (gotSettings && settingsFromJson(in.settingsJson)) {
    prefs.putString("settings", in.settingsJson);
    applySettings();
    Serial.println("[cloud] new settings from the phone app");
  }
  for (int i = 0; i < nev; i++) {
    NetEvent& e = ev[i];
    if (!strcmp(e.kind, "note")) {
      if (noteQn < 5) { strlcpy(noteQ[noteQn], e.text, 128); strlcpy(noteQFrom[noteQn], e.from, 24); noteQn++; }
      Serial.printf("[note] from %s: %s\n", e.from, e.text);
    } else if (!strcmp(e.kind, "timer_start")) {
      buddy.startTimer(uint32_t(e.minutes > 0 ? e.minutes : S.study)); interacted();
      Serial.printf("[timer] %d min from the phone\n", e.minutes);
    } else if (!strcmp(e.kind, "timer_stop")) {
      buddy.stopTimer(); Serial.println("[timer] stopped from the phone");
    }
    ackId = e.id;
  }
  if (weather >= 0) { buddy.setWeather(face::Weather(weather)); Serial.printf("[weather] %s\n", face::WEATHER_NAMES[weather]); }

  // set-up cards: Wi-Fi first, then pairing (decided before notes, so a note never hides under a card)
  bool hidden = int32_t(bannerHiddenUntil - now) > 0;
  if (portalOn && !hidden) setBanner(BANNER_SETUP);
  else if (!portalOn && cloudOn() && pairedKnown && (!paired || pairRequested) && pairCode[0] && !hidden) setBanner(BANNER_PAIR);
  else setBanner(BANNER_NONE);
  if (banner == BANNER_PAIR) {   // the code may have been renewed
    static char shown[12] = "";
    if (strcmp(shown, pairCode)) { strcpy(shown, pairCode); banner = BANNER_NONE; setBanner(BANNER_PAIR); }
  }
  showNextNote();

  // firmware updates: only when nothing important is on screen
  face::Mood m = buddy.mood();
  updateAllowed = !buddy.timerActive() && !buddy.overlayActive() && !buddy.busy() && (m == face::HAPPY || m == face::SLEEPY || m == face::BORED)
                  && vState == V_IDLE && !vRequested;      // never in the middle of a conversation: the restart would cut it off
  static bool updating = false;
  float p = fwProgress;
  if (p >= 0) { buddy.setUpdateProgress(p); updating = true; }
  else if (updating) { buddy.setUpdateProgress(-1); buddy.reset(); updating = false; }
  dewyAsleep = m == face::SLEEPY;
}

// ---------------- the alarm: rings until you stop it; snooze rings again later ----------------
// Snooze: say "snooze", tap the screen, or shake or lift Dewy. Stop: say "stop", or hold a finger on the screen.
// With no speaker (or before it's wired) the alarm is just the face, and the touches still work.
static bool ringing = false;
static uint32_t ringSince = 0, snoozeAt = 0;
static void alarmStart() {
  ringing = true; ringSince = millis(); alarmHeard = ALARM_NONE;
  alarmRinging = vReady;                                     // the voice task beeps and listens
  buddy.setMood(face::ALARM); interacted();
  Serial.printf("[alarm] %s\n", buddy.alarmText);
}
static void alarmSnooze() {
  if (!ringing) return;
  ringing = false; alarmRinging = false;
  int mins = S.snoozeMin >= 1 && S.snoozeMin <= 30 ? S.snoozeMin : 9;
  snoozeAt = millis() + uint32_t(mins) * 60000UL; if (!snoozeAt) snoozeAt = 1;
  buddy.setMood(face::HAPPY);
  static char msg[24];
  snprintf(msg, sizeof msg, "Snoozing %d min", mins);
  buddy.bondMoment(msg, 0, 2500);
  Serial.printf("[alarm] snoozed for %d minutes\n", mins);
}
static void alarmStop(bool timedOut = false) {
  if (!ringing && !snoozeAt) return;
  bool wasRinging = ringing;
  ringing = false; alarmRinging = false; snoozeAt = 0;
  buddy.setMood(face::HAPPY);
  time_t tnow = time(nullptr); struct tm tmv; localtime_r(&tnow, &tmv);
  bool morning = clockIsReal && tmv.tm_hour >= 4 && tmv.tm_hour < 12;
  if (wasRinging && !timedOut) buddy.bondMoment(morning ? "Good morning!" : "Alarm off", morning ? 3 : 0, 3000);
  Serial.println(timedOut ? "[alarm] stopped by itself after ringing a while" : "[alarm] stopped");
}
static void checkAlarmRing(uint32_t now) {
  if (snoozeAt && !ringing && int32_t(now - snoozeAt) >= 0) { snoozeAt = 0; alarmStart(); }
  if (!ringing) return;
  int heard = alarmHeard;
  if (heard) { alarmHeard = ALARM_NONE; if (heard == ALARM_SNOOZE) alarmSnooze(); else alarmStop(); return; }
  if (buddy.mood() != face::ALARM && !buddy.faceDown()) buddy.setMood(face::ALARM);   // nothing else may take the face while it rings
  if (now - ringSince > ALARM_RING_MAX_MS) alarmStop(true);
}

// ---------------- alarms, birthdays and reminders ----------------
static void checkCalendar(uint32_t now) {
  static uint32_t last = 0, lastWater = 0, lastStretch = 0;
  static int lastAlarmKey = -1;
  if (now - last < 15000) return;
  last = now;
  if (!lastWater) { lastWater = now; lastStretch = now; }
  time_t tt = time(nullptr); struct tm tmv; localtime_r(&tt, &tmv);
  int today = (tmv.tm_year + 1900) * 10000 + (tmv.tm_mon + 1) * 100 + tmv.tm_mday;

  // alarms (not while it lies on its face: that's quiet time)
  for (int i = 0; i < S.nAlarms; i++) {
    const DewyAlarm& a = S.alarms[i];
    bool dayOk = a.dayMask == 0 || (a.dayMask & (1 << tmv.tm_wday));
    int key = today * 100 + i;
    if (a.on && dayOk && tmv.tm_hour == a.h && tmv.tm_min == a.m && lastAlarmKey != key && !buddy.faceDown()) {
      lastAlarmKey = key;
      static char alarmText[8];
      snprintf(alarmText, sizeof alarmText, "%02d:%02d", a.h, a.m);
      buddy.alarmText = alarmText;
      snoozeAt = 0;                                          // a new alarm replaces a snoozed one
      alarmStart();
    }
  }

  bool freeNow = buddy.mood() == face::HAPPY && !buddy.faceDown() && !buddy.upsideDown() && !buddy.busy() && !buddy.overlayActive();

  // reminders, only during the day
  bool day = tmv.tm_hour >= S.from && tmv.tm_hour < S.to && !buddy.resting;   // switched off: no nudges
  if (S.water > 0 && now - lastWater > S.water * 60000UL) {
    if (!day || buddy.mood() == face::SLEEPY || buddy.faceDown()) lastWater = now;   // skipped
    else if (freeNow) { lastWater = now; buddy.remind(face::REMIND_WATER); Serial.println("[remind] water"); }
  }
  if (S.stretch > 0 && now - lastStretch > S.stretch * 60000UL) {
    if (!day || buddy.mood() == face::SLEEPY || buddy.faceDown()) lastStretch = now;
    else if (freeNow) { lastStretch = now; buddy.remind(face::REMIND_STRETCH); Serial.println("[remind] stretch"); }
  }

  // birthdays: hers, and the buddy's own (the day it was given, from the next year on).
  // Only with a real date from the internet, the first time it's awake after 8 am, once a day.
  if (!clockIsReal || tmv.tm_hour < 8 || !freeNow) return;
  bool hers = S.bdM > 0 && tmv.tm_mon + 1 == S.bdM && tmv.tm_mday == S.bdD;
  bool own = S.gD > 0 && tmv.tm_mon + 1 == S.gM && tmv.tm_mday == S.gD && tmv.tm_year + 1900 > S.gY;
  if ((hers || own) && prefs.getUInt("bday", 0) != uint32_t(today)) {
    prefs.putUInt("bday", uint32_t(today));
    buddy.birthday(!hers); interacted();
    Serial.println(hers ? "[birthday] hers" : "[birthday] the buddy's own");
  }
}

// ---------------- battery ----------------
static int simBattery = -1;   // set by the "battery" test command
static void pollBattery(uint32_t now) {
#if BATTERY_ENABLED
  static uint32_t last = 0;
  static float mv = -1;
  static bool slept = false;
  if (simBattery >= 0 || (last && now - last < 5000)) return;
  last = now;
  float v = analogReadMilliVolts(PIN_BATTERY) * 2.0f;   // two 100k resistors halve the voltage
  mv = mv < 0 ? v : mv + (v - mv) * 0.3f;
  int pct = int(face::clampf((mv - BATTERY_EMPTY_MV) / float(BATTERY_FULL_MV - BATTERY_EMPTY_MV) * 100.f, 0, 100) + 0.5f);
  buddy.setBattery(pct);
  if (pct <= 5 && !slept && buddy.mood() != face::SLEEPY) { buddy.sleep(); slept = true; }
  if (pct > 10) slept = false;
#else
  (void)now;
#endif
}

// ---------------- input ----------------
static void printMood(face::Mood m, void*) {
  Serial.printf("[mood] %s, eyes: %s, %s pupils\n", face::MOOD_NAMES[m], buddy.palette().name, buddy.shapeName());
}

static void printHelp() {
  Serial.println("Moods:    shake, pick, name, listen, think, talk, pet, poke, ignore, bored, sleep, wake,");
  Serial.println("Alarm:    alarm (ring now), snooze, alarm stop. Ringing: say snooze or stop, tap = snooze, hold = stop");
  Serial.println("          alarm, time, happy, annoyed, colour, cat, acorn, mouth, settime HH:MM");
  Serial.println("Features: 8ball, weather sun|cloud|rain|off|show, timer [minutes] | timer stop,");
  Serial.println("          remind water|stretch, note <text>, bday [own], battery <0-100>|auto,");
  Serial.println("          affection <0-1>, tilt <x> <y>|off, facedown on|off, upside on|off");
  Serial.println("System:   mem (free RAM and how long a frame takes), bench <command> (frame times after it)");
  Serial.println("Bond:     bond (level and progress), rest on|off (as if switched off in the app), miss on|off");
  Serial.println("Setup:    cal up, cal face, axes, wifi, wifi reset, setup, pair");
}

static void handleCommand(String raw) {
  raw.trim();
  String cmd = raw; cmd.toLowerCase();
  if (!cmd.length()) return;
  interacted();
  if (cmd.startsWith("bench ")) {
    for (int i = 0; i < 12; i++) { benchSum[i] = 0; benchN[i] = 0; }
    handleCommand(raw.substring(6, raw.length()));
    benchStart = millis() | 1;
    Serial.println("bench: timing for 6 s...");
    return;
  }
  if (cmd == "shake") buddy.shake();
  else if (cmd == "pick" || cmd == "pickup") buddy.pickUp();
  else if (cmd == "name") buddy.sayName();
  else if (cmd == "listen") buddy.listen();
  else if (cmd == "think") buddy.think();
  else if (cmd == "talk") buddy.talk();
  else if (cmd == "ask") { if (!voiceRequest()) Serial.println("[voice] cannot start: needs Wi-Fi, a server, mic and speaker, and no alarm ringing"); }
  else if (cmd == "mic") { micTest = !micTest; Serial.printf("mic test %s\n", micTest ? "on" : "off"); }
  else if (wakeCommand(cmd)) {}
  else if (cmd == "pet") { buddy.pet(); addAffection(1); }
  else if (cmd == "poke") buddy.poke();
  else if (cmd == "ignore") { face::Face::Step s[] = { { face::BORED, 5000 }, { face::SLEEPY, 0 } }; buddy.play(s, 2); }
  else if (cmd == "bored") buddy.bored();
  else if (cmd == "sleep") buddy.sleep();
  else if (cmd == "wake") buddy.wake();
  else if (cmd == "alarm") alarmStart();
  else if (cmd == "snooze") alarmSnooze();
  else if (cmd == "alarm stop") alarmStop();
  else if (cmd == "time") buddy.showTime();
  else if (cmd == "happy" || cmd == "reset") buddy.reset();
  else if (cmd == "annoyed") buddy.setMood(face::ANNOYED);
  else if (cmd == "colour" || cmd == "color") { buddy.nextPalette(); Serial.printf("eyes: %s\n", buddy.palette().name); }
  else if (cmd == "cat") { buddy.setCat(!buddy.isCat()); Serial.printf("pupils: %s\n", buddy.shapeName()); }
  else if (cmd == "acorn") { buddy.setAcorn(!buddy.isAcorn()); Serial.printf("pupils: %s\n", buddy.shapeName()); }
  else if (cmd == "mouth") { buddy.mouth = !buddy.mouth; Serial.printf("mouth %s\n", buddy.mouth ? "on" : "off"); }
  else if (cmd.startsWith("settime ") && cmd.length() >= 13) {
    int hh = cmd.substring(8, 10).toInt(), mm = cmd.substring(11, 13).toInt();
    setClock(hh, mm); Serial.printf("clock set to %02d:%02d\n", hh, mm);
  }
  // ---- features
  else if (cmd == "8ball") { buddy.eightBall(); Serial.printf("[8-ball] %s\n", buddy.eightBallAnswer()); }
  else if (cmd.startsWith("weather")) {
    String w = cmd.substring(7, cmd.length()); w.trim();
    if (w == "sun") buddy.setWeather(face::WEATHER_SUN);
    else if (w == "cloud") buddy.setWeather(face::WEATHER_CLOUD);
    else if (w == "rain") buddy.setWeather(face::WEATHER_RAIN);
    else if (w == "off") buddy.setWeather(face::WEATHER_NONE);
    buddy.showWeather();
    Serial.printf("weather: %s\n", face::WEATHER_NAMES[buddy.weather()]);
  }
  else if (cmd == "timer stop") { buddy.stopTimer(); Serial.println("timer stopped"); }
  else if (cmd.startsWith("timer")) {
    long m = cmd.length() > 6 ? cmd.substring(6, cmd.length()).toInt() : 0;
    if (m <= 0) m = STUDY_MINUTES;
    buddy.startTimer(uint32_t(m)); Serial.printf("study timer: %ld min\n", m);
  }
  else if (cmd == "remind water") Serial.println(buddy.remind(face::REMIND_WATER) ? "water reminder" : "skipped (asleep or face down)");
  else if (cmd == "remind stretch") Serial.println(buddy.remind(face::REMIND_STRETCH) ? "stretch reminder" : "skipped (asleep or face down)");
  else if (cmd.startsWith("note ")) {
    String text = raw.substring(5, raw.length());
    buddy.showNote(text.c_str());
    Serial.println(buddy.hasPendingNote() ? "note saved for when it wakes up" : "note shown");
  }
  else if (cmd == "bday" || cmd == "bday own") buddy.birthday(cmd == "bday own");
  else if (cmd.startsWith("battery")) {
    String v = cmd.substring(7, cmd.length()); v.trim();
    if (v == "auto") { simBattery = -1; buddy.setBattery(-1); Serial.println("battery: measured again"); }
    else { simBattery = int(v.toInt()); buddy.setBattery(simBattery); if (simBattery <= 5) buddy.sleep(); Serial.printf("battery: pretending %d%%\n", simBattery); }
  }
  else if (cmd == "prof reset") { for (auto& u : face::profUs) u = 0; profFrames = 0; Serial.println("prof: counting again"); }
  else if (cmd == "prof") {
    if (!profFrames) { Serial.println("prof: no frames yet"); }
    else {
      static const char* nm[10] = { "blush", "eyeL", "eyeR", "arcs", "glasses", "effects", "mouth", "overlays", "flip", "clear" };
      Serial.printf("prof over %u frames, ms per frame:", unsigned(profFrames));
      for (int i = 0; i < 10; i++) Serial.printf(" %s %.1f", nm[i], face::profUs[i] / 1000.0f / profFrames);
      Serial.println("");
    }
  }
  else if (cmd == "mem") {
    Serial.printf("internal RAM free %u (largest block %u), PSRAM free %u, frame takes %.1f ms of %d ms (drawing %.1f, sending %.1f; %d-row strips, %s)\n",
                  unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)), unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                  unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)), frameCostMs, FRAME_MS, drawCostMs, frameCostMs - drawCostMs, BAND_ROWS, SCREEN_PARALLEL ? "parallel screen" : "SPI screen");
#ifdef ESP_PLATFORM
    Serial.printf("loop stack: %u bytes never used (of 32768)\n", unsigned(uxTaskGetStackHighWaterMark(nullptr)));
#if defined(__OPTIMIZE_SIZE__)
    Serial.println("built for size (-Os)");
#elif defined(__OPTIMIZE__)
    Serial.println("built for speed (-O2)");
#else
    Serial.println("built without optimisation");
#endif
#endif
  }
  else if (cmd == "bond") {
    float a = bondNow();
    Serial.printf("bond %.2f: %s, %d%% of the way to the next level, %u pts (base %.1f), last together %u\n",
                  a, bond::NAMES[bond::levelOf(a)], int(bond::progressIn(a) * 100), unsigned(affPoints), bondBase, unsigned(seenEpoch));
  }
  else if (cmd == "rest on" || cmd == "rest off") { S.powerOn = cmd == "rest off"; Serial.println(S.powerOn ? "switched on (until the app changes a setting)" : "resting (until the app changes a setting)"); }
  else if (cmd == "miss on" || cmd == "miss off") buddy.setMissing(cmd == "miss on");
  else if (cmd.startsWith("affection ")) {
    float a = face::clampf(cmd.substring(10, cmd.length()).toFloat(), 0, 1);
    affPoints = a * AFFECTION_FULL; addAffection(0); saveAffection(millis(), true);
    Serial.printf("affection %.2f\n", a);
  }
  else if (cmd.startsWith("tilt")) {
    float x = 0, y = 0;
    if (cmd == "tilt off") buddy.setTilt(0, 0, false);
    else if (sscanf(cmd.c_str() + 4, "%f %f", &x, &y) == 2) buddy.setTilt(x, y, true);
    Serial.println(calOk ? "tilt set (the motion sensor takes over again within a moment)" : "tilt set");
  }
  else if (cmd == "facedown on" || cmd == "facedown off") buddy.setFaceDown(cmd == "facedown on");
  else if (cmd == "upside on" || cmd == "upside off") buddy.setUpsideDown(cmd == "upside on");
  // ---- setup
  else if (cmd == "cal up" || cmd == "cal face") {
    if (!motion.available() || dot3(accF, accF) < 0.5f) Serial.println("No motion sensor reading yet");
    else {
      bool isUp = cmd == "cal up";
      (isUp ? calUp : calFace) = accF;
      prefs.putBytes(isUp ? "calUp" : "calFace", &accF, sizeof(Vec3));
      updateAxes();
      Serial.printf("saved \"%s\" = (%.2f, %.2f, %.2f). %s\n", isUp ? "up" : "face", accF.x, accF.y, accF.z,
                    calOk ? "Calibration done: leaning, face down and upside down are on." :
                    (isUp ? "Now lay it on its face and type: cal face" : "Now stand it upright and type: cal up"));
    }
  }
  else if (cmd == "axes") {
    Serial.printf("gravity (%.2f, %.2f, %.2f)", accF.x, accF.y, accF.z);
    if (calOk) Serial.printf("  up %.2f  back %.2f  right %.2f  tilt %.2f %.2f%s\n", dot3(accF, axisUp), dot3(accF, axisBack), dot3(accF, axisRight), tiltX, tiltY, tiltShown ? " (showing)" : "");
    else Serial.println("  not calibrated: type cal up, then cal face");
  }
  else if (cmd == "wifi") {
    Serial.printf("Wi-Fi \"%s\" %s, setup hotspot %s, clock %s, IDE updates %s, weather %s\n", wifiSsid, WiFi.status() == WL_CONNECTED ? "connected" : "not connected",
                  portalOn ? "open" : "closed", clockIsReal ? "from the internet" : "from the upload time", otaOn ? "on" : "off", face::WEATHER_NAMES[buddy.weather()]);
    Serial.printf("Cloud: %s, %s, %s\n", cloudOn() ? DEWY_SERVER : "off (no DEWY_SERVER in secrets.h)", cloudReady ? "connected" : "not connected yet",
                  !pairedKnown ? "pairing unknown" : paired ? "paired with a phone" : "no phone paired yet");
  }
  else if (cmd == "wifi reset") {
    prefs.remove("wifi_ssid"); prefs.remove("wifi_pass");
    Serial.println("Saved Wi-Fi forgotten. Restarting into the setup hotspot...");
    delay(500); ESP.restart();
  }
  else if (cmd == "setup") { startPortal(); bannerHiddenUntil = 0; }
  else if (cmd == "pair") {
    if (!cloudOn()) Serial.println("The cloud is off: set DEWY_SERVER in secrets.h");
    else { wantPair = true; pairRequested = true; bannerHiddenUntil = 0; Serial.println("Asking the server for a pairing code..."); }
  }
  else printHelp();
}

static void pollSerial() {
  static String line;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') { handleCommand(line); line = ""; }
    else if (line.length() < 160) line += ch;
  }
}

static void pollTouch(uint32_t now) {
#if TOUCH_ENABLED
  static bool touching = false, petting = false, swallow = false;
  static uint32_t downAt = 0, lastSeen = 0;
  if (buddy.faceDown()) return;
  int32_t x, y;
  bool down = tft.getTouch(&x, &y) > 0;
  if (down) {
    lastSeen = now;
    if (!touching) { touching = true; petting = false; downAt = now; personHere(now); }
    else if (ringing && now - downAt > ALARM_HOLD_STOP_MS) { swallow = true; alarmStop(); return; }   // hold = stop the alarm
    else if (!petting && now - downAt > PET_HOLD_MS && !ringing && buddy.mood() != face::ALARM) { petting = true; buddy.petStart(); addAffection(1); interacted(); }
  } else if (touching && now - lastSeen > 60) {        // released (with a little debounce)
    touching = false;
    interacted();
    if (swallow) { swallow = false; return; }               // the hold that stopped the alarm: not a tap
    if (petting) buddy.petEnd();
    else if (ringing) alarmSnooze();                        // a tap while it rings = snooze
    else if (lastSeen - downAt < TAP_MAX_MS) {
      if (buddy.bannerShown()) { bannerHiddenUntil = now + 30UL * 60UL * 1000UL; setBanner(BANNER_NONE); return; }   // a tap hides the set-up card
      static uint32_t noVoiceUntil = 0;                       // after a poking burst, taps don't start a conversation for a while
      bool pokingBurst = noVoiceUntil && int32_t(noVoiceUntil - now) > 0;
      if (lastTapAt && now - lastTapAt < 450 && !pokingBurst && voiceRequest()) {   // double-tap = talk to Dewy
        lastTapAt = 0; vTapAt = now; return;
      }
      // A third quick tap right after a double-tap is someone poking Dewy, not asking it something. Drop the
      // conversation and count the tap it swallowed as a poke too, so five quick pokes still make it annoyed.
      if (vTapAt && now - vTapAt < 700 && !vByWake && (vState == V_LISTEN || (vState == V_IDLE && vRequested))) {
        vCancel = true; buddy.poke(); noVoiceUntil = now + 1500;
      }
      vTapAt = 0;
      buddy.poke(); addAffection(0.2f); lastTapAt = now;
    }
  }
#else
  (void)now;
#endif
}

static void pollMotion(uint32_t now) {
  Motion::Event e = motion.poll(now);
  if (e == Motion::NONE || buddy.faceDown()) return;        // lying on its face: ignore bumps
  interacted();
  if (ringing) { alarmSnooze(); return; }                   // shake or lift to snooze
  if (e == Motion::SHAKE && lastTapAt && now - lastTapAt < EIGHT_BALL_WINDOW_MS) {
    lastTapAt = 0; buddy.eightBall();
    Serial.printf("[8-ball] %s\n", buddy.eightBallAnswer());
    return;
  }
  if (e == Motion::SHAKE) buddy.shake();
  else { personHere(now); buddy.pickUp(); addAffection(0.1f); }   // also wakes it up if asleep
}

static void checkIdle(uint32_t now) {
  if (buddy.faceDown() || buddy.upsideDown()) return;
  // lastInteract can be a millisecond newer than "now" (set later in the same loop), so don't let it wrap around
  int32_t sinceTouch = int32_t(now - lastInteract);
  uint32_t idle = sinceTouch > 0 ? uint32_t(sinceTouch) : 0;
  bool hold = buddy.busy() || buddy.overlayActive() || buddy.timerActive();
  if (buddy.mood() == face::HAPPY && idle > BORED_AFTER_MS && !hold) buddy.bored();
  else if (buddy.mood() == face::BORED && idle > SLEEP_AFTER_MS && !hold) buddy.sleep();
}

// ---------------- the bond over time: welcomes, new levels, missing you, fading after days apart ----------------
static void checkBond(uint32_t now) {
  bool free = !buddy.busy() && !buddy.overlayActive() && !buddy.faceDown() && !buddy.upsideDown() &&
              buddy.mood() != face::ALARM && vState == V_IDLE && !vRequested;
  bond::Level lvl = bond::levelOf(bondNow());

  if (welcomeAt && int32_t(now - welcomeAt) >= 0) {
    if (free) {
      welcomeAt = 0;
      int hearts = bond::welcomeHearts(lvl, welcomeHours);
      const char* msg = bond::welcomeText(lvl, welcomeHours);
      if (hearts > 0 || msg[0]) { buddy.bondMoment(msg, hearts, welcomeHours >= 24 ? 6500 : 4500); Serial.printf("[bond] welcome back (%.1f h away)\n", welcomeHours); }
    } else if (int32_t(now - welcomeAt) > 15000) welcomeAt = 0;   // too busy for too long: skip it
  }
  if (levelUpTo >= 0 && free && !welcomeAt) {
    buddy.bondMoment(bond::levelUpText(bond::Level(levelUpTo)), 6 + levelUpTo / 2, 6000);
    Serial.printf("[bond] now %s\n", bond::NAMES[levelUpTo]);
    levelUpTo = -1;
  }

  static uint32_t last = 0;
  if (now - last < 60000 && last) return;
  last = now;
  bondReport = int(bondNow() * 1000 + 0.5f);

  // missing you after a long time with nobody around (not while switched off)
  int32_t idle = int32_t(now - lastInteract);
  bool miss = !buddy.resting && lvl >= bond::FRIEND && idle > int32_t(BOND_MISS_AFTER_H * 3600000UL);
  if (miss != buddy.isMissing()) buddy.setMissing(miss);

  // fading after days apart, counted on the real clock. Paused while switched off in the app.
  if (!clockIsReal) return;
  uint32_t nowEpoch = uint32_t(time(nullptr));
  if (!seenEpoch || buddy.resting) { seenEpoch = nowEpoch; bondBase = affPoints; affDirty = true; return; }
  double days = (double(nowEpoch) - double(seenEpoch)) / 86400.0;
  float f = bond::faded(bondBase, AFFECTION_FULL, days, BOND_GRACE_DAYS, BOND_FADE_PER_DAY);
  if (f < affPoints - 0.01f) { affPoints = f; applyBond(); affDirty = true; Serial.printf("[bond] faded to %.2f after %.1f days apart\n", bondNow(), days); }
}

// ---------------- switched off in the app: rest until switched on again ----------------
static void checkRest(uint32_t now) {
  bool rest = !S.powerOn;
  if (rest != buddy.resting) {
    buddy.resting = rest; dewyResting = rest;
    if (rest) { buddy.setMissing(false); welcomeAt = 0; if (buddy.mood() != face::ALARM) buddy.sleep(); Serial.println("[power] switched off in the app: resting"); }
    else { buddy.wake(); interacted(); Serial.println("[power] switched on in the app"); }
    return;
  }
  if (!rest || buddy.mood() == face::SLEEPY || buddy.mood() == face::ALARM) return;
  bool hold = buddy.busy() || buddy.overlayActive() || buddy.timerActive() || vState != V_IDLE;
  if (!hold && int32_t(now - lastInteract) > int32_t(REST_AWAKE_MS)) buddy.sleep();   // back to sleep after a touch
}

// ---------------- setup and loop ----------------
void setup() {
  face::profClock = []() -> uint32_t { return micros(); };   // lets the "prof" command time each part of the face
  Serial.begin(115200);
  delay(200);
  Serial.println("\nDewy starting");

  tft.init();
  tft.setRotation(SCREEN_ROTATION);
  tft.setBrightness(0);
  tft.fillScreen(TFT_BLACK);

  // two strips of BAND_ROWS x 320 pixels in internal, DMA-capable RAM (see drawFrame)
  for (int i = 0; i < 2; i++) {
    bandBuf[i] = (uint16_t*)heap_caps_malloc(size_t(face::W) * BAND_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!bandBuf[i]) { Serial.println("Not enough memory for the screen strips: lower BAND_ROWS in config.h"); while (true) delay(1000); }
  }
  canvas.fb = bandBuf[0];

  prefs.begin("buddy", false);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  if (motion.begin(Wire)) Serial.printf("Motion sensor found (WHO_AM_I 0x%02X)\n", motion.whoAmI());
  else Serial.println("Motion sensor not found: check SDA 4, SCL 5, 3.3V and GND. The face still runs.");
  loadCalibration();
  if (motion.available() && !calOk) Serial.println("Leaning, face down and upside down need a one-time calibration: type cal up, then cal face");

  setClockFromCompileTime();
  loadSavedSettings();
  refreshDateMask();

  font18 = fontFrom(fonts::FreeSansBold18pt7b);
  font24 = fontFrom(fonts::FreeSansBold24pt7b);
  buddy.font18 = &font18;
  buddy.font24 = &font24;
  buddy.noteFrom = NOTE_FROM;
  buddy.mouth = SHOW_MOUTH;
  buddy.catChancePct = CAT_CHANCE_PCT;
  buddy.acornChancePct = ACORN_CHANCE_PCT;
  buddy.alarmText = strlen(ALARM_TIME) == 5 ? ALARM_TIME : "07:00";
  buddy.drawDate = drawDate;
  buddy.onMood = printMood;
  buddy.begin(esp_random());
  affPoints = prefs.getFloat("aff", 0);
  bondBase = prefs.getFloat("bbase", affPoints);
  seenEpoch = prefs.getUInt("seen", 0);
  applyBond(); affDirty = false;

  startWifi();
  voiceBegin();
  lastInteract = lastFrame = millis();
  Serial.println("Type \"help\" for test commands.");
}

void loop() {
  uint32_t now = millis();
  pollSerial();
  pollTouch(now);
  voicePoll(now);
  pollMotion(now);
  pollOrientation(now);
  pollWifi(now);
  pollCloud(now);
  pollBattery(now);
  checkCalendar(now);
  checkIdle(now);
  checkAlarmRing(now);
  checkRest(now);
  checkBond(now);
  saveAffection(now);
  if (now - lastFrame >= FRAME_MS) drawFrame();
}
