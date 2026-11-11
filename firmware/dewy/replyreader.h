// Reads the body of an HTTP reply (with a length, without one, or chunked) and always sleeps while it waits for data.
// Why not HTTPClient::writeToStream: while it waits for the next bytes it loops without sleeping. On core 0 that keeps the
// idle task from ever running, and after 5 seconds the watchdog restarts Dewy in the middle of its answer.
// It is a template over the stream type so it can be tested on a PC (firmware/test/reply_test.cpp).
#pragma once
#include <Arduino.h>

// S needs available(), read(uint8_t*, size_t) and connected(). sink(const uint8_t*, size_t) is called with each piece of
// the body and returns false to stop (the buffer is full). contentLength is -1 when the server didn't say.
// idleMs: give up when nothing arrives for this long. totalMs: give up after this long altogether.
// Returns true when the whole body arrived.
template <typename S, typename Sink>
static bool readReplyBody(S& s, long contentLength, bool chunked, uint32_t idleMs, uint32_t totalMs, Sink sink) {
  enum { CLOSED = -1, TIMEOUT = -2 };
  uint8_t buf[512];
  const uint32_t startedAt = millis();
  uint32_t lastData = startedAt;

  // Gets between 1 and `want` bytes, sleeping while there are none.
  auto take = [&](uint8_t* dst, size_t want) -> int {
    for (;;) {
      int a = s.available();
      if (a > 0) {
        int n = s.read(dst, (size_t)a < want ? (size_t)a : want);
        if (n > 0) { lastData = millis(); return n; }
      } else if (!s.connected()) {
        return CLOSED;
      }
      uint32_t now = millis();
      if (now - lastData > idleMs || now - startedAt > totalMs) return TIMEOUT;
      delay(2);
    }
  };

  if (!chunked) {
    long got = 0;
    while (contentLength < 0 || got < contentLength) {
      size_t want = sizeof buf;
      if (contentLength >= 0 && (size_t)(contentLength - got) < want) want = (size_t)(contentLength - got);
      int n = take(buf, want);
      if (n < 0) return n == CLOSED && contentLength < 0;   // with no length, the end of the connection is the end of the body
      if (!sink(buf, (size_t)n)) return false;
      got += n;
    }
    return true;
  }

  for (;;) {
    // chunk header: hex size, maybe ";extension", then CRLF
    size_t size = 0;
    bool digits = false, ext = false;
    for (;;) {
      uint8_t c;
      if (take(&c, 1) < 0) return false;
      if (c == '\n') break;
      if (c == '\r' || ext) continue;
      if (c == ';') { ext = true; continue; }
      int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (d < 0 || size > (1u << 27)) return false;          // not a chunk header
      size = size * 16 + (size_t)d;
      digits = true;
    }
    if (!digits) return false;
    if (size == 0) return true;                               // the last chunk; what follows (trailers) doesn't matter
    while (size) {
      int n = take(buf, size < sizeof buf ? size : sizeof buf);
      if (n < 0) return false;
      if (!sink(buf, (size_t)n)) return false;
      size -= (size_t)n;
    }
    for (;;) {                                                // the CRLF after the chunk's data
      uint8_t c;
      if (take(&c, 1) < 0) return false;
      if (c == '\n') break;
    }
  }
}
