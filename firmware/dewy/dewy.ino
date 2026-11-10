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
#include "config.h"
#include "display.h"
#include "face.h"
#include "motion.h"

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
static void drawFrame() {
  uint32_t now = millis();
  float dt = float(now - lastFrame);
  lastFrame = now;
  buddy.update(dt);

  float target = buddy.faceDown() ? 0 : (buddy.mood() == face::SLEEPY ? BRIGHTNESS_ASLEEP : BRIGHTNESS_AWAKE);
  brightness += (target - brightness) * 0.08f;
  tft.setBrightness(uint8_t(brightness + 0.5f));
  if (buddy.faceDown() && brightness < 0.5f) return;   // lying on its face with the light off: nothing to draw

  char hhmm[6] = "--:--";
  if (buddy.mood() == face::CLOCK) {
    time_t t = time(nullptr); struct tm tmv; localtime_r(&t, &tmv);
    snprintf(hhmm, sizeof hhmm, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    refreshDateMask();
  }
  buddy.render(canvas, hhmm);
  tft.pushImage(0, 0, face::W, face::H, (lgfx::swap565_t*)canvas.fb);
}

// ---------------- personality (saved in flash) ----------------
static float affPoints = 0;
static bool affDirty = false;
static void addAffection(float p) {
  affPoints += p; affDirty = true;
  buddy.setAffection(fminf(1.f, affPoints / AFFECTION_FULL));
}
static void saveAffection(uint32_t now, bool force = false) {
  static uint32_t last = 0;
  if (!affDirty || (!force && now - last < 5UL * 60UL * 1000UL)) return;
  last = now; affDirty = false;
  prefs.putFloat("aff", affPoints);
}

// ---------------- which way up it is (needs "cal up" and "cal face" once) ----------------
struct Vec3 { float x, y, z; };
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

// ---------------- Wi-Fi: time, updates, weather ----------------
static bool wifiWanted() { return strlen(WIFI_SSID) > 0; }
static bool otaOn = false;
static volatile int weatherFromTask = -1;   // written by the weather task, read by the loop

static void startOta() {
  if (!strlen(OTA_PASSWORD)) { Serial.println("Updates over Wi-Fi are off: set OTA_PASSWORD in secrets.h"); return; }
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
  Serial.printf("Updates over Wi-Fi: on, as \"%s\"\n", OTA_HOSTNAME);
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
static void weatherTask(void*) {
  for (;;) {
    uint32_t waitMs = 60UL * 1000UL;                     // try again in a minute if it didn't work
    if (WiFi.status() == WL_CONNECTED) {
      WiFiClientSecure client;
      client.setInsecure();                              // public weather data; nothing private is sent
      HTTPClient http;
      char url[180];
      snprintf(url, sizeof url, "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=weather_code,is_day", WEATHER_LAT, WEATHER_LON);
      if (http.begin(client, url)) {
        if (http.GET() == 200) {
          int w = parseWeather(http.getString().c_str());
          if (w >= 0) { weatherFromTask = w; waitMs = 30UL * 60UL * 1000UL; }
        }
        http.end();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(waitMs));
  }
}

static void startWifi() {
  if (!wifiWanted()) { Serial.println("Wi-Fi is off (no WIFI_SSID in secrets.h)"); return; }
  WiFi.setHostname(OTA_HOSTNAME);   // before mode(), or some core versions ignore it
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  sntp_set_time_sync_notification_cb(onTimeSynced);
  xTaskCreatePinnedToCore(weatherTask, "weather", 10240, nullptr, 1, nullptr, 0);
  Serial.printf("Connecting to Wi-Fi \"%s\"...\n", WIFI_SSID);
}
static void pollWifi() {
  static bool was = false;
  if (!wifiWanted()) return;
  bool on = WiFi.status() == WL_CONNECTED;
  if (on && !was) {
    Serial.printf("Wi-Fi connected, IP %s\n", WiFi.localIP().toString().c_str());
    configTzTime(TIMEZONE, "pool.ntp.org", "time.google.com");
    if (!otaOn) startOta();
  }
  if (!on && was) Serial.println("Wi-Fi lost, reconnecting...");
  was = on;
  if (otaOn) ArduinoOTA.handle();

  static int weatherShown = -1;
  int w = weatherFromTask;
  if (w != weatherShown) {
    weatherShown = w;
    buddy.setWeather(face::Weather(w));
    Serial.printf("[weather] %s\n", face::WEATHER_NAMES[w]);
  }
}

// ---------------- alarm, birthdays and reminders ----------------
static int alarmH = -1, alarmM = -1;
static void checkCalendar(uint32_t now) {
  static uint32_t last = 0, lastWater = 0, lastStretch = 0;
  static int lastAlarmDay = 0;
  if (now - last < 15000) return;
  last = now;
  if (!lastWater) { lastWater = now; lastStretch = now; }
  time_t tt = time(nullptr); struct tm tmv; localtime_r(&tt, &tmv);
  int today = (tmv.tm_year + 1900) * 10000 + (tmv.tm_mon + 1) * 100 + tmv.tm_mday;

  // daily alarm (not while it lies on its face: that's quiet time)
  if (alarmH >= 0 && tmv.tm_hour == alarmH && tmv.tm_min == alarmM && lastAlarmDay != today && !buddy.faceDown()) {
    lastAlarmDay = today; buddy.alarm(); interacted();
    Serial.println("[alarm]");
  }

  bool freeNow = buddy.mood() == face::HAPPY && !buddy.faceDown() && !buddy.upsideDown() && !buddy.busy() && !buddy.overlayActive();

  // reminders, only during the day
  bool day = tmv.tm_hour >= REMIND_FROM_HOUR && tmv.tm_hour < REMIND_TO_HOUR;
  if (REMIND_WATER_MIN > 0 && now - lastWater > REMIND_WATER_MIN * 60000UL) {
    if (!day || buddy.mood() == face::SLEEPY || buddy.faceDown()) lastWater = now;   // skipped
    else if (freeNow) { lastWater = now; buddy.remind(face::REMIND_WATER); Serial.println("[remind] water"); }
  }
  if (REMIND_STRETCH_MIN > 0 && now - lastStretch > REMIND_STRETCH_MIN * 60000UL) {
    if (!day || buddy.mood() == face::SLEEPY || buddy.faceDown()) lastStretch = now;
    else if (freeNow) { lastStretch = now; buddy.remind(face::REMIND_STRETCH); Serial.println("[remind] stretch"); }
  }

  // birthdays: hers, and the buddy's own (the day it was given, from the next year on).
  // Only with a real date from the internet, the first time it's awake after 8 am, once a day.
  if (!clockIsReal || tmv.tm_hour < 8 || !freeNow) return;
  bool hers = BIRTHDAY_MONTH > 0 && tmv.tm_mon + 1 == BIRTHDAY_MONTH && tmv.tm_mday == BIRTHDAY_DAY;
  bool own = GIFT_DAY > 0 && tmv.tm_mon + 1 == GIFT_MONTH && tmv.tm_mday == GIFT_DAY && tmv.tm_year + 1900 > GIFT_YEAR;
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
  Serial.println("          alarm, time, happy, annoyed, colour, cat, acorn, mouth, settime HH:MM");
  Serial.println("Features: 8ball, weather sun|cloud|rain|off|show, timer [minutes] | timer stop,");
  Serial.println("          remind water|stretch, note <text>, bday [own], battery <0-100>|auto,");
  Serial.println("          affection <0-1>, tilt <x> <y>|off, facedown on|off, upside on|off");
  Serial.println("Setup:    cal up, cal face, axes, wifi");
}

static void handleCommand(String raw) {
  raw.trim();
  String cmd = raw; cmd.toLowerCase();
  if (!cmd.length()) return;
  interacted();
  if (cmd == "shake") buddy.shake();
  else if (cmd == "pick" || cmd == "pickup") buddy.pickUp();
  else if (cmd == "name") buddy.sayName();
  else if (cmd == "listen") buddy.listen();
  else if (cmd == "think") buddy.think();
  else if (cmd == "talk") buddy.talk();
  else if (cmd == "pet") { buddy.pet(); addAffection(1); }
  else if (cmd == "poke") buddy.poke();
  else if (cmd == "ignore") { face::Face::Step s[] = { { face::BORED, 5000 }, { face::SLEEPY, 0 } }; buddy.play(s, 2); }
  else if (cmd == "bored") buddy.bored();
  else if (cmd == "sleep") buddy.sleep();
  else if (cmd == "wake") buddy.wake();
  else if (cmd == "alarm") buddy.alarm();
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
    if (!wifiWanted()) Serial.println("Wi-Fi off (set WIFI_SSID in secrets.h)");
    else Serial.printf("Wi-Fi %s, clock %s, updates %s, weather %s\n", WiFi.status() == WL_CONNECTED ? "connected" : "not connected",
                       clockIsReal ? "from the internet" : "from the upload time", otaOn ? "on" : "off", face::WEATHER_NAMES[buddy.weather()]);
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
  static bool touching = false, petting = false;
  static uint32_t downAt = 0, lastSeen = 0;
  if (buddy.faceDown()) return;
  int32_t x, y;
  bool down = tft.getTouch(&x, &y) > 0;
  if (down) {
    lastSeen = now;
    if (!touching) { touching = true; petting = false; downAt = now; interacted(); }
    else if (!petting && now - downAt > PET_HOLD_MS && buddy.mood() != face::ALARM) { petting = true; buddy.petStart(); addAffection(1); interacted(); }
  } else if (touching && now - lastSeen > 60) {        // released (with a little debounce)
    touching = false;
    interacted();
    if (petting) buddy.petEnd();
    else if (lastSeen - downAt < TAP_MAX_MS) { buddy.poke(); addAffection(0.2f); lastTapAt = now; }
  }
#else
  (void)now;
#endif
}

static void pollMotion(uint32_t now) {
  Motion::Event e = motion.poll(now);
  if (e == Motion::NONE || buddy.faceDown()) return;        // lying on its face: ignore bumps
  interacted();
  if (buddy.mood() == face::ALARM) { buddy.snooze(); return; }   // shake or lift to snooze
  if (e == Motion::SHAKE && lastTapAt && now - lastTapAt < EIGHT_BALL_WINDOW_MS) {
    lastTapAt = 0; buddy.eightBall();
    Serial.printf("[8-ball] %s\n", buddy.eightBallAnswer());
    return;
  }
  if (e == Motion::SHAKE) buddy.shake();
  else { buddy.pickUp(); addAffection(0.1f); }                    // also wakes it up if asleep
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

// ---------------- setup and loop ----------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nDewy starting");

  tft.init();
  tft.setRotation(SCREEN_ROTATION);
  tft.setBrightness(0);
  tft.fillScreen(TFT_BLACK);

  // one full 320 x 240 frame (150 KB). Internal RAM is fastest; PSRAM is the fallback.
  canvas.fb = (uint16_t*)heap_caps_malloc(face::W * face::H * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!canvas.fb) canvas.fb = (uint16_t*)heap_caps_malloc(face::W * face::H * 2, MALLOC_CAP_SPIRAM);
  if (!canvas.fb) { Serial.println("Not enough memory for the frame buffer"); while (true) delay(1000); }

  prefs.begin("buddy", false);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  if (motion.begin(Wire)) Serial.printf("Motion sensor found (WHO_AM_I 0x%02X)\n", motion.whoAmI());
  else Serial.println("Motion sensor not found: check SDA 4, SCL 5, 3.3V and GND. The face still runs.");
  loadCalibration();
  if (motion.available() && !calOk) Serial.println("Leaning, face down and upside down need a one-time calibration: type cal up, then cal face");

  setClockFromCompileTime();
  refreshDateMask();
  if (strlen(ALARM_TIME) == 5) { alarmH = atoi(ALARM_TIME); alarmM = atoi(ALARM_TIME + 3); }

  font18 = fontFrom(fonts::FreeSansBold18pt7b);
  font24 = fontFrom(fonts::FreeSansBold24pt7b);
  buddy.font18 = &font18;
  buddy.font24 = &font24;
  buddy.noteFrom = NOTE_FROM;
  buddy.mouth = SHOW_MOUTH;
  buddy.catChancePct = CAT_CHANCE_PCT;
  buddy.acornChancePct = ACORN_CHANCE_PCT;
  buddy.alarmText = alarmH >= 0 ? ALARM_TIME : "07:00";
  buddy.drawDate = drawDate;
  buddy.onMood = printMood;
  buddy.begin(esp_random());
  affPoints = prefs.getFloat("aff", 0);
  addAffection(0); affDirty = false;

  startWifi();
  lastInteract = lastFrame = millis();
  Serial.println("Type \"help\" for test commands.");
}

void loop() {
  uint32_t now = millis();
  pollSerial();
  pollTouch(now);
  pollMotion(now);
  pollOrientation(now);
  pollWifi();
  pollBattery(now);
  checkCalendar(now);
  checkIdle(now);
  saveAffection(now);
  if (now - lastFrame >= FRAME_MS) drawFrame();
}
