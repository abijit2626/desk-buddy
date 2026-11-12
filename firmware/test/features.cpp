// Renders each new feature with the real face engine and fonts (PC only).
#include "../dewy/face.h"
#include "fonts/fonts_pc.h"
#include <cstdio>
#include <vector>
#include <functional>
using namespace face;
static std::vector<uint16_t> buf(W * H);
static Canvas cv;
static void save(const char* name) {
  char path[96]; snprintf(path, sizeof path, "feat/%s.ppm", name);
  FILE* f = fopen(path, "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int i = 0; i < W * H; i++) { RGB c = unpack(buf[i]); unsigned char px[3] = { (unsigned char)c.r, (unsigned char)c.g, (unsigned char)c.b }; fwrite(px, 1, 3, f); }
  fclose(f);
}
static void fresh(Face& f, int pal = 0) {
  f.begin(4242); f.font18 = &PC_FONT18; f.font24 = &PC_FONT24; f.setPalette(pal);
  for (int i = 0; i < 60; i++) f.update(16);
}
static void run(Face& f, int ms) { for (int i = 0; i < ms / 16; i++) f.update(16); }
static void shot(const char* name, std::function<void(Face&)> act, int waitMs, int pal = 0) {
  Face f; fresh(f, pal); act(f); run(f, waitMs); f.render(cv, "07:30"); save(name);
}
int main() {
  cv.fb = buf.data();
  shot("happy", [](Face&) {}, 200);
  shot("sun", [](Face& f) { f.setWeather(WEATHER_SUN); f.showWeather(); }, 1600);
  shot("cloud", [](Face& f) { f.setWeather(WEATHER_CLOUD); f.showWeather(); }, 1600);
  shot("rain", [](Face& f) { f.setWeather(WEATHER_RAIN); f.showWeather(); }, 1600);
  shot("eight", [](Face& f) { f.eightBall(); }, 2600);
  shot("timer", [](Face& f) { f.startTimer(25); for (int i = 0; i < 540; i++) f.update(1000); }, 1200);
  shot("break", [](Face& f) { f.startTimer(1); for (int i = 0; i < 60; i++) f.update(1000); }, 1500);
  shot("water", [](Face& f) { f.remind(REMIND_WATER); }, 1700);
  shot("stretch", [](Face& f) { f.remind(REMIND_STRETCH); }, 1700);
  shot("note", [](Face& f) { f.showNote("Good luck with your exam today! You\xE2\x80\x99ve got this \xE2\x9D\xA4\xEF\xB8\x8F"); }, 3000);
  shot("note_long", [](Face& f) { f.showNote("This is a really long note to check that the words wrap onto three lines and that anything past that ends with three little dots"); }, 3000);
  shot("bday", [](Face& f) { f.birthday(false); }, 1800);
  shot("bday_own", [](Face& f) { f.birthday(true); }, 2600);
  shot("grow", [](Face& f) { f.setAffection(0.9f); run(f, 4000 - int(f.timeMs()) % 4000 + 400); }, 0);
  shot("bond_welcome", [](Face& f) { f.setAffection(0.7f); f.bondMoment("Yay, it's you!", 4, 4500); }, 1400);
  shot("bond_missed", [](Face& f) { f.setAffection(0.7f); f.bondMoment("I missed you so much!", 7, 6500); }, 1900);
  shot("bond_levelup", [](Face& f) { f.setAffection(0.66f); f.bondMoment("Best friends forever!", 7, 6000); }, 2200);
  shot("bond_missing", [](Face& f) { f.setAffection(0.5f); f.setMissing(true); f.bored(); run(f, 9000 - int(f.timeMs()) % 9000 + 1500); }, 0);
  shot("resting_awake", [](Face& f) { f.resting = true; }, 800);
  shot("batt_low", [](Face& f) { f.setBattery(15); }, 1500);
  shot("batt_empty", [](Face& f) { f.setBattery(4); f.sleep(); }, 2000);
  shot("update", [](Face& f) { f.setUpdateProgress(0.45f); }, 1500);
  shot("upside", [](Face& f) { f.setUpsideDown(true); }, 1500);
  shot("tilt_right", [](Face& f) { f.setTilt(1, 0); }, 1500);
  shot("tilt_back", [](Face& f) { f.setTilt(0, 1); }, 1500);
  shot("facedown", [](Face& f) { f.setFaceDown(true); }, 1500);
  shot("setup_card", [](Face& f) { f.showBanner("Set up my Wi-Fi", "Join this Wi-Fi on your phone", "Dewy-Setup"); }, 1500);
  shot("pair_card", [](Face& f) { f.showBanner("Pair your phone", "Type it in the Dewy app", "ZG3U T4CS"); }, 1500);
  // a note that arrives while asleep shows up on waking
  { Face f; fresh(f); f.sleep(); run(f, 500); f.showNote("Waiting for you"); run(f, 500);
    printf("pending while asleep: %d\n", f.hasPendingNote()); f.wake(); run(f, 3000); printf("pending after wake: %d\n", f.hasPendingNote());
    f.render(cv, "07:30"); save("note_after_wake"); }
  // reminders are skipped while asleep
  { Face f; fresh(f); f.sleep(); run(f, 300); printf("remind while asleep returned %d\n", f.remind(REMIND_WATER)); }
  // weather shows by itself on waking
  { Face f; fresh(f); f.setWeather(WEATHER_SUN); f.sleep(); run(f, 500); f.wake(); run(f, 1500); printf("weather on wake overlay: %d\n", f.overlayActive()); }
  // the clock keeps running smoothly over many hours (time is rebased)
  { Face f; fresh(f); for (int i = 0; i < 30 * 3600; i++) f.update(1000); printf("after 30 h: t = %.0f ms\n", f.timeMs()); run(f, 500); f.render(cv, "07:30"); save("after_30h"); }
  printf("done\n");
}
