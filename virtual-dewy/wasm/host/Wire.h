// I2C stand-in with a tiny MPU6050 inside (address 0x68), so the firmware's real motion.h talks to a "sensor".
// The browser sets the acceleration; if the sensor isn't wired to the right pins the bus answers "no device",
// exactly like the real thing, and the firmware prints its own "Motion sensor not found" line.
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>

inline int g_imuPresent = 1;            // set by the browser from the wiring
inline float g_accel[3] = { 0, 1, 0 };  // in g, as the sensor would measure it
inline int g_i2cSda = -1, g_i2cScl = -1;

struct TwoWire {
  uint8_t addr = 0, reg = 0, nbytes = 0;
  uint8_t rx[16]; int rxLen = 0, rxPos = 0;
  bool begin(int sda, int scl, uint32_t) { g_i2cSda = sda; g_i2cScl = scl; return true; }
  void beginTransmission(uint8_t a) { addr = a; nbytes = 0; }
  void write(uint8_t b) { if (nbytes == 0) reg = b; nbytes++; }   // the first byte is the register to read; writes to registers are ignored
  uint8_t endTransmission(bool = true) { return (addr == 0x68 && g_imuPresent) ? 0 : 2; }   // 2 = address NACK
  uint8_t requestFrom(uint8_t a, uint8_t n) {
    rxLen = rxPos = 0;
    if (a != 0x68 || !g_imuPresent || n > sizeof rx) return 0;
    for (uint8_t i = 0; i < n; i++) rx[rxLen++] = regValue(uint8_t(reg + i));
    return n;
  }
  int read() { return rxPos < rxLen ? rx[rxPos++] : -1; }
 private:
  static uint8_t regValue(uint8_t r) {
    if (r == 0x75) return 0x68;                       // WHO_AM_I
    if (r >= 0x3B && r <= 0x40) {                     // ACCEL_XOUT_H .. ACCEL_ZOUT_L, +-4 g range = 8192 LSB per g
      int axis = (r - 0x3B) / 2;
      float v = g_accel[axis] * 8192.f;
      if (v > 32767.f) v = 32767.f; if (v < -32768.f) v = -32768.f;
      int16_t raw = int16_t(lroundf(v));
      return ((r - 0x3B) & 1) ? uint8_t(raw & 0xFF) : uint8_t((raw >> 8) & 0xFF);
    }
    return 0;
  }
};
inline TwoWire Wire;
