// Dewy voice, stage 3: the "Hey Dewy" wake word, running on the ESP32 itself.
// Same pipeline as microWakeWord training and voice/desktop_dewy.py:
//   16 kHz audio -> micro frontend (40 features every 10 ms) -> int8 -> the streaming model every 30 ms
//   -> average of the last WAKE_WINDOW scores against WAKE_CUTOFF.
// Library: TensorFlowLite_ESP32 (Library Manager). The model is in hey_dewy_model.h.
// Serial commands (typed in the Serial Monitor): "ww on", "ww off", "ww debug" (prints the scores every second),
// "ww" (shows the settings), "ww cutoff 0.9", "ww gain 8", "ww reset". Cutoff and gain are saved and survive a restart.
#pragma once
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <TensorFlowLite_ESP32.h>
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"
#include "hey_dewy_model.h"

#ifndef WAKE_CUTOFF
#define WAKE_CUTOFF 0.98f     // same as the round 2 tests. Lower = easier to wake, more false wakes.
#endif
#ifndef WAKE_WINDOW
#ifndef WAKE_GAIN
#define WAKE_GAIN 4           // the wake word hears the mic 4x louder than recording does: MIC_SHIFT went from 14 to 16
                              // for recording, and at 14 "Hey Dewy" woke it 5/5 in a normal voice (at 16 it needed shouting)
#endif
#define WAKE_WINDOW 5         // average of the last 5 scores (5 x 30 ms)
#endif
#define WAKE_ARENA_BYTES  (64 * 1024)   // memory for the model; the real need is printed at start-up
#define WAKE_FEATURES     40
#define WAKE_STRIDE       3             // feature frames per model run (the model input is 1 x 3 x 40)
#define WAKE_WARMUP_RUNS  50            // ignore the first 1.5 s after a reset while the model's memory fills

// ---------------------------------------------------------------- features (the micro frontend)
static FrontendState wakeFe;
static bool wakeFeOk = false;

static bool wakeFrontendBegin() {
  FrontendConfig c;
  FrontendFillConfigWithDefaults(&c);
  c.window.size_ms = 30;
  c.window.step_size_ms = 10;
  c.filterbank.num_channels = WAKE_FEATURES;
  c.filterbank.lower_band_limit = 125.0f;
  c.filterbank.upper_band_limit = 7500.0f;
  c.noise_reduction.smoothing_bits = 10;
  c.noise_reduction.even_smoothing = 0.025f;
  c.noise_reduction.odd_smoothing = 0.06f;
  c.noise_reduction.min_signal_remaining = 0.05f;
  c.pcan_gain_control.enable_pcan = 1;
  c.pcan_gain_control.strength = 0.95f;
  c.pcan_gain_control.offset = 80.0f;
  c.pcan_gain_control.gain_bits = 21;
  c.log_scale.enable_log = 1;
  c.log_scale.scale_shift = 6;
  wakeFeOk = FrontendPopulateState(&c, &wakeFe, 16000) != 0;
  return wakeFeOk;
}

// The frontend gives 0..~670 per feature. Training sees them as v / 25.6, and the model's input
// is int8 with scale 0.10196079 and zero point -128 (read from hey_dewy_model). Same rounding as training.
static inline int8_t wakeQuantize(uint16_t v) {
  int32_t q = (int32_t)lroundf(v * (0.0390625f / 0.10196079f)) - 128;
  if (q < -128) q = -128;
  if (q > 127) q = 127;
  return (int8_t)q;
}

// Feeds audio. Calls onFrame(40 int8 features) for every new 10 ms frame. Returns how many frames came out.
template <typename F>
static int wakeFrontendFeed(const int16_t* s, int n, F onFrame) {
  int frames = 0;
  while (n > 0) {
    size_t used = 0;
    FrontendOutput out = FrontendProcessSamples(&wakeFe, s, (size_t)n, &used);
    if (used == 0 && out.size == 0) break;
    s += used;
    n -= (int)used;
    if (out.size == WAKE_FEATURES) {
      int8_t f[WAKE_FEATURES];
      for (int i = 0; i < WAKE_FEATURES; i++) f[i] = wakeQuantize(out.values[i]);
      onFrame(f);
      frames++;
    }
  }
  return frames;
}

