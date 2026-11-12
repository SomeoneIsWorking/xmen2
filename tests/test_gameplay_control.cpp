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

static void expect(unsigned long frame, X2GameplayControl want,
                   const char *what) {
  const X2GameplayControl got = x2_gameplay_control_state(frame);
  checks++;
  if (got != want) {
    failures++;
    printf("FAIL: %s: want %s, got %s\n", what, x2_gameplay_control_name(want),
           x2_gameplay_control_name(got));
  }
}

int main(void) {
  int i;

  for (i = 0; i < kX2ControlCount; i++)
    check(strcmp(x2_gameplay_control_name(i), "invalid") != 0,
          "each state has a name");
  check(strcmp(x2_gameplay_control_name(-1), "invalid") == 0, "below range");
  check(strcmp(x2_gameplay_control_name(kX2ControlCount), "invalid") == 0,
        "above range");

  x2_gameplay_control_reset();
  expect(0ul, kX2ControlNeverSeen, "no frame yet");
  expect(100ul, kX2ControlNeverSeen, "still no frame, much later");
  check(!x2_gameplay_control_active(100ul), "never-seen is not active");

  /* Drawn during frame 10: active through frame 11, which has not drawn yet. */
  x2_gameplay_control_reset();
  x2_gameplay_control_hud_drawn(10ul);
  expect(10ul, kX2ControlActive, "the frame that drew it");
  expect(11ul, kX2ControlActive, "the next frame, before its HUD draw");

  /* Frame 11 was presented without the HUD: a menu. */
  expect(12ul, kX2ControlHudStale, "a whole frame without the HUD");
  expect(5000ul, kX2ControlHudStale, "long past, still stale");
  check(!x2_gameplay_control_active(5000ul), "stale is not active");

  /* However long frame 11 took, it is still the next frame (issue 0210). */
  x2_gameplay_control_hud_drawn(5000ul);
  expect(5001ul, kX2ControlActive, "a slow frame is not a menu");

  x2_gameplay_control_set_cutscene_locked(1);
  expect(5001ul, kX2ControlCutsceneLocked, "HUD up, cinematic holds input");
  check(!x2_gameplay_control_active(5001ul), "cutscene-locked is not active");
  expect(5002ul, kX2ControlHudStale, "stale outranks the lock");

  x2_gameplay_control_set_cutscene_locked(0);
  x2_gameplay_control_hud_drawn(5002ul);
  expect(5002ul, kX2ControlActive, "released");

  x2_gameplay_control_reset();
  x2_gameplay_control_state(0ul);
  check(x2_gameplay_control_answers(kX2ControlNeverSeen) == 1ul,
        "answers are counted");
  check(x2_gameplay_control_answers(-1) == 0ul, "counter refuses bad index");

  printf("gameplay_control: %u checks, %u failures\n", checks, failures);
  return failures != 0;
}
