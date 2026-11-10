#include "../dewy/face.h"
#include <cstdio>
#include <vector>
using namespace face;
int main() {
  std::vector<uint16_t> buf(W * H); Canvas cv; cv.fb = buf.data();
  for (int p = 0; p < NUM_PALETTES; p++) for (int cat = 0; cat < 2; cat++) {
    Face f; f.begin(11); f.setPalette(p); f.setCat(cat);
    for (int i = 0; i < 90; i++) f.update(16);
    f.render(cv, "00:00");
    char n[64]; snprintf(n, sizeof n, "sets3/p%02d_%d.ppm", p, cat);
    FILE* o = fopen(n, "wb"); fprintf(o, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) { RGB c = unpack(buf[i]); unsigned char px[3] = { (unsigned char)c.r, (unsigned char)c.g, (unsigned char)c.b }; fwrite(px, 1, 3, o); }
    fclose(o);
    if (!cat) printf("%s|%d\n", PALETTES[p].name, PALETTES[p].weight);
  }
}