#ifndef WAKE_HOST_TEST   // everything below runs only on the ESP32
#include <Preferences.h>
// ---------------------------------------------------------------- the model
static tflite::MicroInterpreter* wakeNet = nullptr;
static TfLiteTensor* wakeIn = nullptr;
static TfLiteTensor* wakeOut = nullptr;
static bool wakeOk = false;
static volatile bool wakeOn = true, wakeDebug = false;
static int8_t wakeFrames[WAKE_STRIDE][WAKE_FEATURES];
static int wakeFrameN = 0, wakeWarm = WAKE_WARMUP_RUNS;
static uint8_t wakeScores[WAKE_WINDOW];
static int wakeScoreN = 0;
static uint8_t wakeBest = 0, wakeBestAvg = 0;   // highest single score and highest average in the last second
static float wakeCutoff = WAKE_CUTOFF;         // tuned with "ww cutoff", kept in flash
static float wakeGain = WAKE_GAIN;             // tuned with "ww gain", kept in flash
static void wakeLoadTuning() {
  Preferences p;
  if (!p.begin("wake", true)) return;           // nothing saved yet: the defaults above
  float c = p.getFloat("cutoff", WAKE_CUTOFF), g = p.getFloat("gain", WAKE_GAIN);
  p.end();
  if (c >= 0.5f && c <= 0.99f) wakeCutoff = c;
  if (g >= 1.0f && g <= 16.0f) wakeGain = g;
}
static void wakeSaveTuning() {
  Preferences p;
  if (!p.begin("wake", false)) return;
  p.putFloat("cutoff", wakeCutoff); p.putFloat("gain", wakeGain);
  p.end();
}
static uint32_t wakeBestAt = 0, wakeRunUs = 0;

static bool wakeBegin() {
  if (!wakeFrontendBegin()) { Serial.println("[wake] frontend setup failed"); return false; }
  const tflite::Model* model = tflite::GetModel(hey_dewy_model);
  if (model->version() != TFLITE_SCHEMA_VERSION) { Serial.println("[wake] the model file has the wrong version"); return false; }
  static tflite::MicroErrorReporter reporter;
  static tflite::MicroMutableOpResolver<13> ops;
  ops.AddAssignVariable(); ops.AddCallOnce(); ops.AddConcatenation(); ops.AddConv2D();
  ops.AddDepthwiseConv2D(); ops.AddFullyConnected(); ops.AddLogistic(); ops.AddQuantize();
  ops.AddReadVariable(); ops.AddReshape(); ops.AddSplitV(); ops.AddStridedSlice(); ops.AddVarHandle();
  uint8_t* arena = (uint8_t*)heap_caps_aligned_alloc(16, WAKE_ARENA_BYTES, MALLOC_CAP_SPIRAM);
  if (!arena) { Serial.println("[wake] no memory for the model (needs PSRAM)"); return false; }
  tflite::MicroAllocator* alloc = tflite::MicroAllocator::Create(arena, WAKE_ARENA_BYTES, &reporter);
  tflite::MicroResourceVariables* vars = tflite::MicroResourceVariables::Create(alloc, 20);
  static tflite::MicroInterpreter net(model, ops, alloc, &reporter, vars);
  if (net.AllocateTensors() != kTfLiteOk) { Serial.println("[wake] AllocateTensors failed: raise WAKE_ARENA_BYTES"); return false; }
  wakeNet = &net;
  wakeIn = net.input(0);
  wakeOut = net.output(0);
  if (wakeIn->bytes != WAKE_STRIDE * WAKE_FEATURES) { Serial.printf("[wake] unexpected model input size %u\n", (unsigned)wakeIn->bytes); return false; }
  wakeLoadTuning();
  Serial.printf("[wake] ready: model uses %u bytes of its %u byte arena, cutoff %.2f, gain %.1f\n", (unsigned)net.arena_used_bytes(), (unsigned)WAKE_ARENA_BYTES, wakeCutoff, wakeGain);
  wakeOk = true;
  return true;
}

