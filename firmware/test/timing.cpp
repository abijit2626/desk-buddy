#include "../dewy/face.h"
#include "fonts/fonts_pc.h"
#include <chrono>
#include <vector>
#include <cstdio>
using namespace face;
int main() {
  std::vector<uint16_t> buf(W * H); Canvas cv; cv.fb = buf.data();
  auto timeIt = [&](const char* name, void (*act)(Face&)) {
    Face f; f.begin(3); f.font18 = &PC_FONT18; f.font24 = &PC_FONT24; act(f);
    for (int i = 0; i < 100; i++) f.update(16);
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 100; i++) { f.update(16); f.render(cv, "07:30"); }
    auto t1 = std::chrono::high_resolution_clock::now();
    printf("%-10s %.2f ms per frame on this PC\n", name, std::chrono::duration<double, std::milli>(t1 - t0).count() / 100);
  };
  timeIt("happy", [](Face&) {});
  timeIt("8-ball", [](Face& f) { f.eightBall(); });
  timeIt("birthday", [](Face& f) { f.birthday(); });
  timeIt("note", [](Face& f) { f.showNote("Good luck with your exam today! You've got this"); });
  timeIt("rain", [](Face& f) { f.setWeather(WEATHER_RAIN); f.showWeather(); });
  timeIt("sun", [](Face& f) { f.setWeather(WEATHER_SUN); f.showWeather(); });
}
