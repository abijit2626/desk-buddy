// Dewy voice, stages 2 and 3. Needs Wi-Fi, the cloud server, the INMP441 mic and the MAX98357A speaker.
//   Say "Hey Dewy" (wakeword.h), double-tap the screen, or type "ask" on serial: Dewy listens, stops when you stop talking,
//   uploads the audio to /api/device/chat, then plays the spoken reply.
//   Type "mic" on serial to toggle the live loudness bar (the stage 1 mic check).
// The whole turn runs in its own task, so the face keeps animating.
#pragma once
#include <Arduino.h>
#include <driver/i2s.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include "wakeword.h"

// ---- pins. Mic: INMP441 (L/R to GND). Speaker: MAX98357A. ----
#ifndef PIN_MIC_SCK
#define PIN_MIC_SCK 15
#endif
#ifndef PIN_MIC_WS
#define PIN_MIC_WS  16
#endif
#ifndef PIN_MIC_SD
#define PIN_MIC_SD  17
#endif
#ifndef PIN_SPK_BCLK
#define PIN_SPK_BCLK 18
#endif
#ifndef PIN_SPK_LRC
#define PIN_SPK_LRC  21
#endif
#ifndef PIN_SPK_DIN
#define PIN_SPK_DIN  38
#endif

#ifndef MIC_TEST
#define MIC_TEST 0        // 1 = start with the loudness bar on. You can also toggle it with the "mic" command.
#endif
#ifndef MIC_SHIFT
#define MIC_SHIFT 14      // bigger = quieter mic
#endif
#ifndef VOICE_VOLUME
#define VOICE_VOLUME 80   // speaker volume, 0 to 100
#endif
#define VOICE_UA          "Dewy-ESP32/1.0"
#define MIC_RATE          16000
#define MIC_PORT          I2S_NUM_0
#define SPK_PORT          I2S_NUM_1
#define REC_MAX_SAMPLES   (MIC_RATE * 10)          // longest question: 10 s (the server accepts up to 12 s)
#define PLAY_MAX_BYTES    (1024UL * 1024UL)        // longest reply held in memory

enum VoiceState { V_IDLE = 0, V_LISTEN, V_THINK, V_SPEAK };
static volatile int vState = V_IDLE;
static volatile bool vRequested = false;
static bool micOk = false, vReady = false;
static bool micTest = (MIC_TEST != 0);
static int16_t* recBuf = nullptr;
static uint8_t* playBuf = nullptr;
static size_t playLen = 0;

// ---------------------------------------------------------------- microphone
static bool micSetup() {
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
  if (i2s_driver_install(MIC_PORT, &cfg, 0, nullptr) != ESP_OK) { Serial.println("[mic] driver install failed"); return false; }
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = PIN_MIC_SCK;
  pins.ws_io_num = PIN_MIC_WS;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = PIN_MIC_SD;
  if (i2s_set_pin(MIC_PORT, &pins) != ESP_OK) { Serial.println("[mic] pin setup failed"); return false; }
  i2s_zero_dma_buffer(MIC_PORT);
  Serial.println("[mic] ready");
  return true;
}

// Reads up to n (at most 256) samples as 16-bit. waitMs = 0 never waits. Returns how many it got.
static int micRead(int16_t* out, int n, uint32_t waitMs = 0) {
  int32_t raw[256];
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

// ---------------------------------------------------------------- speaker
static bool spkSetup() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = 16000;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;     // we send the same sample on both sides
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  if (i2s_driver_install(SPK_PORT, &cfg, 0, nullptr) != ESP_OK) { Serial.println("[speaker] driver install failed"); return false; }
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = PIN_SPK_BCLK;
  pins.ws_io_num = PIN_SPK_LRC;
  pins.data_out_num = PIN_SPK_DIN;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(SPK_PORT, &pins) != ESP_OK) { Serial.println("[speaker] pin setup failed"); return false; }
  i2s_zero_dma_buffer(SPK_PORT);
  Serial.println("[speaker] ready");
  return true;
}

static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

