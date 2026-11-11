// The reply reader (replyreader.h): assembles an HTTP body from a stream that delivers it in bursts with gaps,
// chunked or not, and must sleep whenever it waits (the core's writeToStream doesn't, and the watchdog restarts Dewy).
#include "Arduino.h"
#include <string>
#include <vector>
#include "../dewy/replyreader.h"

static int passed = 0, failed = 0;
static void check(const char* name, bool ok) { printf("  %s %s\n", ok ? "ok  " : "FAIL", name); (ok ? passed : failed)++; }
SerialT Serial;

struct FakeStream {
  std::string data; size_t pos = 0;
  size_t burst = 1 << 20;     // most bytes handed out at a time
  int gapLen = 0, gap = 0;    // calls to available() that say "nothing yet" after each read
  bool stayOpen = false;      // still connected after all the data is gone (a stalled server)
  int spins = 0, maxSpins = 0, lastDelays = -1;
  int available() {
    if (pos >= data.size() || gap > 0) {
      if (gap > 0) gap--;
      if (g_delayCalls == lastDelays) maxSpins = ++spins > maxSpins ? spins : maxSpins; else spins = 1;   // zero answers in a row without sleeping
      lastDelays = g_delayCalls;
      return 0;
    }
    return (int)std::min(burst, data.size() - pos);
  }
  int read(uint8_t* b, size_t n) { size_t k = std::min(n, std::min(burst, data.size() - pos)); memcpy(b, data.data() + pos, k); pos += k; gap = gapLen; return (int)k; }
  bool connected() { return pos < data.size() || stayOpen; }
};

static std::string payload(size_t n) { std::string s(n, 0); for (size_t i = 0; i < n; i++) s[i] = char((i * 131 + 17) & 255); return s; }
static std::string chunkify(const std::string& body, size_t chunk, const char* ext = "", bool upper = false, const char* trailer = "") {
  std::string out;
  for (size_t i = 0; i < body.size(); i += chunk) {
    size_t k = std::min(chunk, body.size() - i); char h[32]; snprintf(h, sizeof h, upper ? "%zX" : "%zx", k);
    out += std::string(h) + ext + "\r\n" + body.substr(i, k) + "\r\n";
  }
  return out + "0\r\n" + trailer + "\r\n";
}
struct Got { std::string bytes; bool whole; };
static Got read(FakeStream& s, long len, bool chunked, uint32_t idle = 15000, uint32_t total = 60000, size_t cap = 1 << 30) {
  Got g; g.whole = readReplyBody(s, len, chunked, idle, total, [&](const uint8_t* d, size_t n) { size_t room = cap - g.bytes.size(), c = std::min(n, room); g.bytes.append((const char*)d, c); return c == n; });
  return g;
}

int main() {
  const std::string body = payload(5000);
  printf("-- with a Content-Length\n");
  { FakeStream s; s.data = body; s.burst = 300; s.gapLen = 3; Got g = read(s, (long)body.size(), false);
    check("a slow stream (300-byte bursts with gaps) arrives whole", g.whole && g.bytes == body);
    check("it slept every time it waited (never two empty checks in a row)", s.maxSpins <= 1); }
  { FakeStream s; s.data = body; Got g = read(s, 0, false); check("Content-Length 0: done at once, nothing read", g.whole && g.bytes.empty() && s.pos == 0); }
  { FakeStream s; s.data = body.substr(0, 3000); Got g = read(s, 5000, false);
    check("connection closed early: reported as incomplete, the 3000 bytes that came are kept", !g.whole && g.bytes.size() == 3000); }
  printf("-- chunked\n");
  { FakeStream s; s.data = chunkify(body, 700); s.burst = 1; s.gapLen = 0; Got g = read(s, -1, true);
    check("one byte at a time (headers split anywhere)", g.whole && g.bytes == body); }
  { FakeStream s; s.data = chunkify(body, 1000, ";name=value", true, "X-Trailer: yes\r\n"); s.burst = 97; s.gapLen = 2; Got g = read(s, -1, true);
    check("upper-case hex sizes, chunk extensions and trailers are fine", g.whole && g.bytes == body);
    check("and it slept at every wait", s.maxSpins <= 1); }
  { FakeStream s; s.data = std::string("zz\r\nhello\r\n"); Got g = read(s, -1, true); check("a garbage chunk header is refused", !g.whole && g.bytes.empty()); }
  { FakeStream s; s.data = chunkify(body, 700).substr(0, 2000); Got g = read(s, -1, true); check("cut off in the middle of a chunk: incomplete", !g.whole); }
  printf("-- no length\n");
  { FakeStream s; s.data = body; s.burst = 400; s.gapLen = 1; Got g = read(s, -1, false); check("the end of the connection ends the body", g.whole && g.bytes == body); }
  printf("-- trouble\n");
  { FakeStream s; s.data = body.substr(0, 1000); s.stayOpen = true; Got g = read(s, 5000, false, 3000);
    check("a server that goes quiet: gives up after the idle time, keeps what came", !g.whole && g.bytes.size() == 1000 && s.maxSpins <= 1); }
  { FakeStream s; s.data = payload(200000); s.burst = 1; s.gapLen = 1; Got g = read(s, 200000, false, 15000, 2000);
    check("a trickle that never stops is cut off by the total time", !g.whole && g.bytes.size() < 200000); }
  { FakeStream s; s.data = body; Got g = read(s, (long)body.size(), false, 15000, 60000, 1234);
    check("a full buffer stops the read (sink says no): the first 1234 bytes are kept", !g.whole && g.bytes == body.substr(0, 1234)); }
  printf("\n%d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
