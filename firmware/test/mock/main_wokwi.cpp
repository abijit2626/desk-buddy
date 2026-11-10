#include "Arduino.h"
#include "Wire.h"
SerialT Serial; TwoWire Wire;
#include "../../wokwi/sketch.ino"
namespace lgfx { inline namespace v1 { namespace fonts { const GFXfont FreeSansBold12pt7b{}, FreeSansBold18pt7b{}, FreeSansBold24pt7b{}; } } }
int main() { setup(); for (int i = 0; i < 5; i++) loop(); return 0; }
