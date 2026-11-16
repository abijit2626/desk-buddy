// Shake and pick-up detection from the MPU6050 on the GY-87 (I2C address 0x68).
// No library needed: it reads the accelerometer registers directly.
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <math.h>

class Motion {
public:
  enum Event { NONE, SHAKE, PICKUP };

  bool begin(TwoWire& w) {
    wire = &w;
    if (!readReg(0x75, who) || (who != 0x68 && who != 0x70 && who != 0x72)) return ok = false;  // WHO_AM_I
    writeReg(0x6B, 0x00);   // wake up
    writeReg(0x1C, 0x08);   // accelerometer range +-4 g
    writeReg(0x1A, 0x03);   // light low-pass filter
    ok = true;
    return true;
  }
  bool available() const { return ok; }
  uint8_t whoAmI() const { return who; }
  // the latest accelerometer reading in g (x, y, z), for working out which way up it is
  bool latest(float out[3]) const { if (!primed) return false; for (int i = 0; i < 3; i++) out[i] = prev[i]; return true; }

  // call often; it samples every 10 ms and returns an event when one is detected
  Event poll(uint32_t now) {
    if (!ok || now - lastRead < 10) return NONE;
    lastRead = now;
    float a[3];
    if (!readAccel(a)) return NONE;
    return feed(now, a);
  }

  // the detector itself, one accelerometer reading (in g) at a time
  Event feed(uint32_t now, const float a[3]) {
    if (!primed) { for (int i = 0; i < 3; i++) prev[i] = base[i] = anchor[i] = a[i]; primed = true; stillSince = now; return NONE; }

    float jerk = dist(a, prev);
    for (int i = 0; i < 3; i++) prev[i] = a[i];

    // shake: several big jolts within one second (it can follow straight after a pick-up)
    if (jerk > 0.7f) {
      if (nj < 8) jolts[nj++] = now;
      int k = 0; for (int i = 0; i < nj; i++) if (now - jolts[i] < 1000) jolts[k++] = jolts[i];
      nj = k;
      if (nj >= 4 && now > shakeCooldown) { nj = 0; shakeCooldown = now + 2500; liftCooldown = now + 2500; onDesk = false; offStart = 0; return SHAKE; }
    }

    // On the desk: every reading for 1.5 s stays within 0.035 g of where the quiet spell began. Sensor noise is
    // far below that; a hand holding Dewy wobbles more, so being held never counts as resting on the desk.
    if (dist(a, anchor) > 0.035f) { for (int i = 0; i < 3; i++) anchor[i] = a[i]; stillSince = now; }
    if (now - stillSince > 1500) {
      float d = dist(a, base);
      if (!onDesk || d >= 0.05f) for (int i = 0; i < 3; i++) base[i] = a[i];   // just put down, or settled a bit
      else for (int i = 0; i < 3; i++) base[i] += (a[i] - base[i]) * 0.02f;     // follow slow drift
      onDesk = true; offStart = 0;
    }

    // Pick-up: it was resting on the desk, then gravity stays off its resting value (tilted about 7 degrees, or
    // pushed up or down by 0.12 g) for 0.15 s. A tap on the screen only jolts it for a few hundredths of a
    // second and a shake is all big jolts, so neither counts. Putting it back down is not a pick-up: it only
    // counts as on the desk again once it has rested there.
    if (onDesk) {
      float dev = dist(a, base);
      if (dev > 0.12f) { if (!offStart) offStart = now; lastOff = now; }
      else if (offStart && now - lastOff > 60) offStart = 0;     // back where it was (short dips are allowed)
      bool jolting = nj > 0 && now - jolts[nj - 1] < 300;
      if (offStart && now - offStart >= 150 && dev > 0.12f && !jolting && now > liftCooldown) {
        onDesk = false; offStart = 0; liftCooldown = now + 3000; return PICKUP;
      }
    }
    return NONE;
  }

private:
  TwoWire* wire = nullptr;
  bool ok = false, primed = false, onDesk = false;
  uint8_t who = 0;
  uint32_t lastRead = 0, stillSince = 0, shakeCooldown = 0, liftCooldown = 0, jolts[8];
  int nj = 0;
  uint32_t offStart = 0, lastOff = 0;
  float prev[3], base[3], anchor[3];

  static float dist(const float* a, const float* b) {
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
  }
  void writeReg(uint8_t reg, uint8_t v) {
    wire->beginTransmission(0x68); wire->write(reg); wire->write(v); wire->endTransmission();
  }
  bool readReg(uint8_t reg, uint8_t& v) {
    wire->beginTransmission(0x68); wire->write(reg);
    if (wire->endTransmission(false) != 0) return false;
    if (wire->requestFrom((uint8_t)0x68, (uint8_t)1) != 1) return false;
    v = wire->read(); return true;
  }
  bool readAccel(float* a) {
    wire->beginTransmission(0x68); wire->write(0x3B);
    if (wire->endTransmission(false) != 0) return false;
    if (wire->requestFrom((uint8_t)0x68, (uint8_t)6) != 6) return false;
    for (int i = 0; i < 3; i++) {
      uint8_t hi = wire->read(), lo = wire->read();   // read in this order (high byte first)
      int16_t raw = int16_t((hi << 8) | lo);
      a[i] = raw / 8192.0f;   // +-4 g range
    }
    return true;
  }
};
