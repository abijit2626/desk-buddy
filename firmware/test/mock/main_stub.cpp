#include "Arduino.h"
#include "Wire.h"
SerialT Serial; TwoWire Wire; int g_wifiStatus = 3;
#include "../../dewy/dewy.ino"
namespace lgfx { inline namespace v1 { namespace fonts { const GFXfont FreeSansBold12pt7b{}, FreeSansBold18pt7b{}, FreeSansBold24pt7b{}; } } }
int main() {
  setup();
  const char* cmds[] = { "help", "8ball", "weather rain", "timer 1", "timer stop", "timer", "remind water", "Note Hello There", "note Hi \xE2\x80\x9Cyou\xE2\x80\x9D",
                         "bday", "bday own", "battery 15", "battery auto", "affection 0.6", "tilt 0.5 0", "tilt off", "facedown on", "facedown off",
                         "upside on", "upside off", "cal up", "cal face", "axes", "wifi", "settime 07:30", "weather show" };
  for (const char* c : cmds) { printf("> %s\n", c); handleCommand(String(c)); for (int i = 0; i < 20; i++) loop(); }
  printf("weather parse: rain=%d sun=%d none(night)=%d cloud=%d bad=%d\n",
    parseWeather("{\"current\":{\"weather_code\":61,\"is_day\":1}}"), parseWeather("{\"current\":{\"weather_code\":0,\"is_day\":1}}"),
    parseWeather("{\"current\":{\"weather_code\":1,\"is_day\":0}}"), parseWeather("{\"current\":{\"weather_code\":45,\"is_day\":1}}"), parseWeather("oops"));
  return 0;
}
