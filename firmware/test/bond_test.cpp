// Bond levels, fading after days apart, and welcomes (PC only).
#include "../dewy/bond.h"
#include <cstdio>
#include <cstring>
#include <cmath>
static int pass = 0, fail = 0;
static void check(bool ok, const char* what) { if (ok) pass++; else { fail++; printf("FAIL %s\n", what); } }
int main() {
  using namespace bond;
  check(levelOf(0) == STRANGER, "0 is a stranger");
  check(levelOf(0.0999f) == STRANGER, "just under friend");
  check(levelOf(0.10f) == FRIEND, "0.10 is a friend");
  check(levelOf(0.35f) == CLOSE, "0.35 is a close friend");
  check(levelOf(0.65f) == BEST, "0.65 is a best friend");
  check(levelOf(0.90f) == SOULMATE && levelOf(1.0f) == SOULMATE, "0.9 and up is a soulmate");
  check(fabsf(progressIn(0.225f) - 0.5f) < 0.01f, "halfway through friend");
  check(progressIn(1.0f) == 1.0f && progressIn(0) == 0, "progress ends");
  // fading: 300 full, 2 days grace, 3% a day
  check(faded(150, 300, 1.9, 2, 0.03f) == 150, "nothing lost in the grace days");
  check(fabsf(faded(150, 300, 3, 2, 0.03f) - 141) < 0.01f, "one day past grace loses 9 points");
  check(faded(150, 300, 400, 2, 0.03f) == 75, "never below half");
  check(faded(0, 300, 10, 2, 0.03f) == 0, "nothing to lose");
  check(faded(-5, 300, 0, 2, 0.03f) == 0, "negative points clamp to 0");
  // welcomes
  check(welcomeHearts(STRANGER, 100) == 0 && welcomeText(STRANGER, 100)[0] == 0, "a stranger doesn't greet");
  check(welcomeHearts(FRIEND, 1) == 2 && welcomeHearts(SOULMATE, 1) == 5, "more hearts at higher levels");
  check(welcomeHearts(BEST, 30) == 6 && welcomeHearts(SOULMATE, 80) == 8, "extra hearts after a day or more, capped at 8");
  check(!strcmp(welcomeText(CLOSE, 30), "I missed you!") && !strcmp(welcomeText(BEST, 30), "I missed you so much!"), "missed-you words");
  for (int l = FRIEND; l < NUM_LEVELS; l++) {
    check(strlen(welcomeText(Level(l), 1)) <= 24 && strlen(welcomeText(Level(l), 30)) <= 24 && strlen(levelUpText(Level(l))) <= 24, "texts fit on one line");
    check(levelUpText(Level(l))[0] != 0, "every level has a level-up message");
  }
  printf("%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
