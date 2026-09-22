/* The movement stick's policy: travel from where the thumb landed. */
#include "../src/input/thumb_stick.h"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void check(const char *what, bool ok) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    failures++;
  }
}

void close_to(const char *what, float value, float expected) {
  check(what, std::fabs(value - expected) < 0.01F);
  if (std::fabs(value - expected) >= 0.01F) {
    std::printf("      %g, expected %g\n", static_cast<double>(value),
                static_cast<double>(expected));
  }
}

lucent::touch::Event
at(float origin_x, float origin_y, float x, float y,
   lucent::touch::Phase phase = lucent::touch::Phase::moved) {
  lucent::touch::Event event;
  event.origin = {origin_x, origin_y};
  event.position = {x, y};
  event.phase = phase;
  return event;
}

} // namespace

int main() {
  using x2::input::ThumbStick;

  /* A thumb that lands away from the ring's centre and has not moved is
     neutral. This is the defect the whole class exists for: measured from
     the ring, that landing offset walked the character on touch-down. */
  {
    ThumbStick stick;
    stick.set_travel(100.0F);
    const auto landed = stick.track(
        at(430.0F, 380.0F, 430.0F, 380.0F, lucent::touch::Phase::began));
    close_to("a thumb landing off-centre steers nothing (x)", landed.x, 0.0F);
    close_to("a thumb landing off-centre steers nothing (y)", landed.y, 0.0F);
  }

  /* The same push produces the same deflection wherever the thumb landed:
     travel is measured from the contact, so the ring is not a map of where
     the player must put their hand. */
  {
    ThumbStick centred;
    ThumbStick offset;
    centred.set_travel(100.0F);
    offset.set_travel(100.0F);
    const auto a = centred.track(at(500.0F, 500.0F, 500.0F, 450.0F));
    const auto b = offset.track(at(430.0F, 380.0F, 430.0F, 330.0F));
    close_to("an identical push gives an identical axis", a.y, b.y);
    check("and it is upward", a.y < -0.4F);
  }

  /* Full travel is one radius; beyond it the stick stays at full. */
  {
    ThumbStick stick;
    stick.set_travel(100.0F);
    close_to("a full radius of travel is full deflection",
             stick.track(at(0.0F, 0.0F, 100.0F, 0.0F)).x, 1.0F);
    close_to("and further travel stays at full",
             stick.track(at(0.0F, 0.0F, 400.0F, 0.0F)).x, 1.0F);
  }

  /* Clamped to the circle, not to each axis: a square clamp would let a
     diagonal reach 1.0 in both axes and outrun every other direction. */
  {
    ThumbStick stick;
    stick.set_travel(100.0F);
    const auto corner = stick.track(at(0.0F, 0.0F, 400.0F, 400.0F));
    close_to("a diagonal cannot exceed the unit circle",
             std::hypot(corner.x, corner.y), 1.0F);
    check("and it is evenly split",
          std::fabs(corner.x - corner.y) < 0.01F && corner.x > 0.6F);
  }

  /* A hand's tremor is not steering. */
  {
    ThumbStick stick;
    stick.set_travel(100.0F);
    const auto tremor = stick.track(at(0.0F, 0.0F, 4.0F, 3.0F));
    close_to("travel inside the dead zone steers nothing",
             std::hypot(tremor.x, tremor.y), 0.0F);
    /* Just outside it the stick starts from zero rather than stepping to
       the dead zone's own size. */
    const auto just_outside = stick.track(at(0.0F, 0.0F, 9.0F, 0.0F));
    check("and just beyond it the stick starts from nothing",
          just_outside.x > 0.0F && just_outside.x < 0.05F);
  }

  /* Lifting the thumb is neutral, and the ring must not go on drawing a
     deflection nothing is holding. */
  {
    ThumbStick stick;
    stick.set_travel(100.0F);
    stick.track(at(0.0F, 0.0F, 100.0F, 0.0F));
    const auto ended =
        stick.track(at(0.0F, 0.0F, 100.0F, 0.0F, lucent::touch::Phase::ended));
    close_to("lifting the thumb is neutral", ended.x, 0.0F);
    close_to("and the ring stops drawing it", stick.deflection().x, 0.0F);
  }

  /* A ring with no size cannot divide by it. */
  {
    ThumbStick stick;
    stick.set_travel(0.0F);
    close_to("a stick with no travel is neutral",
             stick.track(at(0.0F, 0.0F, 50.0F, 0.0F)).x, 0.0F);
  }

  if (failures == 0) {
    std::printf("thumb stick: all checks passed\n");
  }
  return failures == 0 ? 0 : 1;
}
