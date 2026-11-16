// Pick-up and shake detection on made-up accelerometer traces.
#include <cstdint>
struct TwoWire { void beginTransmission(uint8_t) {} void write(uint8_t) {} uint8_t endTransmission(bool = true) { return 0; } uint8_t requestFrom(uint8_t, uint8_t n) { return n; } int read() { return 0; } };
#include "../dewy/motion.h"
#include <cstdio>
#include <cmath>
#include <functional>
static int fails = 0, checks = 0;
static void check(bool ok, const char* what) { checks++; if (!ok) { fails++; printf("FAIL: %s\n", what); } }
static uint32_t lcg = 1;
static float noise() { lcg = lcg * 1664525u + 1013904223u; return ((lcg >> 8) / 16777216.f - 0.5f) * 0.016f; }   // +-0.008 g
// run a trace: f(t ms) gives the clean reading; samples every dt ms; counts events
static void run(std::function<void(float, float*)> f, float ms, int& picks, int& shakes, float dt = 20) {
  Motion m; picks = shakes = 0;
  for (float t = 0; t < ms; t += dt) {
    float a[3]; f(t, a); for (int i = 0; i < 3; i++) a[i] += noise();
    Motion::Event e = m.feed(uint32_t(t) + 1, a);
    if (e == Motion::PICKUP) picks++;
    if (e == Motion::SHAKE) shakes++;
  }
}
static void tilt(float deg, float* a) { float r = deg * 3.14159f / 180; a[0] = sinf(r); a[1] = 0; a[2] = cosf(r); }
int main() {
  int p, s;
  for (float dt : { 10.f, 20.f, 35.f }) {
    run([](float t, float* a) { tilt(0, a); }, 5000, p, s, dt);
    check(p == 0 && s == 0, "resting: nothing");
    run([](float t, float* a) { tilt(0, a); if (t > 2000 && t < 2030) a[2] += 0.6f; if (t >= 2030 && t < 2060) a[2] -= 0.3f; }, 5000, p, s, dt);
    check(p == 0, "a tap on the screen is not a pick-up");
    run([](float t, float* a) { float d = t < 2000 ? 0 : fminf(25.f, (t - 2000) / 20.f); tilt(d, a); }, 5000, p, s, dt);
    check(p == 1, "lift and tilt 25 degrees over half a second: one pick-up");
    run([](float t, float* a) { float d = t < 2000 ? 0 : fminf(10.f, (t - 2000) / 40.f); tilt(d, a); }, 5000, p, s, dt);
    check(p == 1, "gentle 10 degree tip: one pick-up");
    run([](float t, float* a) { tilt(0, a); if (t > 2000 && t < 2250) a[2] += 0.2f; }, 5000, p, s, dt);
    check(p == 1, "straight up lift (0.2 g for a quarter second): one pick-up");
    run([](float t, float* a) { tilt(t < 2000 ? 0 : 4, a); }, 5000, p, s, dt);
    check(p == 0, "settling 4 degrees: nothing");
    run([](float t, float* a) { tilt(0, a); if (t > 2000 && t < 2600) a[0] += (int(t / 60) % 2 ? 1.2f : -1.2f); }, 5000, p, s, dt);
    check(s == 1, "shaking: one shake");
    run([](float t, float* a) { tilt(0, a); if (t > 2000 && t < 2090) a[2] += (int(t / 15) % 2 ? 0.5f : -0.4f); }, 5000, p, s, dt);
    check(p == 0, "a firm tap that rings for 90 ms: no pick-up");
    run([](float t, float* a) { tilt(0, a); a[2] += 0.03f * sinf(t * 0.37f); }, 6000, p, s, dt);
    check(p == 0 && s == 0, "desk vibration from typing: nothing");
    run([](float t, float* a) { float d = (t > 2000 && t < 3000) || t > 7000 ? 20.f : 0.f; tilt(d, a); }, 9000, p, s, dt);
    check(p == 2, "picked up, put down, picked up again later: two pick-ups");
    run([](float t, float* a) { float d = t > 2000 && t < 9000 ? 25.f + 4.f * sinf(t * 0.004f) + 1.5f * sinf(t * 0.031f) : 0.f; tilt(d, a); }, 13000, p, s, dt);
    check(p == 1, "held in a hand for 7 s, then put down: one pick-up, none for putting it down");
  }
  printf("%d passed, %d failed\n", checks - fails, fails);
  return fails ? 1 : 0;
}
