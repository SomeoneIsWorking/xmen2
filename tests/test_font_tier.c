/* The step between the game's two font tiers, as arithmetic. Both refusals
   are exercised: a step that cannot be measured must come back as a refusal,
   because the caller substituting 1.0 for it is AUTO silently holding
   nothing at every resolution. */
#include "../src/native/font_tier.h"

#include <stdio.h>

static unsigned checks, failures;

static void check(int ok, const char *what) {
  checks++;
  if (!ok) {
    failures++;
    printf("FAIL: %s\n", what);
  }
}

static void fill(int16_t *out, unsigned count, int16_t value) {
  unsigned i;
  for (i = 0; i < count; i++)
    out[i] = value;
}

int main(void) {
  int16_t pc[X2_FONT_TIER_SAMPLES], hd[X2_FONT_TIER_SAMPLES];
  unsigned samples;
  float ratios[4];
  float step;

  /* A mean over these is 3.175; the median is 1.35. If the measurement ever
     goes back to a mean, this case is the one that catches it. */
  ratios[0] = 1.0f;
  ratios[1] = 1.3f;
  ratios[2] = 1.4f;
  ratios[3] = 9.0f;
  check(x2_font_tier_median(ratios, 4) == (1.3f + 1.4f) / 2.0f,
        "an even-length median averages the middle pair");
  ratios[0] = 2.0f;
  ratios[1] = 1.0f;
  ratios[2] = 1.5f;
  check(x2_font_tier_median(ratios, 3) == 1.5f, "an odd-length median is the middle");
  check(x2_font_tier_median(NULL, 4) == 0.0f, "no ratios is not a median");
  check(x2_font_tier_median(ratios, 0) == 0.0f, "zero ratios is not a median");

  /* The real shape: 'A' is 14x13 in x2f_med_pc and 19x18 in x2f_med_hd. */
  fill(pc, X2_FONT_TIER_SAMPLES, 11);
  fill(hd, X2_FONT_TIER_SAMPLES, 15);
  samples = 0;
  step = x2_font_tier_ratio(pc, hd, X2_FONT_TIER_SAMPLES, &samples);
  check(step > 1.363f && step < 1.364f, "a whole sample measures 15/11");
  check(samples == X2_FONT_TIER_SAMPLES, "every drawn glyph is counted");

  /* One outlier glyph must not move the answer. */
  hd[7] = 90;
  step = x2_font_tier_ratio(pc, hd, X2_FONT_TIER_SAMPLES, &samples);
  check(step > 1.363f && step < 1.364f, "one outlier glyph does not move the median");

  /* Glyphs a tier does not draw are skipped on both sides. */
  fill(pc, X2_FONT_TIER_SAMPLES, 11);
  fill(hd, X2_FONT_TIER_SAMPLES, 15);
  pc[0] = 0;
  hd[1] = 0;
  step = x2_font_tier_ratio(pc, hd, X2_FONT_TIER_SAMPLES, &samples);
  check(step > 1.363f && step < 1.364f, "undrawn glyphs are skipped, not counted as 0");
  check(samples == X2_FONT_TIER_SAMPLES - 2u, "skipped glyphs leave the count");

  /* Too few glyphs in both tiers: refused, not averaged over what is left. */
  fill(pc, X2_FONT_TIER_SAMPLES, 0);
  fill(hd, X2_FONT_TIER_SAMPLES, 15);
  pc[0] = 11;
  pc[1] = 11;
  check(x2_font_tier_ratio(pc, hd, X2_FONT_TIER_SAMPLES, &samples) == 0.0f,
        "two of fifty-two drawn glyphs is refused");
  check(samples == 2u, "a refusal still reports what it saw");

  /* An implausible step is refused rather than emitted. */
  fill(pc, X2_FONT_TIER_SAMPLES, 10);
  fill(hd, X2_FONT_TIER_SAMPLES, 90);
  check(x2_font_tier_ratio(pc, hd, X2_FONT_TIER_SAMPLES, &samples) == 0.0f,
        "a 9x step is not a font tier step");
  fill(hd, X2_FONT_TIER_SAMPLES, 5);
  check(x2_font_tier_ratio(pc, hd, X2_FONT_TIER_SAMPLES, &samples) == 0.0f,
        "an HD tier smaller than the PC tier is refused");
  fill(hd, X2_FONT_TIER_SAMPLES, 10);
  check(x2_font_tier_ratio(pc, hd, X2_FONT_TIER_SAMPLES, &samples) == 1.0f,
        "an equal pair is a step of exactly 1");

  check(x2_font_tier_ratio(NULL, hd, X2_FONT_TIER_SAMPLES, &samples) == 0.0f,
        "no PC tier is refused");
  check(x2_font_tier_ratio(pc, NULL, X2_FONT_TIER_SAMPLES, &samples) == 0.0f,
        "no HD tier is refused");
  check(x2_font_tier_ratio(pc, hd, X2_FONT_TIER_SAMPLES + 1u, &samples) == 0.0f,
        "more glyphs than the sample holds is refused");
  check(x2_font_tier_ratio(pc, hd, X2_FONT_TIER_SAMPLES, NULL) == 1.0f,
        "the sample count is optional");

  printf("%u check(s), %u failure(s)\n", checks, failures);
  return failures ? 1 : 0;
}
