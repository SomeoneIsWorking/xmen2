#ifndef X2_PAD_STICK_DEAD_ZONE_H
#define X2_PAD_STICK_DEAD_ZONE_H

/*
 * One physical thumbstick's dead zone, over the stick as a VECTOR.
 *
 * The retail game had no stick dead zone of its own: its gameplay resolver
 * (0x0061a4c0, see stick_axis_override.c) dropped every axis below 0.75, which
 * hid a worn stick's drift and also every direction but the eight a key pad
 * can make. With that threshold gone, drift needs an owner, and it is this: a
 * circle, so every direction has the same threshold and a diagonal is not cut
 * off on one axis first. Past the circle the output restarts from zero and
 * still reaches 1 at full deflection, so a gentle push is a gentle push
 * rather than a step to the dead zone's own size.
 *
 * The radii are XInput's documented recommendations (XINPUT_GAMEPAD_LEFT/
 * RIGHT_THUMB_DEADZONE, 7849 and 8689 of 32767), the ones a 360 pad -- the
 * controller this game's mapping was written for -- was specified against.
 */

#include <math.h>

#define X2_PAD_LEFT_STICK_DEAD_ZONE (7849.0f / 32767.0f)
#define X2_PAD_RIGHT_STICK_DEAD_ZONE (8689.0f / 32767.0f)

typedef struct X2PadStick {
  float x;
  float y;
} X2PadStick;

/* `x`/`y` in [-1, 1]; the result has the same direction, its length rescaled
   from (dead_zone, 1] to (0, 1] and clamped to the unit circle. */
static inline X2PadStick x2_pad_stick_dead_zone(float x, float y,
                                                float dead_zone) {
  const X2PadStick centred = {0.0f, 0.0f};
  const float length = hypotf(x, y);
  if (!(length > dead_zone) || !(dead_zone < 1.0f)) {
    return centred;
  }
  const float reach = fminf(length, 1.0f);
  const float scale = (reach - dead_zone) / (1.0f - dead_zone) / length;
  const X2PadStick result = {x * scale, y * scale};
  return result;
}

#endif
