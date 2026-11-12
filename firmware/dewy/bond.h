#pragma once
// Dewy's bond with its person: five levels that it grows through, and a slow fade after days apart.
// Pure maths, no Arduino calls, so the PC tests can check it. dewy.ino decides what Dewy does with it.
//
//   level          bond (0..1)   what changes
//   stranger       0.00          shy: just blinks at you
//   friend         0.10          says hello when you come back
//   close friend   0.35          little hearts float up now and then
//   best friend    0.65          hearts more often, more patient with pokes
//   soulmate       0.90          the biggest welcomes, hearts often

#include <math.h>

namespace bond {

enum Level { STRANGER, FRIEND, CLOSE, BEST, SOULMATE, NUM_LEVELS };
static const char* const NAMES[NUM_LEVELS] = { "stranger", "friend", "close friend", "best friend", "soulmate" };
static const float START[NUM_LEVELS] = { 0.00f, 0.10f, 0.35f, 0.65f, 0.90f };

inline Level levelOf(float a) {
  int l = STRANGER;
  for (int i = 1; i < NUM_LEVELS; i++) if (a >= START[i]) l = i;
  return Level(l);
}

// How far through its level the bond is, 0..1 (soulmate fills up at 1.0).
inline float progressIn(float a) {
  Level l = levelOf(a);
  float lo = START[l], hi = l + 1 < NUM_LEVELS ? START[l + 1] : 1.0f;
  float p = (a - lo) / (hi - lo);
  return p < 0 ? 0 : (p > 1 ? 1 : p);
}

// What the bond points become after daysApart without any pets, pokes or pick-ups.
// Nothing is lost for the first graceDays. After that it loses fadePerDay of full points a day,
// but never below half of what it had when you left, so a long trip can't wipe it out.
inline float faded(float points, float full, double daysApart, double graceDays, float fadePerDay) {
  if (points <= 0 || daysApart <= graceDays) return points < 0 ? 0 : points;
  float lost = float((daysApart - graceDays) * fadePerDay * full);
  float floorPts = points * 0.5f;
  float left = points - lost;
  return left < floorPts ? floorPts : left;
}

// How warmly to greet you, 0..8 hearts, from the level and how long you were away (hours).
inline int welcomeHearts(Level l, double hoursAway) {
  if (l == STRANGER) return 0;
  int h = int(l) + 1;                         // friend 2 ... soulmate 5
  if (hoursAway >= 24) h += 2;                // extra happy after a day or more
  if (hoursAway >= 72) h += 1;
  return h > 8 ? 8 : h;
}

// The words for a welcome. Short enough for one line on the 320 px screen.
inline const char* welcomeText(Level l, double hoursAway) {
  if (l == STRANGER) return "";
  if (hoursAway >= 24) return l >= BEST ? "I missed you so much!" : "I missed you!";
  switch (l) {
    case FRIEND: return "Oh, hi!";
    case CLOSE: return "Hey, you're back!";
    case BEST: return "Yay, it's you!";
    default: return "My favourite person!";
  }
}

inline const char* levelUpText(Level l) {
  switch (l) {
    case FRIEND: return "We're friends now!";
    case CLOSE: return "We're close friends!";
    case BEST: return "Best friends forever!";
    case SOULMATE: return "You're my soulmate!";
    default: return "";
  }
}

}  // namespace bond
