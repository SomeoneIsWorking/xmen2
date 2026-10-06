#include "controller_hotplug.h"

void x2_controller_hotplug_invalidate(X2ControllerHotplug *state) {
  /* The next pump must admit the live inventory again even though no SDL
     generation changed: a save payload deserializes the input manager's
     own controller table over the live one, so the table can name devices
     that no longer exist while the connected pad sits unadmitted. */
  if (state)
    state->initialized = 0;
}

int x2_controller_hotplug_needs_admission(X2ControllerHotplug *state,
                                          uint64_t generation) {
  if (!state)
    return 0;
  if (state->initialized && state->processed_generation == generation)
    return 0;
  state->initialized = 1;
  state->processed_generation = generation;
  return 1;
}

void x2_controller_hotplug_enumerated(X2ControllerHotplug *state,
                                      uint64_t generation, int connected,
                                      int reported) {
  if (!state)
    return;
  state->initialized = 1;
  state->processed_generation = generation;
  state->connected = connected;
  state->last_reported = reported;
}

void x2_controller_hotplug_admitted(X2ControllerHotplug *state) {
  if (state)
    state->admissions++;
}
