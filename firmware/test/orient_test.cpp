#include "Arduino.h"
#include "Wire.h"
SerialT Serial; TwoWire Wire; float g_acc[3] = { 0, 0, 1 };
#include "../dewy/dewy.ino"
namespace lgfx { inline namespace v1 { namespace fonts { const GFXfont FreeSansBold12pt7b{}, FreeSansBold18pt7b{}, FreeSansBold24pt7b{}; } } }
static void hold(float x, float y, float z, int loops) { g_acc[0] = x; g_acc[1] = y; g_acc[2] = z; for (int i = 0; i < loops; i++) loop(); }
int main() {
  setup();
  hold(0, 0, 1, 300); handleCommand(String("cal up"));            // upright: sensor z points up
  hold(0, -1, 0, 300); handleCommand(String("cal face"));         // lying on its face: gravity reads along -y
  hold(0, 0, 1, 400); handleCommand(String("axes"));
  float s = sinf(20 * 3.14159f / 180), c = cosf(20 * 3.14159f / 180);
  printf("-- lean right 20 deg (right side lower)\n");  hold(s, 0, c, 200); handleCommand(String("axes"));
  printf("-- lean left 20 deg\n");                      hold(-s, 0, c, 200); handleCommand(String("axes"));
  printf("-- tip back 20 deg (screen faces up)\n");     hold(0, s, c, 200); handleCommand(String("axes"));
  printf("-- tiny lean 4 deg (ignored)\n");             hold(sinf(0.07f), 0, cosf(0.07f), 200); handleCommand(String("axes"));
  printf("-- face down\n");                             hold(0, -1, 0, 300); printf("faceDown=%d mood=%s\n", buddy.faceDown(), face::MOOD_NAMES[buddy.mood()]);
  printf("-- stand up\n");                              hold(0, 0, 1, 300); printf("faceDown=%d mood=%s\n", buddy.faceDown(), face::MOOD_NAMES[buddy.mood()]);
  printf("-- upside down\n");                           hold(0, 0, -1, 300); printf("upside=%d mood=%s\n", buddy.upsideDown(), face::MOOD_NAMES[buddy.mood()]);
  printf("-- upright again\n");                         hold(0, 0, 1, 300); printf("upside=%d mood=%s\n", buddy.upsideDown(), face::MOOD_NAMES[buddy.mood()]);
  return 0;
}
