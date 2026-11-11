// Dewy's real voice code (voice.h, and its hooks in dewy.ino and cloud.h) on a PC, with a pretend microphone, speaker
// and server. The voice task itself never runs here: each test sets vState the way the task would.
#include "Arduino.h"
#include "Wire.h"
#include "wake_stub.h"
SerialT Serial; TwoWire Wire; int g_wifiStatus = 3;
#include "../dewy/dewy.ino"
namespace lgfx { inline namespace v1 { namespace fonts { const GFXfont FreeSansBold12pt7b{}, FreeSansBold18pt7b{}, FreeSansBold24pt7b{}; } } }
#include <cmath>

static int passed = 0, failed = 0;
static void check(const char* name, bool ok) { printf("  %s %s\n", ok ? "ok  " : "FAIL", name); (ok ? passed : failed)++; }

// a short tap whose release lands at time t + 100
static void tapAt(uint32_t t) { g_touch = 1; pollTouch(t); pollTouch(t + 20); g_touch = 0; pollTouch(t + 100); }
static void idle() { vState = V_IDLE; vRequested = false; vCancel = false; vTapAt = 0; lastTapAt = 0; vNoticeNew = false; buddy.reset(); }
static uint32_t pollAt = 100000;
static void poll() { voicePoll(pollAt); pollAt += 50; }

static std::string wav(int samples, uint32_t rate) {
  std::string w(44 + samples * 2, '\0');
  auto put32 = [&](int o, uint32_t v) { for (int i = 0; i < 4; i++) w[o + i] = char(v >> (8 * i)); };
  memcpy(&w[0], "RIFF", 4); put32(4, 0xFFFFFFFF); memcpy(&w[8], "WAVEfmt ", 8); put32(16, 16);
  w[20] = 1; w[22] = 1; put32(24, rate); put32(28, rate * 2); w[32] = 2; w[34] = 16;
  memcpy(&w[36], "data", 4); put32(40, 0xFFFFFFFF);                 // wrong lengths on purpose, like the real server's may be
  for (int i = 0; i < samples; i++) { int16_t v = int16_t(8000 * sin(i * 0.05)); memcpy(&w[44 + 2 * i], &v, 2); }
  return w;
}

