// The sketch draws each frame in strips (one render per strip). Every strip-drawn frame must match the whole frame.
#include "../dewy/face.h"
#include "fonts/fonts_pc.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <functional>
using namespace face;
static int pass = 0, fail = 0;
static void fresh(Face& f) { f.begin(4242); f.font18 = &PC_FONT18; f.font24 = &PC_FONT24; f.setPalette(3); for (int i = 0; i < 60; i++) f.update(16); }
static void check(const char* name, std::function<void(Face&)> act, int waitMs, int bandRows) {
  Face f; fresh(f); act(f); for (int i = 0; i < waitMs / 16; i++) f.update(16);
  std::vector<uint16_t> whole(W * H), strips(W * H), strip(W * bandRows);
  Canvas a; a.fb = whole.data(); f.render(a, "07:30");
  bool flipped = f.upsideDown();
  for (int y0 = 0; y0 < H; y0 += bandRows) {
    int rows = H - y0 < bandRows ? H - y0 : bandRows;
    int sy0 = flipped ? H - y0 - rows : y0;
    Canvas c; c.fb = strip.data() - sy0 * W; c.bandY0 = sy0; c.bandY1 = sy0 + rows;
    f.render(c, "07:30");
    memcpy(strips.data() + y0 * W, strip.data(), size_t(rows) * W * 2);
  }
  int diff = 0; for (int i = 0; i < W * H; i++) if (whole[i] != strips[i]) diff++;
  if (diff == 0) pass++; else { fail++; printf("FAIL %s (%d-row strips): %d pixels differ\n", name, bandRows, diff); }
}
int main() {
  for (int band : { 48, 40, 60, 7 }) {
    check("happy", [](Face&) {}, 200, band);
    check("talking", [](Face& f) { f.talk(); }, 500, band);
    check("sun card", [](Face& f) { f.setWeather(WEATHER_SUN); f.showWeather(); }, 1600, band);
    check("note (dims the face)", [](Face& f) { f.showNote("Good luck with your exam today!"); }, 3000, band);
    check("upside down", [](Face& f) { f.setUpsideDown(true); }, 800, band);
    check("bond moment", [](Face& f) { f.setAffection(0.7f); f.bondMoment("Yay, it's you!", 5, 4500); }, 1400, band);
    check("8-ball", [](Face& f) { f.eightBall(); }, 2600, band);
    check("clock", [](Face& f) { f.showTime(8000); }, 1000, band);
    check("wave mouth", [](Face& f) { f.setMood(LISTENING); }, 900, band);
  }
  printf("%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
