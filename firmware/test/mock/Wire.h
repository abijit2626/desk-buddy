#pragma once
#include <cstdint>
struct TwoWire { bool begin(int, int, uint32_t) { return true; } void beginTransmission(uint8_t) {} void write(uint8_t) {} uint8_t endTransmission(bool = true) { return 0; } uint8_t requestFrom(uint8_t, uint8_t n) { return n; } int read() { return 0; } };
extern TwoWire Wire;
