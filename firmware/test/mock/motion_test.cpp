#include <cstdint>
#include <cmath>
#include <cstdio>
#include <vector>
// fake I2C that plays back accelerometer samples
static float g_ax = 0, g_ay = 0, g_az = 1; static int g_byte = 0; static uint8_t g_reg = 0;
struct TwoWire {
  void beginTransmission(uint8_t) {} void write(uint8_t r) { g_reg = r; g_byte = 0; } uint8_t endTransmission(bool = true) { return 0; }
  uint8_t requestFrom(uint8_t, uint8_t n) { return n; }
  int read() {
    if (g_reg == 0x75) return 0x68;
    float v[3] = { g_ax, g_ay, g_az }; int i = g_byte / 2; int16_t raw = int16_t(lroundf(v[i] * 8192));
    int b = (g_byte % 2 == 0) ? ((raw >> 8) & 0xFF) : (raw & 0xFF); g_byte++; return b;
  }
};
#define ARDUINO_H_STUB

#include "../../dewy/motion.h"
int main() {
  TwoWire w; Motion m; printf("begin: %d\n", m.begin(w));
  uint32_t t = 0; auto run = [&](int ms, auto fn) { std::vector<int> ev; for (int i = 0; i < ms; i += 10) { t += 10; fn(i); int e = m.poll(t); if (e) ev.push_back(e); } return ev; };
  auto still = [&](int) { g_ax = 0.01f; g_ay = -0.02f; g_az = 1.0f; };
  auto p1 = run(3000, still); printf("resting 3 s -> %zu events (want 0)\n", p1.size());
  auto lift = run(600, [&](int i) { float k = fminf(1, i / 200.f); g_ax = 0.05f + 0.35f * k; g_ay = -0.02f; g_az = 1.0f + 0.25f * k * (i < 300); });
  printf("lift -> "); for (int e : lift) printf("%s ", e == 2 ? "PICKUP" : "SHAKE"); printf("(want PICKUP)\n");
  run(3500, still);
  auto shake = run(1200, [&](int i) { float s = (i / 40) % 2 ? 1.5f : -1.5f; g_ax = s; g_ay = 0.3f * s; g_az = 1.0f; });
  printf("shake -> "); for (int e : shake) printf("%s ", e == 2 ? "PICKUP" : "SHAKE"); printf("(want SHAKE)\n");
  auto bump = run(3000, [&](int i) { still(i); if (i == 1000) g_az = 1.3f; });
  printf("one small bump on a resting buddy -> %zu events\n", bump.size());
  return 0;
}
