/* A physical stick's dead zone: a circle; past it the output restarts. */
#include "pad_stick_dead_zone.h"

#include <math.h>
#include <stdio.h>

static int failures;

static void check(const char *what, int ok) {
  if (!ok) {
    printf("FAIL: %s\n", what);
    failures++;
  }
}

static int near(float value, float expected) {
  return fabsf(value - expected) < 0.001f;
}

int main(void) {
  const float zone = X2_PAD_LEFT_STICK_DEAD_ZONE;

  /* Drift inside the circle is centred, in every direction alike. */
  {
    const X2PadStick drift = x2_pad_stick_dead_zone(0.2f, 0.0f, zone);
    const X2PadStick diagonal = x2_pad_stick_dead_zone(0.16f, 0.16f, zone);
    check("drift along an axis is centred", drift.x == 0.0f && drift.y == 0.0f);
    check("drift on a diagonal is centred too",
          diagonal.x == 0.0f && diagonal.y == 0.0f);
  }

  /* A full diagonal keeps BOTH components. Retail's per-axis 0.75 dropped
     each 0.71 and did not move at all; this is the case the owner exists
     for. */
  {
    const float c = 0.70710678f;
    const X2PadStick corner = x2_pad_stick_dead_zone(c, -c, zone);
    check("a full diagonal reaches full length",
          near(hypotf(corner.x, corner.y), 1.0f));
    check("in its own direction", near(corner.x, -corner.y) && corner.x > 0.7f);
  }

  /* Past the circle the output starts from zero, not from the zone's size,
     and full deflection is still full. */
  {
    const X2PadStick edge = x2_pad_stick_dead_zone(zone + 0.01f, 0.0f, zone);
    const X2PadStick full = x2_pad_stick_dead_zone(0.0f, 1.0f, zone);
    const X2PadStick half = x2_pad_stick_dead_zone(0.0f, 0.5f, zone);
    check("just outside the zone is barely moving",
          edge.x > 0.0f && edge.x < 0.02f);
    check("full deflection is full", near(full.y, 1.0f));
    check("half-way is proportional",
          near(half.y, (0.5f - zone) / (1.0f - zone)));
  }

  /* A pad whose square gate reports past the circle is clamped to it. */
  {
    const X2PadStick corner = x2_pad_stick_dead_zone(1.0f, 1.0f, zone);
    check("a square-gate corner is clamped to the unit circle",
          near(hypotf(corner.x, corner.y), 1.0f));
  }

  /* A zone that leaves nothing, or input that is not a number, is centred
     rather than divided by zero. */
  {
    const X2PadStick all = x2_pad_stick_dead_zone(1.0f, 0.0f, 1.0f);
    const X2PadStick nan_in = x2_pad_stick_dead_zone(NAN, 0.0f, zone);
    check("a zone of 1 is always centred", all.x == 0.0f && all.y == 0.0f);
    check("NaN input is centred", nan_in.x == 0.0f && nan_in.y == 0.0f);
  }

  if (failures == 0) {
    printf("pad stick dead zone: all checks passed\n");
  }
  return failures == 0 ? 0 : 1;
}
