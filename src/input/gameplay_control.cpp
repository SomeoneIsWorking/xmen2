#include "gameplay_control.h"

#include <stddef.h>

static const char *const kStateNames[kX2ControlCount] = {
    "never-seen", "hud-stale", "cutscene-locked", "active"};

static struct {
  double last_hud;
  int seen;
  int cutscene_locked;
  unsigned long answers[kX2ControlCount];
} g_gate;

const char *x2_gameplay_control_name(int state) {
  if (state < 0 || state >= kX2ControlCount)
    return "invalid";
  return kStateNames[state];
}

void x2_gameplay_control_hud_drawn(double now) {
  g_gate.last_hud = now;
  g_gate.seen = 1;
}

void x2_gameplay_control_set_cutscene_locked(int locked) {
  g_gate.cutscene_locked = locked != 0;
}

X2GameplayControl x2_gameplay_control_state(double now) {
  X2GameplayControl state;

  if (!g_gate.seen) {
    state = kX2ControlNeverSeen;
  } else if (now - g_gate.last_hud > X2_GAMEPLAY_HUD_GRACE_SECONDS) {
    state = kX2ControlHudStale;
  } else if (g_gate.cutscene_locked) {
    state = kX2ControlCutsceneLocked;
  } else {
    state = kX2ControlActive;
  }
  g_gate.answers[state]++;
  return state;
}

int x2_gameplay_control_active(double now) {
  return x2_gameplay_control_state(now) == kX2ControlActive;
}

void x2_gameplay_control_reset(void) {
  size_t i;
  g_gate.last_hud = 0.0;
  g_gate.seen = 0;
  g_gate.cutscene_locked = 0;
  for (i = 0; i < kX2ControlCount; i++)
    g_gate.answers[i] = 0ul;
}

unsigned long x2_gameplay_control_answers(int state) {
  if (state < 0 || state >= kX2ControlCount)
    return 0ul;
  return g_gate.answers[state];
}
