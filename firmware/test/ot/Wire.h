#pragma once
#include <cstdint>
#include <cmath>
// fake MPU6050: WHO_AM_I = 0x68, accelerometer = the global gravity vector g (in g)
extern float g_acc[3];
struct TwoWire {
  uint8_t reg = 0, buf[6]; int n = 0, i = 0;
  bool begin(int, int, uint32_t) { return true; }
  void beginTransmission(uint8_t) { n = -1; }
  void write(uint8_t v) { if (n == -1) { reg = v; n = 0; } }
  uint8_t endTransmission(bool = true) { return 0; }
  uint8_t requestFrom(uint8_t, uint8_t cnt) {
    i = 0;
    if (reg == 0x75) buf[0] = 0x68;
    if (reg == 0x3B) for (int k = 0; k < 3; k++) { int16_t r = int16_t(lroundf(g_acc[k] * 8192)); buf[2 * k] = uint8_t(r >> 8); buf[2 * k + 1] = uint8_t(r & 255); }
    return cnt;
  }
  int read() { return buf[i++]; }
};
extern TwoWire Wire;
