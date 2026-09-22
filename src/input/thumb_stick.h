/* The movement stick's own policy: a thumb's travel, as an axis. */
#ifndef X2_THUMB_STICK_H
#define X2_THUMB_STICK_H

#include <lucent/touch.h>

namespace x2::input {

/*
 * WHERE THE THUMB LANDED IS THE CENTRE, NOT WHERE THE RING IS DRAWN.
 *
 * A thumb does not arrive on the middle of a circle it cannot see; it
 * arrives somewhere inside it. Measured from the ring's geometric centre --
 * which is how this stick read for its whole life -- that offset IS the
 * player's input: the character walks off the instant the thumb touches
 * down, before it has moved at all, in whatever direction the thumb happened
 * to land. The travel is lopsided by the same amount, full deflection being
 * a short push towards the near edge and a long reach to the far one.
 *
 * So the origin is the contact's own landing point, exactly as the camera
 * swipe beside it already worked, and a ring radius of travel from there is
 * full deflection.
 */
class ThumbStick {
public:
  struct Deflection {
    float x = 0.0F;
    float y = 0.0F;
  };

  /* Travel below this fraction of full is the thumb resting, not steering.
     A stick with none of it walks the character on the tremor of a hand
     holding the phone. Beyond it the remaining travel is rescaled to reach
     full, so the dead zone costs range rather than adding a step. */
  static constexpr float kDeadZone = 0.08F;

  /* One ring radius of travel is full deflection. */
  void set_travel(float travel);

  /* This contact's deflection, retained for the overlay. An ended or
     canceled phase is neutral and forgets it. */
  Deflection track(const lucent::touch::Event &event);

  /* What the ring should draw: the live deflection, -1..1 per axis, never
     outside the unit circle. */
  Deflection deflection() const { return deflection_; }

  void release();

private:
  float travel_ = 0.0F;
  Deflection deflection_;
};

} // namespace x2::input

#endif