int main() {
  setup();
  buddy.clearBanner();
  strcpy(deviceToken, "tok_abcdefghijklmnopqrstuvwxyz0123456789");
  check("voice is ready (mic, speaker, memory)", vReady);

  printf("-- five quick pokes (item 7)\n");
  idle();
  tapAt(1000);                                       // poke 1
  tapAt(1300);                                       // the second tap looks like a double-tap: a conversation starts
  check("the second quick tap starts listening", vRequested && !vByWake);
  vState = V_LISTEN; poll();
  tapAt(1600);                                       // a third tap: this is poking
  check("the third tap drops the conversation", vCancel);
  vState = V_IDLE; vRequested = false; poll();       // the voice task stops
  check("the listening face goes away again", buddy.mood() == face::HAPPY);
  tapAt(1900);                                       // poke 4: no new conversation during the burst
  check("a fourth quick tap doesn't start another conversation", !vRequested);
  tapAt(2200);                                       // poke 5
  check("five quick pokes: annoyed, as before voice existed", buddy.mood() == face::ANNOYED);

  printf("-- a real double-tap\n");
  idle();
  tapAt(10000); tapAt(10300);
  check("a double-tap still starts a conversation", vRequested);
  vState = V_THINK; tapAt(12500);
  check("a tap seconds later (while it thinks) doesn't drop it", !vCancel);

  printf("-- what can't start a conversation (item 5 and others)\n");
  idle(); buddy.alarm();
  check("not while the alarm rings", !voiceRequest());
  idle(); fwProgress = 0.3f;
  check("not while a firmware update is installing (it restarts Dewy)", !voiceRequest());
  fwProgress = -1;
  check("otherwise yes", voiceRequest());

  printf("-- firmware updates wait for the conversation (item 5)\n");
  idle(); vState = V_THINK; vRequested = true;
  pollCloud(200000);
  check("no update allowed while it thinks", !updateAllowed);
  vState = V_IDLE; vRequested = false; pollCloud(200200);
  check("allowed again once the conversation is over", updateAllowed);

  printf("-- faces during a conversation (item 6)\n");
  idle(); vRequested = true;
  vState = V_LISTEN; poll(); check("listening face", buddy.mood() == face::LISTENING);
  vState = V_THINK; poll();  check("thinking face", buddy.mood() == face::THINKING);
  vState = V_SPEAK; poll();  check("talking face", buddy.mood() == face::TALKING);
  vState = V_IDLE; poll();   check("happy again at the end", buddy.mood() == face::HAPPY);

  idle(); vRequested = true; vState = V_LISTEN; poll();
  buddy.alarm(); poll();
  check("an alarm during a conversation drops it", vCancel);
  vState = V_THINK; poll();  check("the alarm stays on the face (not the thinking face)", buddy.mood() == face::ALARM);
  vState = V_IDLE; poll();   check("and still after the turn ends", buddy.mood() == face::ALARM);

  idle(); vRequested = true; vState = V_THINK; poll();
  buddy.setFaceDown(true); poll();
  check("laid face down mid-turn: the turn is dropped", vCancel);
  vState = V_IDLE; poll();
  check("and it stays asleep (quiet time)", buddy.mood() == face::SLEEPY && buddy.faceDown());
  buddy.setFaceDown(false);

  idle(); vRequested = true; vState = V_THINK; poll();
  buddy.setUpsideDown(true);
  vState = V_SPEAK; poll();  check("upside down mid-turn: it keeps complaining", buddy.mood() == face::ANNOYED);
  vState = V_IDLE; poll();   check("and after the turn", buddy.mood() == face::ANNOYED);
  buddy.setUpsideDown(false);

  printf("-- talking to the server (items 1, 4, 8)\n");
  std::string lastHeaders;
  std::string reply = wav(16000, 18400);
  for (int chunked = 0; chunked < 2; chunked++) {
    idle(); g_semTakes = g_semGives = 0;
    g_http = [&](const std::string&, const std::string&, const std::string&, const std::string& h) {
      lastHeaders = h;
      return MockReply{ 200, reply, { { "Content-Type", "audio/wav" }, { "x-dewy-heard", "what%20time%20is%20it" }, { "x-dewy-reply", "It%27s%20noon%21" } }, chunked == 1 };
    };
    bool ok = talkToCloud(16000);
    check(chunked ? "a chunked spoken answer arrives whole" : "a spoken answer with a length arrives whole", ok && playLen == reply.size());
    check("one HTTPS conversation: the lock was taken and given back", g_semTakes == 1 && g_semGives == 1);
  }
  check("the request sends the recording, the device token and its own User-Agent",
        g_lastPostBytes == 32000 && lastHeaders.find("Authorization: Bearer tok_") != std::string::npos && lastHeaders.find("User-Agent: Dewy-ESP32") != std::string::npos);
  g_spoken.clear(); playReply();
  check("it plays at the rate in the WAV header, ignoring the wrong lengths", g_spkRate == 18400 && g_spoken.size() >= 16000);
  vCancel = true; g_spoken.clear(); playReply();
  check("a dropped turn stops the speaker at once", g_spoken.size() < 2000);

  struct Case { const char* name; MockReply r; const char* expect; };
  Case cases[] = {
    { "the daily limit (429): her screen says so", { 429, "{\"error\":\"That's 40 questions today. Ask me again tomorrow!\"}" }, "40 questions" },
    { "words only (nobody spoke): shown on screen", { 200, "{\"heard\":\"\",\"reply\":\"Sorry, I didn't catch that. Could you say it again?\",\"audio\":false}", { { "Content-Type", "application/json; charset=utf-8" } } }, "didn't catch" },
    { "a server error without words: a friendly fallback", { 502, "" }, "server had a problem" },
    { "no connection at all", { -1, "" }, "can't reach" },
  };
  for (auto& c : cases) {
    idle(); g_semTakes = g_semGives = 0;
    MockReply r = c.r; g_http = [r](const std::string&, const std::string&, const std::string&, const std::string&) { return r; };
    bool ok = talkToCloud(16000);
    check(c.name, !ok && vNoticeNew && strstr(vNotice, c.expect) != nullptr);
    check("  and the lock is given back", g_semTakes == g_semGives);
    poll();
    check("  the card is shown once the turn is over", !vNoticeNew && (buddy.overlayActive() || buddy.hasPendingNote()));
  }
  idle(); g_semFail = true;
  check("the network task holds the line for 30 s: says it's busy", !talkToCloud(16000) && strstr(vNotice, "busy"));
  g_semFail = false;
  idle(); g_freeInternal = 40000; g_semTakes = g_semGives = 0;
  check("not enough RAM for HTTPS: says so, gives the lock back", !talkToCloud(16000) && strstr(vNotice, "memory") && g_semTakes == g_semGives);
  g_freeInternal = 120000;

  printf("-- recording (items 2 and 3)\n");
  // pretend microphone: background 10, and speech (peaks `level`, 5 frames loud, 2 quiet) from `from` to `to` seconds
  auto mic = [](float level, float from, float to) {
    static long t; t = 0;
    g_micSource = [level, from, to](int32_t* out, size_t n) {
      for (size_t i = 0; i < n; i++, t++) {
        float sec = t / 16000.0f; int frame = int(t / 256);
        bool speaking = sec >= from && sec < to && frame % 7 < 5;
        int v = speaking ? int(level * 1.41f * sinf(t * 0.3f)) : ((t & 1) ? 10 : -10);
        out[i] = v * (1 << 14);
      }
    };
  };
  idle(); noiseTrack.level = 10; noiseTrack.heardMs = 5000;
  mic(400, 0.0f, 1.5f);                               // "Hey Dewy, what's the weather" with no pause
  int n = recordUtterance(true);
  check("after Hey Dewy, speech straight away is recorded", n > 0);
  check("  with its start (1.5 s of speech, 0.3 s after)", n >= int(1.7 * 16000) && n <= int(1.85 * 16000));
  idle(); noiseTrack = NoiseTracker(); mic(400, 0.0f, 1.5f);
  check("just after start-up (no background level yet) it still works", recordUtterance(false) > 0);
  idle(); noiseTrack.level = 10; noiseTrack.heardMs = 5000; mic(200, 0.3f, 1.8f);
  check("quiet speech (peaks 200) is heard", recordUtterance(false) > 0);
  idle(); mic(0, 0, 0);
  check("silence: nothing recorded", recordUtterance(false) == 0);
  idle(); vCancel = true; mic(400, 0.0f, 1.5f);
  check("a dropped turn records nothing", recordUtterance(true) == 0);

  printf("\n%d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
