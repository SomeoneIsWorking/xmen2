#include "touch_layout.h"

#include <math.h>

namespace x2::presentation {

/*
 * The proportions, named once.
 *
 * The layout this replaces carried eighteen bare fractions -- 0.77, 0.61,
 * 0.98 -- one per rectangle edge, so moving a button meant editing four
 * numbers that had no stated relationship to each other or to anything else.
 * Here a control's SIZE is one number and its PLACE is derived from the
 * cluster it belongs to, which is what makes the arrangement adjustable
 * without re-deriving it.
 *
 * Sizes are fractions of the SHORT edge, because a thumb is the same size on
 * a tall phone and a wide tablet; positions are insets from the safe edges for
 * the same reason.
 */
namespace {
const float kStickDiameter = 0.38F; /* of the short edge */
const float kEdgeInset = 0.06F;
/* The action cluster, in fingers (a tenth of the short edge, the touch-target
   floor). Attack is the largest, in the corner; every other action is a
   finger and a tenth. */
const float kFinger = 0.1F;
const float kPrimaryFingers = 1.4F;
const float kSecondaryFingers = 1.1F;
const float kClusterGapFingers = 0.15F;
/* Attack's centre from the safe corner, on both axes. */
const float kCornerFingers = 1.25F;
/* Degrees around Attack, counter-clockwise from its right: 180 is straight
   left along the bottom, 90 straight up the right edge. The hit zones are
   squares, so a diagonal neighbour has to clear Attack's square on one axis;
   the ring radii are derived from that below. */
struct ArcSeat {
  LayoutSlot slot;
  int arc;
  float degrees;
};
/* By use: Smash and Jump on the inner arc beside Attack, then the powers in
   slot order, then Use, which the use prompt also offers when in range. */
const ArcSeat kArcSeats[] = {
    {kSlotHeavyAttack, 0, 180.0F}, {kSlotJump, 0, 135.0F},
    {kSlotPower1, 0, 90.0F},       {kSlotPower2, 1, 180.0F},
    {kSlotPower3, 1, 150.0F},      {kSlotPower4, 1, 120.0F},
    {kSlotUse, 1, 90.0F}};

const char *const kSlotNames[] = {
    "vitals",       "potions",      "portraits", "minimap", "stick",
    "light-attack", "heavy-attack", "use",       "jump",    "power-1",
    "power-2",      "power-3",      "power-4"};

static_assert((int)(sizeof kSlotNames / sizeof kSlotNames[0]) ==
                  (int)kSlotCount,
              "every LayoutSlot needs a name");

} // namespace

const char *layout_slot_name(int slot) {
  if (slot < 0 || slot >= (int)kSlotCount)
    return "invalid-slot";
  return kSlotNames[slot];
}

int layout_slot_is_hud(int slot) {
  return slot >= (int)kSlotVitals && slot <= (int)kSlotMinimap;
}

int layout_rects_overlap(Rect a, Rect b) {
  return a.left < b.right && b.left < a.right && a.top < b.bottom &&
         b.top < a.bottom;
}

namespace {
int finite_viewport(LayoutViewport v) {
  return isfinite(v.width) && isfinite(v.height) && isfinite(v.safe_left) &&
         isfinite(v.safe_top) && isfinite(v.safe_right) &&
         isfinite(v.safe_bottom);
}

/* A square of `size`, centred on (x, y). Every control is round or square and
   is placed by its centre, so the arithmetic exists once. */
Rect centred(float x, float y, float size) {
  const float half = size * 0.5F;
  Rect r = {x - half, y - half, x + half, y + half};
  return r;
}

/* The smallest rectangle holding all `count` rectangles. */
Rect bounds(const Rect *rects, unsigned count) {
  Rect r = rects[0];
  for (unsigned i = 1; i < count; ++i) {
    r.left = fminf(r.left, rects[i].left);
    r.top = fminf(r.top, rects[i].top);
    r.right = fmaxf(r.right, rects[i].right);
    r.bottom = fmaxf(r.bottom, rects[i].bottom);
  }
  return r;
}

/* The action cluster in fingers, Attack's centre at the origin, y up. */
struct ArcCluster {
  float radius[2];
  /* How far the cluster reaches left of and above Attack's centre, to the
     outer edge of its furthest button. */
  float reach_left;
  float reach_up;
};

struct Point {
  float x;
  float y;
};

Point seat_centre(const ArcCluster &cluster, const ArcSeat &seat) {
  const float angle = seat.degrees * 3.14159265F / 180.0F;
  return {cosf(angle) * cluster.radius[seat.arc],
          sinf(angle) * cluster.radius[seat.arc]};
}

/* Two squares, by centre and side, at least `gap` apart on one axis. */
bool squares_clear(Point a, float side_a, Point b, float side_b, float gap) {
  const float apart = (side_a + side_b) * 0.5F + gap - 1e-4F;
  return fabsf(a.x - b.x) >= apart || fabsf(a.y - b.y) >= apart;
}

/* Each arc at the smallest radius whose seats clear Attack, the inner arc and
   each other: hit zones are squares, so a diagonal seat sits further out
   than a seat straight left or up. */
ArcCluster arc_cluster() {
  ArcCluster cluster{};
  const Point attack{0.0F, 0.0F};
  const float step = 0.01F;
  for (int arc = 0; arc < 2; ++arc) {
    float radius =
        arc == 0
            ? (kPrimaryFingers + kSecondaryFingers) * 0.5F + kClusterGapFingers
            : cluster.radius[0] + kSecondaryFingers + kClusterGapFingers;
    for (bool clear = false; !clear; radius += step) {
      cluster.radius[arc] = radius;
      clear = true;
      for (const ArcSeat &seat : kArcSeats) {
        if (seat.arc != arc) {
          continue;
        }
        const Point at = seat_centre(cluster, seat);
        clear = clear && squares_clear(at, kSecondaryFingers, attack,
                                       kPrimaryFingers, kClusterGapFingers);
        for (const ArcSeat &other : kArcSeats) {
          if (other.arc > arc || &other == &seat) {
            continue;
          }
          clear = clear && squares_clear(at, kSecondaryFingers,
                                         seat_centre(cluster, other),
                                         kSecondaryFingers, kClusterGapFingers);
        }
      }
    }
  }
  for (const ArcSeat &seat : kArcSeats) {
    const Point at = seat_centre(cluster, seat);
    cluster.reach_left =
        fmaxf(cluster.reach_left, -at.x + kSecondaryFingers * 0.5F);
    cluster.reach_up = fmaxf(cluster.reach_up, at.y + kSecondaryFingers * 0.5F);
  }
  return cluster;
}
} // namespace

int layout_build(LayoutViewport v, const HudPlacement *hud, Rect *out) {
  float left, top, right, bottom, width, height, shortest;
  float inset, stick, gap, finger;

  if (!out || !finite_viewport(v))
    return 0;
  left = v.safe_left;
  top = v.safe_top;
  right = v.width - v.safe_right;
  bottom = v.height - v.safe_bottom;
  width = right - left;
  height = bottom - top;
  if (!(width > 0.0F) || !(height > 0.0F))
    return 0;

  shortest = width < height ? width : height;

  inset = shortest * kEdgeInset;
  stick = shortest * kStickDiameter;
  gap = shortest * kLayoutGap;
  finger = shortest * kFinger;

  /* --- The retail HUD: copied from its owner, never re-guessed ------- */
  /* No placement means empty bands: the controls lay out as if the HUD were
     not on screen. */
  out[kSlotVitals] = hud ? hud->vitals : Rect{0.0F, 0.0F, 0.0F, 0.0F};
  out[kSlotPotions] =
      hud ? bounds(hud->potions, kHudPotions) : Rect{0.0F, 0.0F, 0.0F, 0.0F};
  out[kSlotPortraits] =
      hud ? bounds(hud->portraits, 4) : Rect{0.0F, 0.0F, 0.0F, 0.0F};
  out[kSlotMinimap] = hud ? hud->minimap : Rect{0.0F, 0.0F, 0.0F, 0.0F};

  /*
   * The two thumb clusters share one band and the right one sits under the
   * portraits and the minimap, so their natural sizes are only a request: one
   * factor scales both until they fit. The action buttons never go below a
   * finger; on a viewport too narrow for that the stick gives up the width.
   */
  {
    const ArcCluster cluster = arc_cluster();
    const float reach_left = (kCornerFingers + cluster.reach_left) * finger;
    const float reach_up = (kCornerFingers + cluster.reach_up) * finger;
    const float needed = inset + stick + gap * 2.0F + reach_left;
    const float room_h =
        bottom - fmaxf(out[kSlotPortraits].bottom, out[kSlotMinimap].bottom);
    /* The stick sits under the potions, so it must fit the room left there. */
    const float room_stick = bottom - out[kSlotPotions].bottom;
    const float fit =
        fminf(1.0F, fminf(fminf(width / needed, room_h / (reach_up + gap)),
                          room_stick / (inset + stick)));
    const float action_fit = fmaxf(fit, 1.0F / kSecondaryFingers);
    const float unit = finger * action_fit;
    const float s_inset = inset * fit;
    const float s_stick = fminf(stick * fit, width - s_inset - gap * 2.0F -
                                                 reach_left * action_fit);

    /* --- Movement, bottom left ---------------------------------------- */
    out[kSlotStick] = centred(left + s_inset + s_stick * 0.5F,
                              bottom - s_inset - s_stick * 0.5F, s_stick);

    /* --- Actions, bottom right ---------------------------------------- */
    {
      const float attack_x = right - kCornerFingers * unit;
      const float attack_y = bottom - kCornerFingers * unit;
      out[kSlotLightAttack] =
          centred(attack_x, attack_y, kPrimaryFingers * unit);
      for (const ArcSeat &seat : kArcSeats) {
        const Point at = seat_centre(cluster, seat);
        out[seat.slot] = centred(attack_x + at.x * unit, attack_y - at.y * unit,
                                 kSecondaryFingers * unit);
      }
    }
  }

  return 1;
}

Rect layout_clear_of(LayoutViewport v, Rect natural, const Rect *drawn,
                     unsigned count) {
  const auto clear = [&](Rect box, float margin) {
    for (unsigned i = 0; i < count; ++i) {
      const Rect grown = {drawn[i].left - margin, drawn[i].top - margin,
                          drawn[i].right + margin, drawn[i].bottom + margin};
      if (layout_rects_overlap(box, grown))
        return false;
    }
    return true;
  };
  if (!finite_viewport(v) || clear(natural, 0.0F))
    return natural;
  const float left = v.safe_left;
  const float top = v.safe_top;
  const float right = v.width - v.safe_right;
  const float bottom = v.height - v.safe_bottom;
  const float width = natural.right - natural.left;
  const float height = natural.bottom - natural.top;
  const float gap = fminf(right - left, bottom - top) * kLayoutGap;
  /* Flush against an obstacle at the gap; the margin is a hair under it so
     that flush placement counts as clear. */
  const float margin = gap * 0.99F;
  Rect best = natural;
  float best_distance = INFINITY;
  const auto consider = [&](float x, float y) {
    const Rect box = {x, y, x + width, y + height};
    if (box.left < left || box.top < top || box.right > right ||
        box.bottom > bottom || !clear(box, margin))
      return;
    const float dx = x - natural.left;
    const float dy = y - natural.top;
    const float distance = dx * dx + dy * dy;
    if (distance < best_distance) {
      best_distance = distance;
      best = box;
    }
  };
  /* Every place the box can stop at: where it is, against a safe edge, or
     flush beside, above or below something drawn, on each axis. */
  const auto stop_x = [&](unsigned i) {
    if (i == 0)
      return natural.left;
    if (i == 1)
      return left;
    if (i == 2)
      return right - width;
    const Rect &d = drawn[(i - 3) / 2];
    return (i - 3) % 2 ? d.right + gap : d.left - gap - width;
  };
  const auto stop_y = [&](unsigned i) {
    if (i == 0)
      return natural.top;
    if (i == 1)
      return top;
    if (i == 2)
      return bottom - height;
    const Rect &d = drawn[(i - 3) / 2];
    return (i - 3) % 2 ? d.bottom + gap : d.top - gap - height;
  };
  const unsigned stops = 3 + 2 * count;
  for (unsigned i = 0; i < stops; ++i)
    for (unsigned j = 0; j < stops; ++j)
      consider(stop_x(i), stop_y(j));
  return best;
}

Rect layout_stick_reach(LayoutViewport viewport, const Rect *slots) {
  const Rect ring = slots[kSlotStick];
  float right = viewport.width * 0.5F;
  float top = viewport.height * 0.5F;
  for (int slot = kSlotLightAttack; slot <= kSlotPower4; ++slot) {
    right = fminf(right, slots[slot].left);
  }
  top = fmaxf(top, slots[kSlotPotions].bottom);
  const Rect reach = {viewport.safe_left, fminf(top, ring.top),
                      fmaxf(right, ring.right),
                      viewport.height - viewport.safe_bottom};
  return reach;
}

} // namespace x2::presentation
