#include "cutscene_control_clock.h"

#include "guest_memory.h"
#include "x86rt_native.h"

#include <string.h>

/* Offsets into the guest clock object. */
enum { CLOCK_NOW = 0x3e8u, CLOCK_CONTROL_DEADLINE = 0x3f4u };

static const char *const kStateNames[kX2CutsceneClockCount] = {
    "unreadable", "locked", "released"};

const char *cutscene_control_clock_name(int state) {
  if (state < 0 || state >= kX2CutsceneClockCount)
    return "invalid";
  return kStateNames[state];
}

float cutscene_control_clock_seconds(uint32_t bits) {
  float value;

  memcpy(&value, &bits, sizeof value);
  return value;
}

static int read_float_bits(uint32_t address, uint32_t *bits) {
  return address && x86_peek32(address, bits);
}

int cutscene_control_clock_now_bits(uint32_t clock, uint32_t *bits) {
  if (!bits)
    return 0;
  return read_float_bits(clock ? clock + CLOCK_NOW : 0u, bits);
}

X2CutsceneClockState cutscene_control_clock_state(uint32_t clock) {
  uint32_t now_bits, deadline_bits;
  float now, deadline;

  if (!clock || !read_float_bits(clock + CLOCK_NOW, &now_bits) ||
      !read_float_bits(clock + CLOCK_CONTROL_DEADLINE, &deadline_bits))
    return kX2CutsceneClockUnreadable;
  now = cutscene_control_clock_seconds(now_bits);
  deadline = cutscene_control_clock_seconds(deadline_bits);
  /* Authored post-release work stays scheduled for ordinary gameplay: a
     negative deadline means nothing is scheduled, not "long ago". */
  if (deadline < 0.0f || deadline > now)
    return kX2CutsceneClockLocked;
  return kX2CutsceneClockReleased;
}

int cutscene_control_clock_release_now(uint32_t clock) {
  uint32_t now_bits;

  if (!cutscene_control_clock_now_bits(clock, &now_bits))
    return 0;
  WR32(clock + CLOCK_CONTROL_DEADLINE, now_bits);
  return 1;
}