// Plays the WAV in playBuf. The length fields in the header can be wrong, so the sound
// is everything after the "data" chunk header. The sample rate comes from the header.
static void playReply() {
  if (playLen < 48 || memcmp(playBuf, "RIFF", 4) != 0 || memcmp(playBuf + 8, "WAVE", 4) != 0) {
    Serial.println("[voice] the reply was not a WAV file");
    return;
  }
  uint32_t rate = 16000;
  size_t off = 12, dataAt = 0;
  while (off + 8 <= playLen) {
    uint32_t sz = rd32(playBuf + off + 4);
    if (memcmp(playBuf + off, "fmt ", 4) == 0 && off + 8 + 8 <= playLen) rate = rd32(playBuf + off + 12);
    if (memcmp(playBuf + off, "data", 4) == 0) { dataAt = off + 8; break; }
    off += 8 + (size_t)sz + (sz & 1);
  }
  if (!dataAt || dataAt >= playLen) { Serial.println("[voice] no sound in the reply"); return; }
  if (rate < 8000 || rate > 48000) rate = 16000;
  size_t total = (playLen - dataAt) / 2;
  Serial.printf("[voice] playing %u samples at %u Hz\n", (unsigned)total, (unsigned)rate);
  i2s_set_clk(SPK_PORT, rate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
  int16_t out[512];
  for (size_t i = 0; i < total;) {
    int k = (int)((total - i) < 256 ? (total - i) : 256);
    for (int j = 0; j < k; j++) {
      int16_t s;
      memcpy(&s, playBuf + dataAt + 2 * (i + j), 2);
      int16_t v = (int16_t)(((int32_t)s * VOICE_VOLUME) / 100);
      out[2 * j] = v;
      out[2 * j + 1] = v;
    }
    size_t wr = 0;
    i2s_write(SPK_PORT, out, k * 4, &wr, pdMS_TO_TICKS(1500));
    i += k;
  }
  memset(out, 0, sizeof(out));                 // a little silence at the end, so the speaker doesn't click
  for (int i = 0; i < 6; i++) { size_t wr = 0; i2s_write(SPK_PORT, out, sizeof(out), &wr, pdMS_TO_TICKS(500)); }
  i2s_zero_dma_buffer(SPK_PORT);
}

// ---------------------------------------------------------------- recording
// Waits for you to speak, stops about 1.2 s after you stop. Returns the number of samples in recBuf (0 = nothing heard).
static int recordUtterance() {
  const int FR = 256;                          // 16 ms per frame
  int16_t fr[256];
  int n = 0, frames = 0, loud = 0, quiet = 0, lastLoudEnd = 0;
  bool speech = false;
  float noiseSum = 0, thr = 600;
  uint32_t t0 = millis();
  while (n + FR <= REC_MAX_SAMPLES) {
    if (millis() - t0 > 15000) break;
    int got = micRead(fr, FR, 60);
    if (got <= 0) continue;
    int64_t sq = 0;
    for (int i = 0; i < got; i++) sq += (int32_t)fr[i] * (int32_t)fr[i];
    float rms = sqrtf((float)sq / got);
    frames++;
    if (frames <= 10) continue;                // throw away old audio and the tap sound
    memcpy(recBuf + n, fr, got * sizeof(int16_t));
    n += got;
    if (frames <= 22) {                        // learn the room noise
      noiseSum += rms;
      if (frames == 22) { thr = noiseSum / 12.0f * 3.0f + 150.0f; if (thr < 450) thr = 450; if (thr > 2500) thr = 2500; }
      continue;
    }
    if (rms > thr) {
      quiet = 0;
      if (speech) lastLoudEnd = n;
      else if (++loud >= 3) { speech = true; lastLoudEnd = n; }
    } else {
      loud = 0;
      if (speech) quiet++;
    }
    if (speech && quiet >= 75) break;          // 1.2 s of quiet: done
    if (!speech && millis() - t0 > 6000) return 0;
  }
  if (!speech) return 0;
  n = lastLoudEnd + 4800;                      // keep 0.3 s after the last word
  if (n > REC_MAX_SAMPLES) n = REC_MAX_SAMPLES;
  if (n < 6400) return 0;                      // the server needs at least 0.4 s
  Serial.printf("[voice] recorded %.1f s (threshold %.0f)\n", n / (float)MIC_RATE, thr);
  return n;
}

// ---------------------------------------------------------------- cloud
struct BufStream : public Stream {            // collects the reply into playBuf
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* d, size_t n) override {
    size_t room = playLen < PLAY_MAX_BYTES ? PLAY_MAX_BYTES - playLen : 0;
    size_t c = n < room ? n : room;
    if (c) { memcpy(playBuf + playLen, d, c); playLen += c; }
    return n;                                   // extra bytes beyond the limit are dropped
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};

static String urlDecode(const String& s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '%' && i + 2 < s.length()) {
      char h[3] = { s[i + 1], s[i + 2], 0 };
      out += (char)strtol(h, nullptr, 16);
      i += 2;
    } else out += c;
  }
  return out;
}

