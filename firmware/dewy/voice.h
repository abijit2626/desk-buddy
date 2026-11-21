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
#include "vad.h"
#include "replyreader.h"

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
#define MIC_SHIFT 16      // bigger = quieter mic. Measured on the real INMP441 at 14: a quiet room read 1200 to 3000 and
                          // close talking clipped at 32767, so the end of a question was never found. 16 = a quarter of that.
#endif
#ifndef ALARM_VOLUME
#define ALARM_VOLUME 70   // the alarm's loudest beep, 0 to 100. It starts at about a third of this and gets louder over a minute
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
static volatile bool vByWake = false;       // this turn was started by "Hey Dewy" (not a tap or the serial command)
static volatile bool vCancel = false;       // the main loop asks the voice task to drop the turn (alarm, face down, a poking burst)
static uint32_t vTapAt = 0;                 // when a double-tap last started a turn (main loop only)
static char vNotice[128] = "";              // something to show on screen after a turn that went wrong, written by the voice task
static volatile bool vNoticeNew = false;
static volatile bool vWakeUp = false;       // "Hey Dewy" while asleep: the main loop wakes the face, nothing else
static volatile bool vShowClock = false;    // the question was about the time: show the clock after the answer
static uint32_t vEndedAt = 0;               // when the last turn ended (main loop only): the speaker can rock Dewy
static NoiseTracker noiseTrack;             // the room's background level while idle (voice task only)
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

