// Voice activity detection for Dewy's microphone: how loud is the room, when does a question start, when is it over.
// There is no hardware in here, so it is tested on a PC (firmware/test/vad_test.cpp).
// Levels are RMS on the 16-bit scale. A frame is 16 ms (256 samples at 16 kHz).
#pragma once
#include <math.h>
#include <stdint.h>

#ifndef VAD_MIN_THR
#define VAD_MIN_THR 150   // the quietest speech it will listen for. Measured on the real mic at MIC_SHIFT 14: room 1200-3000,
                          // speech 4000-20000, so at MIC_SHIFT 16 about a quarter. Check it with the "mic" serial command.
#endif
#define VAD_MAX_THR 2500
#define VAD_LEARN_FRAMES 12    // with no background level to start from: learn it from the first 0.2 s
#define VAD_QUIET_FRAMES 75    // 1.2 s of quiet after speech = the question is over

// Follows the background noise while Dewy is idle. It drops quickly when the room gets quieter and rises slowly (about 6 s)
// when it gets louder, so a few seconds of talking, like "Hey Dewy" itself, barely move it.
struct NoiseTracker {
  float level = 0, heardMs = 0;
  void feed(float rms, float ms) {
    if (heardMs == 0) level = rms;
    else {
      float k = ms / (rms < level ? 200.0f : 6000.0f);
      level += (rms - level) * (k > 1 ? 1 : k);
    }
    heardMs += ms;
  }
  bool ready() const { return heardMs >= 1500; }   // has listened for at least 1.5 s
};

class Vad {
public:
  struct Verdict {
    bool keep;   // store this frame in the recording
    bool loud;   // this frame is louder than the threshold
    bool done;   // speech started and has now been quiet for long enough
  };

  // noiseHint: the background level from NoiseTracker, or 0 to learn it from the first frames.
  // skipFrames: frames to ignore at the start (the click of a tap, or old audio still in the buffer).
  void begin(float noiseHint, int skipFrames) {
    skip_ = skipFrames; frame_ = 0; nLearn_ = 0; loud_ = 0; quiet_ = 0; speech_ = false;
    haveThr_ = noiseHint > 0;
    thr_ = haveThr_ ? thresholdFor(noiseHint) : (float)VAD_MIN_THR;
  }

  Verdict feed(float rms) {
    Verdict v = { false, false, false };
    if (++frame_ <= skip_) return v;
    v.keep = true;
    if (!haveThr_) {
      // The level of the quietest few frames, not the average: if she is already talking, her voice must not set the bar.
      learn_[nLearn_++] = rms;
      if (nLearn_ == VAD_LEARN_FRAMES) {
        for (int i = 1; i < VAD_LEARN_FRAMES; i++)        // sort, smallest first
          for (int j = i; j > 0 && learn_[j] < learn_[j - 1]; j--) { float t = learn_[j]; learn_[j] = learn_[j - 1]; learn_[j - 1] = t; }
        float sum = 0;
        for (int i = 0; i < VAD_LEARN_FRAMES / 4; i++) sum += learn_[i];
        thr_ = thresholdFor(sum / (VAD_LEARN_FRAMES / 4));
        haveThr_ = true;
      }
      return v;
    }
    if (rms > thr_) {
      quiet_ = 0; v.loud = true;
      if (!speech_ && ++loud_ >= 3) speech_ = true;       // three loud frames in a row: she is talking
    } else {
      loud_ = 0;
      if (speech_) quiet_++;
    }
    v.done = speech_ && quiet_ >= VAD_QUIET_FRAMES;
    return v;
  }

  bool speech() const { return speech_; }
  float threshold() const { return thr_; }

  static float thresholdFor(float noise) {
    float t = noise * 2.5f + 40.0f;
    return t < VAD_MIN_THR ? (float)VAD_MIN_THR : (t > VAD_MAX_THR ? (float)VAD_MAX_THR : t);
  }

private:
  int skip_ = 0, frame_ = 0, nLearn_ = 0, loud_ = 0, quiet_ = 0;
  bool speech_ = false, haveThr_ = false;
  float thr_ = VAD_MIN_THR, learn_[VAD_LEARN_FRAMES];
};
