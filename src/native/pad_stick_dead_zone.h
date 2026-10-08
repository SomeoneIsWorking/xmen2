#pragma once

/*
 * One physical thumbstick's dead zone, over the stick as a VECTOR.
 *
 * The retail game had no stick dead zone of its own: its gameplay resolver
 * (0x0061a4c0, see stick_axis_override.cpp) dropped every axis below 0.75,
 * which hid a worn stick's drift and also every direction but the eight a key
 * pad can make. With that threshold gone, drift needs an owner, and it is this:
 * a circle, so every direction has the same threshold and a diagonal is not cut
 * off on one axis first. Past the circle the output restarts from zero and
 * still reaches 1 at full deflection, so a gentle push is a gentle push
 * rather than a step to the dead zone's own size.
 *
 * The radii are XInput's documented recommendations (XINPUT_GAMEPAD_LEFT/
 * RIGHT_THUMB_DEADZONE, 7849 and 8689 of 32767), the ones a 360 pad -- the
 * controller this game's mapping was written for -- was specified against.
 */

#include <cmath>

namespace x2::native {

inline constexpr float kPadLeftStickDeadZone = 7849.0f / 32767.0f;
inline constexpr float kPadRightStickDeadZone = 8689.0f / 32767.0f;

struct PadStick {
  float x;
  float y;
};

/* `x`/`y` in [-1, 1]; the result has the same direction, its length rescaled
   from (dead_zone, 1] to (0, 1] and clamped to the unit circle. */
inline PadStick pad_stick_dead_zone(float x, float y, float dead_zone) {
  const PadStick centred = {0.0f, 0.0f};
  const float length = std::hypot(x, y);
  if (!(length > dead_zone) || !(dead_zone < 1.0f)) {
    return centred;
  }
  const float reach = std::fmin(length, 1.0f);
  const float scale = (reach - dead_zone) / (1.0f - dead_zone) / length;
  const PadStick result = {x * scale, y * scale};
  return result;
}

} // namespace x2::native