static float rmsOf(const int16_t* x, int n) {
  int64_t sq = 0;
  for (int i = 0; i < n; i++) sq += (int32_t)x[i] * (int32_t)x[i];
  return n > 0 ? sqrtf((float)sq / n) : 0.0f;
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
  for (size_t i = 0; i < total && !vCancel;) {
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
// The speech detector is in vad.h. After "Hey Dewy" very little is skipped, so the start of the question isn't lost.
static int recordUtterance(bool byWake) {
  const int FR = 256;                          // 16 ms per frame
  int16_t fr[256];
  Vad vad;
  vad.begin(noiseTrack.ready() ? noiseTrack.level : 0, byWake ? 2 : 10);   // a tap's click needs 10 frames to die away
  int n = 0, lastLoudEnd = 0;
  uint32_t t0 = millis();
  while (n + FR <= REC_MAX_SAMPLES && !vCancel) {
    if (millis() - t0 > 15000) break;
    int got = micRead(fr, FR, 60);
    if (got <= 0) continue;
    Vad::Verdict v = vad.feed(rmsOf(fr, got));
    if (v.keep) { memcpy(recBuf + n, fr, got * sizeof(int16_t)); n += got; }
    if (v.loud && vad.speech()) lastLoudEnd = n;
    if (v.done) break;
    if (!vad.speech() && millis() - t0 > 6000) return 0;
  }
  if (vCancel || !vad.speech()) return 0;
  int stored = n;
  n = lastLoudEnd + 4800;                      // keep 0.3 s after the last word
  if (n > stored) n = stored;                  // but never more than was recorded
  if (n < 6400) return 0;                      // the server needs at least 0.4 s
  Serial.printf("[voice] recorded %.1f s (threshold %.0f, background %.0f)\n", n / (float)MIC_RATE, vad.threshold(), noiseTrack.ready() ? noiseTrack.level : -1.0f);
  return n;
}

// ---------------------------------------------------------------- cloud
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

// Something to show on the face once the turn is over (voicePoll puts it on a card), so a refusal isn't silent.
static void say(const char* text) {
  strlcpy(vNotice, text, sizeof vNotice);
  vNoticeNew = true;
}

// The server answered with words only. That is {"error": "..."} for a refusal (like the daily limit) and
// {"heard": "...", "reply": "..."} when nobody spoke or its voice isn't available. Shows what it said, or a fallback.
static void sayServersWords(HTTPClient& http, int code) {
  const char* fallback = code == 401 ? "I lost my link to my server. Try again in a minute."
                       : code > 0     ? "My server had a problem. Try again in a moment."
                                      : "I can't reach my server right now.";
  long size = code > 0 ? http.getSize() : 0;
  if (size > 0 && size <= 1024) {
    JsonDocument d;
    String body = http.getString();
    if (!deserializeJson(d, body.c_str())) {
      const char* msg = d["error"] | (const char*)(d["reply"] | "");
      if (msg[0]) { say(msg); return; }
    }
  }
  say(fallback);
}

// Sends the recording and collects the reply. Returns true when playBuf holds a WAV to play.
static bool talkToCloud(int nSamples) {
  TlsGuard tls(30000);                            // the network task may be mid-sync: wait for its turn
  if (!tls.ok) { Serial.println("[voice] the network is busy"); say("I'm busy right now. Try again in a moment."); return false; }
  uint32_t freeRam = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  if (freeRam < 55000) {
    Serial.printf("[voice] not enough free memory for HTTPS (%u bytes)\n", (unsigned)freeRam);
    say("I'm short of memory. Try again in a moment.");
    return false;
  }
  WiFiClientSecure client;
  client.setCACert(CA_BUNDLE);
  client.setTimeout(20);
  HTTPClient http;
  http.setTimeout(30000);
  http.setUserAgent(VOICE_UA);
  String url = String(DEWY_SERVER) + "/api/device/chat";
  if (!http.begin(client, url)) { Serial.println("[voice] could not start the request"); say("I can't reach my server right now."); return false; }
  http.addHeader("Authorization", String("Bearer ") + deviceToken);
  http.addHeader("Content-Type", "audio/pcm");
  http.addHeader("X-Dewy-Version", DEWY_VERSION);
  const char* keys[] = { "x-dewy-heard", "x-dewy-reply", "Content-Type", "Transfer-Encoding", "x-dewy-show" };
  http.collectHeaders(keys, 5);
  uint32_t t0 = millis();
  int code = http.POST((uint8_t*)recBuf, (size_t)nSamples * 2);
  if (code != 200) {
    Serial.printf("[voice] server answered %d\n", code);
    sayServersWords(http, code);
    http.end();
    return false;
  }
  vShowClock = http.header("x-dewy-show") == "clock";
  String ctype = http.header("Content-Type");
  if (!ctype.startsWith("audio/") && vShowClock) {  // "what's the time?" and nothing else: no answer to say, just the clock
    Serial.printf("[voice] a time question: showing the clock (%lu ms)\n", (unsigned long)(millis() - t0));
    http.end();
    return false;
  }
  if (!ctype.startsWith("audio/")) {              // words only: nobody spoke, the day's voice budget is used up, or its voice failed
    Serial.println("[voice] the server answered in words only");
    sayServersWords(http, code);
    http.end();
    return false;
  }
  String heard = urlDecode(http.header("x-dewy-heard"));
  String reply = urlDecode(http.header("x-dewy-reply"));
  Serial.printf("[voice] heard: %s\n", heard.c_str());
  Serial.printf("[voice] reply: %s\n", reply.c_str());
  playLen = 0;
  bool whole = false;
  auto* body = http.getStreamPtr();
  if (body) {
    bool chunked = strcasecmp(http.header("Transfer-Encoding").c_str(), "chunked") == 0;
    whole = readReplyBody(*body, (long)http.getSize(), chunked, 15000, 60000, [](const uint8_t* d, size_t n) {
      size_t room = PLAY_MAX_BYTES - playLen, c = n < room ? n : room;
      memcpy(playBuf + playLen, d, c);
      playLen += c;
      return c == n;                              // false when the buffer is full: play what fits
    });
  }
  http.end();
  Serial.printf("[voice] answer took %lu ms, %u bytes of sound%s\n", (unsigned long)(millis() - t0), (unsigned)playLen, whole ? "" : " (cut short)");
  return playLen > 48;
}


// ---------------- the alarm: rings through the speaker, and listens for "snooze" or "stop" between the beeps ----------------
// The main loop decides when it rings (alarmRinging) and what snooze and stop do. This task makes the sound, because it owns
// the speaker and the mic. Between beeps it listens: anything said goes to the server, which answers snooze, stop or
// nothing. No "Hey Dewy" needed while it rings.
static volatile bool alarmRinging = false;
static volatile int alarmHeard = 0;            // to the main loop: 1 = snooze, 2 = stop
enum { ALARM_NONE = 0, ALARM_SNOOZE = 1, ALARM_STOP = 2 };

static void spkTone(int freq, int ms, int volPct) {
  static int16_t out[512];
  int total = 16 * ms, edge = 80;              // 5 ms fade in and out, so the beeps don't click
  float ph = 0, step = 2.0f * (float)M_PI * freq / 16000.0f;
  float amp = 32767.0f * 0.6f * volPct / 100.0f;
  for (int i = 0; i < total && alarmRinging;) {
    int k = total - i < 256 ? total - i : 256;
    for (int j = 0; j < k; j++, i++) {
      float env = i < edge ? i / (float)edge : (total - i < edge ? (total - i) / (float)edge : 1.0f);
      int16_t v = freq > 0 ? (int16_t)(sinf(ph) * amp * env) : 0;
      ph += step; if (ph > 2.0f * (float)M_PI) ph -= 2.0f * (float)M_PI;
      out[2 * j] = v; out[2 * j + 1] = v;
    }
    size_t wr = 0;
    i2s_write(SPK_PORT, out, k * 4, &wr, pdMS_TO_TICKS(500));
  }
}

// Sends what was said while the alarm rang. The server only listens for snooze or stop words, speaks nothing back,
// and doesn't count it as one of the day's questions.
static int alarmVoiceCommand(int nSamples) {
  if (WiFi.status() != WL_CONNECTED || !cloudOn() || !deviceToken[0]) return ALARM_NONE;
  TlsGuard tls(8000);
  if (!tls.ok || heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 55000) { Serial.println("[alarm] can't ask the server right now"); return ALARM_NONE; }
  WiFiClientSecure client;
  client.setCACert(CA_BUNDLE);
  HTTPClient http;
  http.setTimeout(12000);
  http.setUserAgent(VOICE_UA);
  if (!http.begin(client, String(DEWY_SERVER) + "/api/device/chat")) return ALARM_NONE;
  http.addHeader("Authorization", String("Bearer ") + deviceToken);
  http.addHeader("Content-Type", "audio/pcm");
  http.addHeader("X-Dewy-Version", DEWY_VERSION);
  http.addHeader("X-Dewy-Context", "alarm");
  int code = http.POST((uint8_t*)recBuf, (size_t)nSamples * 2);
  int act = ALARM_NONE;
  if (code == 200) {
    JsonDocument d;
    String body = http.getString();
    if (!deserializeJson(d, body.c_str())) {
      const char* a = d["action"] | "";
      act = !strcmp(a, "snooze") ? ALARM_SNOOZE : (!strcmp(a, "stop") ? ALARM_STOP : ALARM_NONE);
      Serial.printf("[alarm] heard \"%s\": %s\n", (const char*)(d["heard"] | ""), a[0] ? a : "nothing to do");
    }
  } else Serial.printf("[alarm] server answered %d\n", code);
  http.end();
  return act;
}

static void alarmRingLoop() {
  uint32_t t0 = millis();
  int16_t fr[256];
  Serial.println("[alarm] ringing: say snooze or stop");
  while (alarmRinging) {
    uint32_t el = millis() - t0;
    int vol = (int)(ALARM_VOLUME * (el >= 60000 ? 1.0f : 0.35f + 0.65f * el / 60000.0f));
    spkTone(1760, 110, vol); spkTone(0, 80, 0);
    spkTone(1760, 110, vol); spkTone(0, 80, 0);
    spkTone(2093, 170, vol); spkTone(0, 140, 0);   // the last silence pushes the beep out of the speaker's buffer
    if (!alarmRinging) break;

    // listen for a second and a half; if someone starts talking, keep listening until they stop
    Vad vad;
    vad.begin(noiseTrack.ready() ? noiseTrack.level : 0, 9);   // skip 0.15 s: the end of the beep is still playing
    int n = 0, lastLoudEnd = 0;
    uint32_t l0 = millis();
    while (alarmRinging) {
      uint32_t lt = millis() - l0;
      if (!vad.speech() && lt > 1500) break;    // nobody spoke: beep again
      if (lt > 5000) break;
      int got = micRead(fr, 256, 60);
      if (got <= 0) continue;
      Vad::Verdict v = vad.feed(rmsOf(fr, got));
      if (v.keep && n + got <= REC_MAX_SAMPLES) { memcpy(recBuf + n, fr, got * sizeof(int16_t)); n += got; }
      if (v.loud && vad.speech()) lastLoudEnd = n;
      if (vad.speech() && n - lastLoudEnd > 9600) break;   // 0.6 s quiet after a word: one or two words are enough
    }
    if (!alarmRinging) break;
    if (vad.speech()) {
      n = lastLoudEnd + 4800 < n ? lastLoudEnd + 4800 : n;
      if (n >= 6400) {
        int act = alarmVoiceCommand(n);
        if (act != ALARM_NONE) { alarmHeard = act; break; }
      }
    }
  }
  i2s_zero_dma_buffer(SPK_PORT);
  i2s_zero_dma_buffer(MIC_PORT);
  wakeReset();
}

static bool voiceRequest(bool byWake = false);

static void voiceTask(void*) {
  int16_t chunk[160];                          // 10 ms of audio
  for (;;) {
    if (alarmRinging && !vRequested) { alarmRingLoop(); continue; }
    if (!vRequested) {
      if (micTest) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }   // the mic check on the main loop reads the mic instead
      int got = micRead(chunk, 160, 50);       // always listening while idle: the room's background level, and "Hey Dewy"
      if (got <= 0) continue;
      noiseTrack.feed(rmsOf(chunk, got), got * 1000.0f / MIC_RATE);
      if (wakeFeed(chunk, got) && !voiceRequest(true))
        Serial.println("[wake] heard you, but can't talk now (no Wi-Fi or server, alarm ringing, face down, or updating)");
      continue;
    }
    vState = V_LISTEN;
    int n = recordUtterance(vByWake);
    if (n <= 0) { if (!vCancel) Serial.println("[voice] heard nothing"); }
    else if (!vCancel) {
      vState = V_THINK;
      if (talkToCloud(n) && !vCancel) { vState = V_SPEAK; playReply(); }
    }
    if (vCancel) Serial.println("[voice] turn dropped");
    vState = V_IDLE;
    vRequested = false;
    i2s_zero_dma_buffer(MIC_PORT);             // drop audio from while Dewy was talking...
    wakeReset();                               // ...so it doesn't wake itself up
  }
}

// ---------------------------------------------------------------- called from the main sketch
static void voiceBegin() {
  if (!VOICE_ENABLED) { Serial.println("[voice] off on this board (its mic and speaker pins drive the screen)"); return; }
  micOk = micSetup();
  bool spkOk = spkSetup();
  recBuf = (int16_t*)heap_caps_malloc(REC_MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  playBuf = (uint8_t*)heap_caps_malloc(PLAY_MAX_BYTES, MALLOC_CAP_SPIRAM);
  Serial.printf("[voice] free internal RAM %u, free PSRAM %u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  if (!recBuf || !playBuf) { Serial.println("[voice] no PSRAM: set Tools > PSRAM > OPI PSRAM. Voice is off."); return; }
  if (!micOk || !spkOk) { Serial.println("[voice] mic or speaker setup failed. Voice is off."); return; }
  if (!wakeBegin()) Serial.println("[wake] wake word is off; double-tap or \"ask\" still work");
  // Priority 2, above the network task (1) on the same core: a check-in with the server held the wake word up for
  // 2.6 s at a time at equal priority, and "Hey Dewy" said then was lost. Listening waits on the mic most of the
  // time, so the network task still gets the rest of the core.
  xTaskCreatePinnedToCore(voiceTask, "voice", 20480, nullptr, 2, nullptr, 0);
  vReady = true;
}

// Starts a conversation. Returns false if it can't (busy, no Wi-Fi, not registered with the server, alarm ringing,
// lying face down, or a firmware update is being installed: it restarts Dewy at the end).
static bool voiceRequest(bool byWake) {
  if (!vReady || vState != V_IDLE || vRequested) return false;
  if (buddy.mood() == face::ALARM || buddy.faceDown()) return false;
  if (fwProgress >= 0 || dewyResting) return false;   // switched off in the app: no talking
  if (byWake && buddy.mood() == face::SLEEPY) {      // asleep: "Hey Dewy" just wakes it up, no conversation
    vWakeUp = true;
    return true;
  }
  if (WiFi.status() != WL_CONNECTED || !cloudOn() || !deviceToken[0]) return false;
  vCancel = false;
  vByWake = byWake;
  vRequested = true;
  return true;
}

static void voicePoll(uint32_t now) {
  static int shown = V_IDLE;
  int st = vState;
  // An alarm, or lying face down (quiet time), beats a conversation: drop the turn and leave the face alone.
  if (st != V_IDLE && (buddy.mood() == face::ALARM || buddy.faceDown())) vCancel = true;
  if (st != shown) {                            // follow the voice task with the face
    shown = st;
    interacted();
    if (st == V_IDLE) {
      vEndedAt = now ? now : 1;
      // The turn is over. Put the happy face back only if the face still wears one of the voice faces: an alarm, quiet
      // time, the complaint about being upside down, or the annoyed face from being poked must stay.
      face::Mood m = buddy.mood();
      if (m == face::LISTENING || m == face::THINKING || m == face::TALKING) {
        if (vShowClock && !buddy.upsideDown()) buddy.showTime(10000);   // "what time is it?": the clock stays up for 10 s
        else buddy.setMood(buddy.upsideDown() ? face::ANNOYED : face::HAPPY);
      }
      vShowClock = false;
    } else if (!vCancel && !buddy.upsideDown()) {   // upside down it keeps complaining (it still listens and answers)
      if (st == V_LISTEN) buddy.listen();
      else if (st == V_THINK) buddy.setMood(face::THINKING);
      else if (st == V_SPEAK) buddy.talk();
    }
  }
  if (vWakeUp) {                                // "Hey Dewy" while asleep
    vWakeUp = false;
    if (buddy.mood() == face::SLEEPY) {           // eyes pop open (like a tap), then happy
      interacted(); buddy.play1(face::SURPRISED, 1200, face::HAPPY); Serial.println("[wake] woke up");
    }
  }
  if (vNoticeNew && st == V_IDLE) {             // the turn went wrong: say why on a card, once the face is back
    char msg[sizeof vNotice];
    strlcpy(msg, vNotice, sizeof msg);
    vNoticeNew = false;
    buddy.showNote(msg, "Dewy");
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
  int len = rms / 50;                           // 30 steps = 1500
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
