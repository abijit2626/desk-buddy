// Records a short preview from the real face engine (PC only).
#include "../dewy/face.h"
#include <cstdio>
#include <vector>
using namespace face;
int main() {
  std::vector<uint16_t> buf(W * H); Canvas cv; cv.fb = buf.data();
  Face f; f.begin(2026);
  struct A { int at; int what; } script[] = { {2200, 1}, {5600, 2}, {7400, 3}, {9400, 4}, {12600, 5}, {14600, 6}, {17000, 7} };
  int si = 0, frame = 0;
  for (int t = 0; t < 19000; t += 16) {
    while (si < 7 && script[si].at <= t) {
      switch (script[si].what) {
        case 1: f.shake(); break;
        case 2: f.poke(); break;
        case 3: f.sleep(); break;
        case 4: f.wake(); f.update(1); f.setPalette(9); break;   // wakes up odd-eyed (Socks)
        case 5: f.pet(); break;
        case 6: f.sleep(); break;
        case 7: f.wake(); f.update(1); f.setPalette(8); break;   // wakes up as Kite
      }
      si++;
    }
    f.update(16);
    if (t % 64 == 0) {
      f.render(cv, "21:47");
      char n[64]; snprintf(n, sizeof n, "demo_frames/f%04d.ppm", frame++);
      FILE* o = fopen(n, "wb"); fprintf(o, "P6\n%d %d\n255\n", W, H);
      for (int i = 0; i < W * H; i++) { RGB c = unpack(buf[i]); unsigned char p[3] = { (unsigned char)c.r, (unsigned char)c.g, (unsigned char)c.b }; fwrite(p, 1, 3, o); }
      fclose(o);
    }
  }
  printf("%d frames\n", frame);
}
