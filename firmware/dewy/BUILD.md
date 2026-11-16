# Building Dewy's firmware

## Board settings (Arduino IDE, Tools menu)
- Board: ESP32S3 Dev Module (esp32 by Espressif Systems, tested with 3.3.12)
- Flash Size: 16MB (128Mb)
- Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS)
- PSRAM: OPI PSRAM (voice and the wake word need it)

## Libraries (Library Manager)
- LovyanGFX (1.3.0)
- ArduinoJson (7.4.3)
- TensorFlowLite_ESP32 (1.0.0), for the "Hey Dewy" wake word. It needs the fixes below.

## Fixes to TensorFlowLite_ESP32 for esp32 core 3.x (GCC 14)
The library was written for an older compiler. Run these once after installing it,
and again if you ever reinstall or update it (from WSL; on Windows the folder is
`Documents\Arduino\libraries\TensorFlowLite_ESP32`).

```
T=/mnt/c/Users/user/Documents/Arduino/libraries/TensorFlowLite_ESP32
```

1. flatbuffers `span` cannot be assigned:
```
sed -i 's/^  const size_type count_;$/  size_type count_;/; s/^  pointer const data_;$/  pointer data_;/; s/^    count_ = other.count_;$/    count_ = other.count_;\n    return *this;/' "$T/src/third_party/flatbuffers/stl_emulation.h"
```
2. Private `operator delete` breaks placement new (make the macro empty):
```
sed -i 's|^  void operator delete(void\* p) {}$|  /* empty: GCC 14 fix */|' "$T/src/tensorflow/lite/micro/compatibility.h"
```
3. The bundled camera and screen drivers (not used by Dewy) do not build. Move them out of `src`:
```
mkdir -p "$T/unused_drivers" && mv "$T/src/screen" "$T/src/bus" "$T/unused_drivers/"
```

## Compiling from WSL (optional)
The Arduino IDE cannot open `\\wsl.localhost` paths, so compile a copy on the Windows side:
```
CLI="/mnt/c/Users/user/AppData/Local/Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
FQBN="esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi"
mkdir -p /mnt/c/Users/user/Documents/Arduino/dewy
cp ~/desk-buddy/firmware/dewy/*.h ~/desk-buddy/firmware/dewy/*.ino /mnt/c/Users/user/Documents/Arduino/dewy/
cd /mnt/c/Users/user/Documents && "$CLI" compile --fqbn "$FQBN" "C:\Users\user\Documents\Arduino\dewy"
```
Without the `FQBN` options the command line uses the 4MB default and reports the sketch as too big.

## Voice wiring
| Part | Pin | GPIO |
|---|---|---|
| INMP441 mic | SCK / WS / SD | 15 / 16 / 17 (L/R to GND) |
| MAX98357A speaker | BCLK / LRC / DIN | 18 / 21 / 38 |

## Serial commands for voice (115200 baud)
- `mic`: live loudness bar (mic check)
- `ask`: start a conversation (same as double-tap or "Hey Dewy")
- `ww on`, `ww off`, `ww debug` (prints the best wake word score every second)

## Tuning how quietly she can talk
Dewy only starts recording once the sound is louder than a threshold. The threshold follows the room's background
noise, but never goes below `VAD_MIN_THR` (150, in `vad.h`). That number is a guess from the INMP441 datasheet.
On the real mic: type `mic`, talk at her normal distance and read the `rms` numbers. If her speech peaks below about
200, lower `VAD_MIN_THR` (or `MIC_SHIFT`, which makes the whole mic louder); if the room alone reaches it, raise it.
Each recording also prints the threshold and background it used: `[voice] recorded 1.8 s (threshold 150, background 12)`.

## PC tests
`bash firmware/test/run_tests.sh` builds the sketch on a PC with stand-in libraries and runs the tests, including
`vad_test` (speech detector), `reply_test` (reading the reply) and `voice_test` (the voice code with a pretend mic,
speaker and server). They check the logic only, not the real I2S, TLS or timing.

## Updating the wake word model
`hey_dewy_model.h` is made from `stream_state_internal_quant.tflite`. After a new training round, regenerate it:
```
python3 -c "import sys;b=open(sys.argv[1],'rb').read();print('#pragma once\n#include <stdint.h>\nalignas(16) static const uint8_t hey_dewy_model[%d] = {'%len(b));[print('  '+', '.join('0x%02x'%x for x in b[i:i+20])+',') for i in range(0,len(b),20)];print('};')" model.tflite > hey_dewy_model.h
```
