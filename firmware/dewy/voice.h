// Dewy voice, stage 1: microphone check (INMP441 on I2S).
// With MIC_TEST 1 it prints a live loudness bar on the serial monitor (115200).
#pragma once
#include <Arduino.h>
#include <driver/i2s.h>

// ---- pins (INMP441). Wire L/R to GND so the mic sends on the left channel. ----
#ifndef PIN_MIC_SCK
#define PIN_MIC_SCK 15
#endif
#ifndef PIN_MIC_WS
#define PIN_MIC_WS  16
#endif
#ifndef PIN_MIC_SD
#define PIN_MIC_SD  17
#endif

#ifndef MIC_TEST
#define MIC_TEST 1        // 1 = print the loudness bar. 0 = quiet.
#endif
#ifndef MIC_SHIFT
#define MIC_SHIFT 14      // bigger = quieter. The INMP441 is quiet, so 14 gives good volume.
#endif
#define MIC_RATE 16000
#define MIC_PORT I2S_NUM_0

static bool micOk = false;

static void voiceBegin() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = MIC_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  if (i2s_driver_install(MIC_PORT, &cfg, 0, nullptr) != ESP_OK) { Serial.println("[mic] driver install failed"); return; }
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = PIN_MIC_SCK;
  pins.ws_io_num = PIN_MIC_WS;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = PIN_MIC_SD;
  if (i2s_set_pin(MIC_PORT, &pins) != ESP_OK) { Serial.println("[mic] pin setup failed"); return; }
  i2s_zero_dma_buffer(MIC_PORT);
  micOk = true;
  Serial.println("[mic] ready");
}

// Reads up to n samples as 16-bit. waitMs = 0 never waits. Returns how many it got.
static int micRead(int16_t* out, int n, uint32_t waitMs = 0) {
  static int32_t raw[256];
  if (n > 256) n = 256;
  size_t got = 0;
  i2s_read(MIC_PORT, raw, n * sizeof(int32_t), &got, pdMS_TO_TICKS(waitMs));
  int cnt = (int)(got / sizeof(int32_t));
  for (int i = 0; i < cnt; i++) {
    int32_t v = raw[i] >> MIC_SHIFT;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    out[i] = (int16_t)v;
  }
  return cnt;
}

static void voicePoll(uint32_t now) {
  if (!micOk || !MIC_TEST) return;
  static int16_t buf[256];
  static uint64_t sumSq = 0;
  static uint32_t count = 0, lastPrint = 0, silentSince = 0;
  static int peak = 0;
  int n;
  while ((n = micRead(buf, 256)) > 0) {
    for (int i = 0; i < n; i++) {
      int a = abs((int)buf[i]);
      if (a > peak) peak = a;
      sumSq += (uint64_t)((int32_t)buf[i] * (int32_t)buf[i]);
    }
    count += n;
  }
  if (now - lastPrint < 200) return;
  lastPrint = now;
  int rms = count ? (int)sqrt((double)sumSq / count) : 0;
  char bar[33];
  int len = rms / 150;
  if (len > 30) len = 30;
  for (int i = 0; i < 30; i++) bar[i] = i < len ? '#' : ' ';
  bar[30] = 0;
  Serial.printf("[mic] rms %5d peak %5d |%s|\n", rms, peak, bar);
  if (peak == 0) {
    if (!silentSince) silentSince = now;
    else if (now - silentSince > 3000) { Serial.println("[mic] only zeros: check SD 17, SCK 15, WS 16, 3.3V, GND and L/R to GND"); silentSince = now; }
  } else silentSince = 0;
  sumSq = 0; count = 0; peak = 0;
}
