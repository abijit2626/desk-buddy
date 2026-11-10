// PC test: runs the real face engine and writes frames to PPM images.
#include "../dewy/face.h"
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <vector>
using namespace face;

static void save(const char* path, const uint16_t* fb) {
  FILE* f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int i = 0; i < W * H; i++) { RGB c = unpack(fb[i]); unsigned char px[3] = { (unsigned char)c.r, (unsigned char)c.g, (unsigned char)c.b }; fwrite(px, 1, 3, f); }
  fclose(f);
}

int main() {
  std::vector<uint16_t> buf(W * H);
  Canvas cv; cv.fb = buf.data();
  char name[128];
  const Mood moods[] = { HAPPY, PETTED, BORED, SLEEPY, ANNOYED, DIZZY, SURPRISED, LISTENING, THINKING, TALKING, ALARM, CLOCK };
  for (Mood m : moods) {
    Face f; f.begin(1234);
    f.setMood(m);
    for (int i = 0; i < 90; i++) f.update(16);     // let the springs settle (~1.4 s)
    f.render(cv, "21:47");
    snprintf(name, sizeof name, "out/mood_%02d_%s.ppm", int(m), MOOD_NAMES[m]); save(name, buf.data());
  }
  // cat mode + every palette
  for (int p = 0; p < NUM_PALETTES; p++) {
    Face f; f.begin(99); f.setPalette(p); f.setCat(p % 3 == 2);
    for (int i = 0; i < 90; i++) f.update(16);
    f.render(cv, "21:47");
    snprintf(name, sizeof name, "out/pal_%02d.ppm", p); save(name, buf.data());
  }
  // mid-blink frame and a poke squint
  { Face f; f.begin(5); for (int i = 0; i < 112; i++) f.update(16); f.render(cv, "00:00"); save("out/blink.ppm", buf.data()); }
  // palette odds over many wake-ups
  { Face f; f.begin(42); int counts[NUM_PALETTES] = {0}; int cats = 0;
    for (int i = 0; i < 20000; i++) { f.sleep(); f.update(16); f.wake(); f.update(16); counts[&f.palette() - PALETTES]++; cats += f.isCat(); }
    for (int p = 0; p < NUM_PALETTES; p++) printf("%-10s %5.1f%%\n", PALETTES[p].name, counts[p] / 200.0);
    printf("cat %.1f%%\n", cats / 200.0); }
  // rough speed of one frame on this PC (busiest mood)
  { Face f; f.begin(7); f.setMood(ALARM); for (int i = 0; i < 60; i++) f.update(16);
    auto t0 = std::chrono::high_resolution_clock::now(); for (int i = 0; i < 200; i++) { f.update(16); f.render(cv, "07:00"); }
    auto t1 = std::chrono::high_resolution_clock::now();
    printf("render: %.2f ms per frame on this PC\n", std::chrono::duration<double, std::milli>(t1 - t0).count() / 200); }
  return 0;
}
