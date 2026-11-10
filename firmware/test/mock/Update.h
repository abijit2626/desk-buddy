#pragma once
#include <cstddef>
#include <cstdint>
struct UpdateClass {
  size_t want = 0, got = 0; bool ended = false;
  bool begin(size_t n) { want = n; got = 0; return true; }
  size_t write(const uint8_t*, size_t n) { got += n; return n; }
  bool end(bool) { ended = got == want; return ended; }
  void abort() { got = 0; }
};
inline UpdateClass Update;
