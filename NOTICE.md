# Third-party parts

Dewy's own code is under [PolyForm Noncommercial 1.0.0](LICENSE), and its designs and docs are under
[CC BY-NC 4.0](LICENSES/CC-BY-NC-4.0.txt). These parts come from other people and keep their own licences.

## Included in this repository

| What | Where | Licence |
|---|---|---|
| Root certificates (from Mozilla's CA list, via certifi) | `firmware/dewy/cacerts.h`, `firmware/wokwi/cacerts.h` | MPL-2.0 |
| FreeSans Bold bitmap fonts (GNU FreeFont, as converted for Adafruit GFX and LovyanGFX), used only by the PC tests | `firmware/test/fonts/` | GPL-3.0 with the font exception |
| Fredoka and Nunito fonts | `cloud/app/fonts/` | SIL Open Font License 1.1 |
| "Hey Dewy" wake word model | `voice/wakeword/model/`, `firmware/dewy/hey_dewy_model.h` | Personal, non-commercial use only. It was trained with [microWakeWord](https://github.com/kahrendt/microWakeWord) (Apache-2.0), using background-sound datasets whose licences include non-commercial terms |

## Used but not included

| What | Licence |
|---|---|
| [Arduino core for the ESP32](https://github.com/espressif/arduino-esp32) | LGPL-2.1 / Apache-2.0 |
| [LovyanGFX](https://github.com/lovyan03/LovyanGFX) | FreeBSD (BSD-2-Clause) |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | MIT |
| [TensorFlowLite_ESP32](https://github.com/tanakamasayuki/Arduino_TensorFlowLite_ESP32) | Apache-2.0 |
| [Cloudflare Wrangler](https://github.com/cloudflare/workers-sdk) and Workers types | MIT / Apache-2.0 |
| [Open-Meteo](https://open-meteo.com) weather data | CC BY 4.0 |
| KiCad (used to check and export the PCB) | GPL-3.0 |
