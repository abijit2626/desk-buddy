#include "../dewy/face.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace face;
static void save(const char* path, const uint16_t* fb) {
  FILE* f = fopen(path, "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int i = 0; i < W * H; i++) { RGB c = unpack(fb[i]); unsigned char px[3] = { (unsigned char)c.r, (unsigned char)c.g, (unsigned char)c.b }; fwrite(px, 1, 3, f); }
  fclose(f);
}
int main() {
  std::vector<uint16_t> buf(W * H); Canvas cv; cv.fb = buf.data();
  int classic = -1; for (int i = 0; i < NUM_PALETTES; i++) if (!strcmp(PALETTES[i].name, "Classic")) classic = i;
  struct T { const char* tag; int pal; Mood m; int shape; } tests[] = {
    { "classic_acorn", classic, HAPPY, 1 }, { "classic_round", classic, HAPPY, 0 }, { "classic_cat", classic, HAPPY, 2 },
    { "acorn_surprised", 0, SURPRISED, 1 }, { "acorn_listening", 2, LISTENING, 1 }, { "acorn_bored", 4, BORED, 1 },
    { "acorn_annoyed", 6, ANNOYED, 1 }, { "acorn_thinking", 8, THINKING, 1 }, { "acorn_alarm", 10, ALARM, 1 },
    { "acorn_socks", 30, HAPPY, 1 }, { "acorn_blossom", 21, HAPPY, 1 }, { "acorn_clock", 2, CLOCK, 1 } };
  int k = 0;
  for (auto& t : tests) {
    Face f; f.begin(77); f.setPalette(t.pal);
    if (t.shape == 1) f.setAcorn(true); if (t.shape == 2) f.setCat(true);
    f.setMood(t.m); for (int i = 0; i < 90; i++) f.update(16);
    f.render(cv, "12:30");
    char n[64]; snprintf(n, sizeof n, "acorn/%02d_%s.ppm", k++, t.tag); save(n, buf.data());
  }
  Face f; f.begin(9); int cat = 0, acorn = 0, N = 20000;
  for (int i = 0; i < N; i++) { f.sleep(); f.update(16); f.wake(); f.update(16); cat += f.isCat(); acorn += f.isAcorn(); }
  printf("shapes over %d wake-ups: cat %.1f%%, acorn %.1f%%, round %.1f%%\n", N, 100.0 * cat / N, 100.0 * acorn / N, 100.0 * (N - cat - acorn) / N);
}
