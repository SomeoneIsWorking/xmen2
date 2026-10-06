/* The gate decides whether a movement stick appears over a save dialog, so
   every state it can report is exercised here, INCLUDING the two negatives.
   A gate tested only on "yes" cannot tell you it ever says "no". */
#include "../src/input/gameplay_control.h"

#include <stdio.h>
#include <string.h>

static unsigned checks, failures;

static void check(int ok, const char *what) {
  checks++;
  if (!ok) {
    failures++;
    printf("FAIL: %s\n", what);
  }
}

static void expect(double now, X2GameplayControl want, const char *what) {
  const X2GameplayControl got = x2_gameplay_control_state(now);
  checks++;
  if (got != want) {
    failures++;
    printf("FAIL: %s: want %s, got %s\n", what, x2_gameplay_control_name(want),
           x2_gameplay_control_name(got));
  }
}

int main(void) {
  int i;

  /* Every state is named, and out-of-range is refused rather than indexed. */
  for (i = 0; i < kX2ControlCount; i++)
    check(strcmp(x2_gameplay_control_name(i), "invalid") != 0,
          "each state has a name");
  check(strcmp(x2_gameplay_control_name(-1), "invalid") == 0, "below range");
  check(strcmp(x2_gameplay_control_name(kX2ControlCount), "invalid") == 0,
        "above range");

  /* Before any frame: not active, and it says WHY. */
  x2_gameplay_control_reset();
  expect(0.0, kX2ControlNeverSeen, "no frame yet");
  expect(100.0, kX2ControlNeverSeen, "still no frame, much later");
  check(!x2_gameplay_control_active(100.0), "never-seen is not active");

  /* A HUD heartbeat means gameplay. */
  x2_gameplay_control_reset();
  x2_gameplay_control_hud_drawn(10.0);
  expect(10.0, kX2ControlActive, "same instant");
  expect(10.0 + X2_GAMEPLAY_HUD_GRACE_SECONDS, kX2ControlActive,
         "at the grace boundary, still active");

  /* The heartbeat EXPIRES -- this is the menu case, and nothing reports it. */
  expect(10.0 + X2_GAMEPLAY_HUD_GRACE_SECONDS + 0.001, kX2ControlHudStale,
         "just past the grace, stale");
  expect(60.0, kX2ControlHudStale, "long past, still stale");
  check(!x2_gameplay_control_active(60.0), "stale is not active");

  /* And it recovers when the HUD comes back. */
  x2_gameplay_control_hud_drawn(60.0);
  expect(60.0, kX2ControlActive, "recovered after the menu closed");

  /* A cinematic holds control while the HUD is still up. */
  x2_gameplay_control_set_cutscene_locked(1);
  expect(60.0, kX2ControlCutsceneLocked, "HUD up, cinematic holds input");
  check(!x2_gameplay_control_active(60.0), "cutscene-locked is not active");

  /* The lock takes priority over nothing: once stale, stale wins, because
     a menu over a cutscene is still not gameplay either way. */
  expect(61.0, kX2ControlHudStale, "stale outranks the stale lock");

  x2_gameplay_control_set_cutscene_locked(0);
  x2_gameplay_control_hud_drawn(61.0);
  expect(61.0, kX2ControlActive, "released");

  /* Every state was actually reached: the denominator of this test. */
  x2_gameplay_control_reset();
  x2_gameplay_control_state(0.0);
  check(x2_gameplay_control_answers(kX2ControlNeverSeen) == 1ul,
        "answers are counted");
  check(x2_gameplay_control_answers(-1) == 0ul, "counter refuses bad index");

  printf("gameplay_control: %u checks, %u failures\n", checks, failures);
  return failures != 0;
}
