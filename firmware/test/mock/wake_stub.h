#pragma once
// For PC tests: stands in for the model half of wakeword.h (which needs the real TensorFlow Lite library).
// Include it BEFORE dewy.ino and define WAKE_HOST_TEST first.
#define WAKE_HOST_TEST
#include <Arduino.h>
[[maybe_unused]] static bool wakeOk = false;
[[maybe_unused]] static volatile bool wakeOn = true, wakeDebug = false;
[[maybe_unused]] static int g_wakeResets = 0;
[[maybe_unused]] static bool g_wakeSays = false;                       // tests set this to make the next wakeFeed() "hear" Hey Dewy
[[maybe_unused]] static bool wakeBegin() { return false; }
[[maybe_unused]] static void wakeReset() { g_wakeResets++; }
[[maybe_unused]] static bool wakeFeed(const int16_t*, int) { bool h = g_wakeSays; g_wakeSays = false; return h; }
[[maybe_unused]] static bool wakeCommand(const String&) { return false; }
