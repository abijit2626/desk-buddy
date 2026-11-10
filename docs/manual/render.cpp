// Renders face pictures for the manual with the real face engine.
#include "../../firmware/dewy/face.h"
#include <cstdio>
#include <vector>
using namespace face;
static void save(const char* path, const uint16_t* fb) {
  FILE* f = fopen(path, "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int i = 0; i < W * H; i++) { RGB c = unpack(fb[i]); unsigned char px[3] = { (unsigned char)c.r, (unsigned char)c.g, (unsigned char)c.b }; fwrite(px, 1, 3, f); }
  fclose(f);
}
int main() {
  std::vector<uint16_t> buf(W * H); Canvas cv; cv.fb = buf.data(); char name[96];
  const Mood moods[] = { HAPPY, PETTED, BORED, SLEEPY, ANNOYED, DIZZY, SURPRISED, LISTENING, THINKING, TALKING, ALARM, CLOCK };
  for (Mood m : moods) {
    Face f; f.begin(1234); f.setPalette(0); f.setMood(m);
    int steps = (m == DIZZY) ? 75 : 90;
    for (int i = 0; i < steps; i++) f.update(16);
    f.render(cv, "07:30");
    snprintf(name, sizeof name, "raw/mood_%s.ppm", MOOD_NAMES[m]); save(name, buf.data());
  }
  struct S { int pal; int shape; } sets[] = { {0,0},{1,2},{4,1},{29,0},{30,2},{33,1},{28,0},{3,2},{38,0},{8,1},{32,0},{5,2} };
  int k = 0;
  for (S s : sets) {
    Face f; f.begin(77 + k); f.setPalette(s.pal); if (s.shape == 1) f.setCat(true); if (s.shape == 2) f.setAcorn(true);
    for (int i = 0; i < 90; i++) f.update(16);
    f.render(cv, "07:30");
    snprintf(name, sizeof name, "raw/set_%02d.ppm", k++); save(name, buf.data());
    printf("%s %s\n", PALETTES[s.pal].name, f.shapeName());
  }
}
