// The speech detector (vad.h) on made-up loudness sequences, one number per 16 ms frame. No hardware needed.
// "old" below is the detector as it was in voice.h before the fix (a copy of its logic), run on the same sequences, to
// show what the fix changed.
#include <cstdio>
#include <vector>
#include "../dewy/vad.h"

static int passed = 0, failed = 0;
static void check(const char* name, bool ok) { printf("  %s %s\n", ok ? "ok  " : "FAIL", name); (ok ? passed : failed)++; }

typedef std::vector<float> Seq;
static void add(Seq& s, int frames, float level) { for (int i = 0; i < frames; i++) s.push_back(level * (1.0f + 0.1f * ((i * 7) % 5 - 2) / 2.0f)); }   // a little wobble, +-10%
// words: syllables (hi for 5 frames) with short dips (lo for 2 frames)
static void talk(Seq& s, int frames, float hi, float lo) { for (int i = 0; i < frames; i++) s.push_back(i % 7 < 5 ? hi : lo); }

// ---- the detector as it was: first 10 frames dropped, next 12 averaged as "room noise", threshold = average * 3 + 150, at least 450
static bool oldDetect(const Seq& seq) {
  int frames = 0, loud = 0, quiet = 0; bool speech = false; float noiseSum = 0, thr = 600;
  for (float rms : seq) {
    frames++;
    if (frames <= 10) continue;
    if (frames <= 22) { noiseSum += rms; if (frames == 22) { thr = noiseSum / 12.0f * 3.0f + 150.0f; if (thr < 450) thr = 450; if (thr > 2500) thr = 2500; } continue; }
    if (rms > thr) { quiet = 0; if (!speech && ++loud >= 3) speech = true; } else { loud = 0; if (speech) quiet++; }
    if (speech && quiet >= 75) break;
  }
  return speech;
}
struct Outcome { bool speech; int lastLoud, doneAt, kept; float thr; };
static Outcome run(const Seq& seq, float hint, int skip) {
  Vad v; v.begin(hint, skip);
  Outcome o = { false, 0, -1, 0, 0 };
  for (size_t i = 0; i < seq.size(); i++) {
    Vad::Verdict r = v.feed(seq[i]);
    if (r.keep) o.kept++;
    if (r.loud && v.speech()) o.lastLoud = (int)i;
    if (r.done) { o.doneAt = (int)i; break; }
  }
  o.speech = v.speech(); o.thr = v.threshold();
  return o;
}

int main() {
  printf("-- she keeps talking right after \"Hey Dewy\" (the room is quiet: background 10)\n");
  { Seq s; talk(s, 90, 400, 80); add(s, 100, 15);
    check("old detector: never finds the speech (her own voice set the bar)", !oldDetect(s));
    Outcome o = run(s, 10, 2);
    check("new: finds it, using the background level from before", o.speech && o.thr == VAD_MIN_THR);
    check("new: nothing of the start is skipped (2 frames only)", o.kept == o.doneAt + 1 - 2);
    check("new: ends 1.2 s (75 frames) after the last loud frame", o.doneAt - o.lastLoud == VAD_QUIET_FRAMES);
  }
  printf("-- no background level yet (just started), and she is already talking\n");
  { Seq s; talk(s, 90, 400, 80); add(s, 100, 15);
    Outcome o = run(s, 0, 10);
    check("new: learns from the quietest frames, so it still finds the speech", o.speech && o.thr < 300);
  }
  printf("-- quiet speech (a metre away: peaks around 200, background 10)\n");
  { Seq s; add(s, 20, 10); talk(s, 80, 200, 70); add(s, 100, 12);
    check("old detector: threshold of at least 450 never hears it", !oldDetect(s));
    Outcome o = run(s, 10, 10);
    check("new: hears it (threshold 150)", o.speech);
  }
  printf("-- a fan, no speech (background 60)\n");
  { Seq s; add(s, 400, 60);
    check("new, background known: nothing heard", !run(s, 60, 10).speech);
    check("new, background learned: nothing heard", !run(s, 0, 10).speech);
  }
  printf("-- the click of a tap\n");
  { Seq s; add(s, 8, 3000); add(s, 60, 10);
    check("after a tap (10 frames skipped): the click is not speech", !run(s, 10, 10).speech);
    check("(with only 2 skipped it would be: that is why a tap skips more)", run(s, 10, 2).speech);
  }
  printf("-- thresholds stay in range\n");
  check("a loud room: threshold capped at 2500", Vad::thresholdFor(5000) == VAD_MAX_THR);
  check("a silent room: threshold floor is VAD_MIN_THR", Vad::thresholdFor(2) == VAD_MIN_THR);
  check("a normal room (background 100): 290", fabsf(Vad::thresholdFor(100) - 290) < 0.01f);

  printf("-- the background tracker\n");
  { NoiseTracker n;
    check("not ready before it has listened for 1.5 s", (n.feed(20, 10), !n.ready()));
    for (int i = 0; i < 200; i++) n.feed(20 * (1.0f + 0.05f * ((i % 3) - 1)), 10);
    check("ready, and close to the true level", n.ready() && fabsf(n.level - 20) < 3);
    for (int i = 0; i < 70; i++) n.feed(300, 10);               // 0.7 s of \"Hey Dewy\"
    check("0.7 s of speech barely moves it (below 80)", n.level < 80);
    for (int i = 0; i < 100; i++) n.feed(20, 10);
    check("and it falls back within a second", n.level < 25);
  }
  printf("\n%d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