// Sends the recording and collects the reply. Returns true when playBuf holds a WAV to play.
static bool talkToCloud(int nSamples) {
  uint32_t freeRam = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  if (freeRam < 55000) { Serial.printf("[voice] not enough free memory for HTTPS (%u bytes)\n", (unsigned)freeRam); return false; }
  WiFiClientSecure client;
  client.setCACert(CA_BUNDLE);
  client.setTimeout(20);
  HTTPClient http;
  http.setTimeout(30000);
  http.setUserAgent(VOICE_UA);
  String url = String(DEWY_SERVER) + "/api/device/chat";
  if (!http.begin(client, url)) { Serial.println("[voice] could not start the request"); return false; }
  http.addHeader("Authorization", String("Bearer ") + deviceToken);
  http.addHeader("Content-Type", "audio/pcm");
  http.addHeader("X-Dewy-Version", DEWY_VERSION);
  const char* keys[] = { "x-dewy-heard", "x-dewy-reply", "Content-Type" };
  http.collectHeaders(keys, 3);
  uint32_t t0 = millis();
  int code = http.POST((uint8_t*)recBuf, (size_t)nSamples * 2);
  if (code != 200) {
    Serial.printf("[voice] server answered %d\n", code);
    http.end();
    return false;
  }
  String heard = urlDecode(http.header("x-dewy-heard"));
  String reply = urlDecode(http.header("x-dewy-reply"));
  String ctype = http.header("Content-Type");
  Serial.printf("[voice] heard: %s\n", heard.c_str());
  Serial.printf("[voice] reply: %s\n", reply.c_str());
  bool audio = ctype.startsWith("audio/");
  if (audio) {
    playLen = 0;
    BufStream bs;
    http.writeToStream(&bs);
  }
  http.end();
  Serial.printf("[voice] answer took %lu ms, %u bytes of sound\n", (unsigned long)(millis() - t0), (unsigned)playLen);
  return audio && playLen > 48;
}

static bool voiceRequest();

static void voiceTask(void*) {
  int16_t chunk[160];                          // 10 ms of audio
  for (;;) {
    if (!vRequested) {
      if (micTest || !wakeOk || !wakeOn) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
      int got = micRead(chunk, 160, 50);       // listen for "Hey Dewy" all the time while idle
      if (got > 0 && wakeFeed(chunk, got) && !voiceRequest())
        Serial.println("[wake] heard you, but can't talk now (no Wi-Fi or server, alarm ringing, or face down)");
      continue;
    }
    vState = V_LISTEN;
    int n = recordUtterance();
    if (n <= 0) Serial.println("[voice] heard nothing");
    else {
      vState = V_THINK;
      if (talkToCloud(n)) { vState = V_SPEAK; playReply(); }
    }
    vState = V_IDLE;
    vRequested = false;
    i2s_zero_dma_buffer(MIC_PORT);             // drop audio from while Dewy was talking...
    wakeReset();                               // ...so it doesn't wake itself up
  }
}

// ---------------------------------------------------------------- called from the main sketch
static void voiceBegin() {
  micOk = micSetup();
  bool spkOk = spkSetup();
  recBuf = (int16_t*)heap_caps_malloc(REC_MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  playBuf = (uint8_t*)heap_caps_malloc(PLAY_MAX_BYTES, MALLOC_CAP_SPIRAM);
  Serial.printf("[voice] free internal RAM %u, free PSRAM %u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  if (!recBuf || !playBuf) { Serial.println("[voice] no PSRAM: set Tools > PSRAM > OPI PSRAM. Voice is off."); return; }
  if (!micOk || !spkOk) { Serial.println("[voice] mic or speaker setup failed. Voice is off."); return; }
  if (!wakeBegin()) Serial.println("[wake] wake word is off; double-tap or \"ask\" still work");
  xTaskCreatePinnedToCore(voiceTask, "voice", 20480, nullptr, 1, nullptr, 0);
  vReady = true;
}

// Starts a conversation. Returns false if it can't (busy, no Wi-Fi, not registered with the server, alarm ringing).
static bool voiceRequest() {
  if (!vReady || vState != V_IDLE || vRequested) return false;
  if (buddy.mood() == face::ALARM || buddy.faceDown()) return false;
  if (WiFi.status() != WL_CONNECTED || !cloudOn() || !deviceToken[0]) return false;
  vRequested = true;
  return true;
}

static void voicePoll(uint32_t now) {
  static int shown = V_IDLE;
  int st = vState;
  if (st != shown) {                            // follow the voice task with the face
    shown = st;
    interacted();
    if (st == V_LISTEN) buddy.listen();
    else if (st == V_THINK) buddy.setMood(face::THINKING);
    else if (st == V_SPEAK) buddy.talk();
    else buddy.setMood(face::HAPPY);
  }
  if (!micOk || !micTest || st != V_IDLE || vRequested) return;
  // live loudness bar (the mic check)
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
