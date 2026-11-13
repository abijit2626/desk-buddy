#!/usr/bin/env bash
# Runs every PC test for Dewy's firmware. Needs g++ and git (to fetch ArduinoJson the first time).
set -euo pipefail
cd "$(dirname "$0")"
DEPS=.deps
if [ ! -d "$DEPS/ArduinoJson" ]; then
  mkdir -p "$DEPS" && git clone --quiet --depth 1 --branch v7.4.2 https://github.com/bblanchon/ArduinoJson.git "$DEPS/ArduinoJson"
fi
AJ="$DEPS/ArduinoJson/src"
CXX="g++ -std=c++17 -Wall -Wextra"
mkdir -p out feat
echo "== compile checks (real sketch, stand-in libraries)"
$CXX -Imock -I"$AJ" -o mock/sketch_check mock/main_stub.cpp && ./mock/sketch_check > /dev/null && echo "  dewy.ino ok"
$CXX -Imock -I"$AJ" -o mock/wokwi_check mock/main_wokwi.cpp && ./mock/wokwi_check > /dev/null && echo "  wokwi/sketch.ino ok"
echo "== motion"
$CXX -O2 -Imock/mt -Imock -o mock/motion_test mock/motion_test.cpp && ./mock/motion_test | tail -4
echo "== lean, face down, upside down"
$CXX -Iot -Imock -I"$AJ" -o orient_bin orient_test.cpp && ./orient_bin | grep -E "^-- |showing|faceDown=|upside="
echo "== cloud: sync, pairing, notes, settings, firmware check, setup page"
$CXX -Icloudmock -Imock -I"$AJ" -o cloud_bin cloud_test.cpp && ./cloud_bin | grep -E "FAIL|passed"
echo "== voice: speech detector, reading replies, the voice code with a pretend mic, speaker and server"
$CXX -o vad_bin vad_test.cpp && ./vad_bin | grep -E "FAIL|passed"
$CXX -Imock -o reply_bin reply_test.cpp && ./reply_bin | grep -E "FAIL|passed"
$CXX -Icloudmock -Imock -I"$AJ" -o voice_bin voice_test.cpp && ./voice_bin | grep -E "FAIL|passed"
echo "== strips: drawing in strips matches the whole frame"
g++ -std=c++17 -O2 -o strips_bin strips_test.cpp && ./strips_bin | grep -E "FAIL|passed"
echo "== bond: levels, fading after days apart, welcomes"
$CXX -o bond_bin bond_test.cpp && ./bond_bin | grep -E "FAIL|passed"
echo "== face renders (feat/*.ppm) and speed"
g++ -std=c++17 -O2 -o features_bin features.cpp && ./features_bin | tail -1
g++ -std=c++17 -O2 -o timing timing.cpp && ./timing