// Clears the model's memory and the score window (after Dewy speaks, so it doesn't hear itself).
static void wakeReset() {
  if (!wakeOk) return;
  wakeNet->ResetVariableTensors();
  FrontendReset(&wakeFe);
  wakeFrameN = 0;
  wakeScoreN = 0;
  wakeWarm = WAKE_WARMUP_RUNS;
}

// Feeds microphone audio (16 kHz). Returns true when "Hey Dewy" was heard.
static bool wakeFeed(const int16_t* in, int n) {
  if (!wakeOk || !wakeOn) return false;
  if (n > 256) n = 256;                    // the voice task sends 160 at a time
  int16_t s[256];
  for (int i = 0; i < n; i++) { int v = int(in[i] * wakeGain); s[i] = v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
  bool heard = false;
  wakeFrontendFeed(s, n, [&](const int8_t* f) {
    memcpy(wakeFrames[wakeFrameN++], f, WAKE_FEATURES);
    if (wakeFrameN < WAKE_STRIDE) return;
    wakeFrameN = 0;
    memcpy(wakeIn->data.int8, wakeFrames, WAKE_STRIDE * WAKE_FEATURES);
    uint32_t t0 = micros();
    if (wakeNet->Invoke() != kTfLiteOk) return;
    wakeRunUs = micros() - t0;
    if (wakeRunUs > 300000) Serial.printf("[wake] one run was held up for %lu ms\n", (unsigned long)(wakeRunUs / 1000));
    uint8_t p = wakeOut->data.uint8[0];
    if (wakeWarm > 0) { wakeWarm--; return; }
    if (p > wakeBest) wakeBest = p;
    wakeScores[wakeScoreN % WAKE_WINDOW] = p;
    wakeScoreN++;
    if (wakeScoreN < WAKE_WINDOW) return;
    uint32_t sum = 0;
    for (int i = 0; i < WAKE_WINDOW; i++) sum += wakeScores[i];
    uint8_t avg = uint8_t(sum / WAKE_WINDOW);
    if (avg > wakeBestAvg) wakeBestAvg = avg;
    if (avg / 255.0f >= wakeCutoff) heard = true;
  });
  if (wakeDebug && millis() - wakeBestAt > 1000) {
    Serial.printf("[wake] best score %.2f, best average %.2f (wakes at %.2f), one run takes %lu us\n",
                  wakeBest / 255.0f, wakeBestAvg / 255.0f, wakeCutoff, (unsigned long)wakeRunUs);
    wakeBest = 0; wakeBestAvg = 0;
    wakeBestAt = millis();
  }
  if (heard) { Serial.println("[wake] Hey Dewy!"); wakeReset(); }
  return heard;
}

// Serial commands. Returns true if it was a wake word command.
static bool wakeCommand(const String& cmd) {
  if (cmd == "ww on") wakeOn = true;
  else if (cmd == "ww off") wakeOn = false;
  else if (cmd == "ww debug") wakeDebug = !wakeDebug;
  else if (cmd.startsWith("ww cutoff ")) {
    float c = cmd.substring(10).toFloat();
    if (c < 0.5f || c > 0.99f) { Serial.println("[wake] cutoff must be 0.50 to 0.99 (lower = wakes more easily, and by mistake more often)"); return true; }
    wakeCutoff = c; wakeSaveTuning();
  }
  else if (cmd.startsWith("ww gain ")) {
    float g = cmd.substring(8).toFloat();
    if (g < 1.0f || g > 16.0f) { Serial.println("[wake] gain must be 1 to 16 (how much louder the wake word hears the mic)"); return true; }
    wakeGain = g; wakeSaveTuning();
  }
  else if (cmd == "ww reset") { wakeCutoff = WAKE_CUTOFF; wakeGain = WAKE_GAIN; wakeSaveTuning(); }
  else if (cmd != "ww") return false;
  Serial.printf("[wake] %s, debug %s, cutoff %.2f, gain %.1f\n", wakeOn ? "listening" : "off", wakeDebug ? "on" : "off", wakeCutoff, wakeGain);
  return true;
}
#endif  // WAKE_HOST_TEST
