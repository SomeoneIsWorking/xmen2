#include "touch_layout.h"

#include <math.h>

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
static const float kStickDiameter = 0.42F; /* of the short edge */
static const float kButtonDiameter = 0.17F;
static const float kButtonGap = 0.035F;
static const float kEdgeInset = 0.045F;
static const float kHudVitalsWidth = 0.30F;  /* of the WIDTH */
static const float kHudVitalsHeight = 0.14F; /* of the HEIGHT */
static const float kHudPotionsHeight = 0.10F;
static const float kHudPortraitsWidth = 0.24F;
static const float kHudPortraitsHeight = 0.22F;
static const float kHudGap = 0.02F;

static const char *const kSlotNames[] = {
    "vitals",       "potions", "portraits", "stick",  "light-attack",
    "heavy-attack", "use",     "jump",      "powers", "pause"};

_Static_assert((int)(sizeof kSlotNames / sizeof kSlotNames[0]) ==
                   (int)kX2SlotCount,
               "every X2LayoutSlot needs a name");

const char *x2_layout_slot_name(int slot) {
  if (slot < 0 || slot >= (int)kX2SlotCount)
    return "invalid-slot";
  return kSlotNames[slot];
}

int x2_layout_slot_is_hud(int slot) {
  return slot >= (int)kX2SlotVitals && slot <= (int)kX2SlotPortraits;
}

int x2_layout_rects_overlap(X2Rect a, X2Rect b) {
  return a.left < b.right && b.left < a.right && a.top < b.bottom &&
         b.top < a.bottom;
}

static int finite_viewport(X2LayoutViewport v) {
  return isfinite(v.width) && isfinite(v.height) && isfinite(v.safe_left) &&
         isfinite(v.safe_top) && isfinite(v.safe_right) &&
         isfinite(v.safe_bottom);
}

/* A square of `size`, centred on (x, y). Every control is round or square and
   is placed by its centre, so the arithmetic exists once. */
static X2Rect centred(float x, float y, float size) {
  const float half = size * 0.5F;
  X2Rect r = {x - half, y - half, x + half, y + half};
  return r;
}

int x2_layout_build(X2LayoutViewport v, X2Rect *out) {
  float left, top, right, bottom, width, height, shortest;
  float inset, stick, button, gap, cluster_x, cluster_y;

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
  button = shortest * kButtonDiameter;
  gap = shortest * kButtonGap;

  /* --- The retail HUD, top edge -------------------------------------- */
  /* Vitals and potions stack down the top-left corner; the party portraits
     take the top-right. Both are pinned to the safe edges rather than
     centred on a fraction of the screen, so a cutout moves them instead of
     cropping them. */
  {
    /* Widths span the WIDTH and heights the HEIGHT -- the axis each actually
       occupies. Taking both from the short or long edge is what put the
       top-left vitals underneath the top-right portraits at 1080x2400: on a
       portrait viewport the long edge is the one they do NOT share. */
    const float vitals_w = width * kHudVitalsWidth;
    const float vitals_h = height * kHudVitalsHeight;
    const float potions_h = height * kHudPotionsHeight;
    const float hud_gap = height * kHudGap;
    const X2Rect vitals = {left, top, left + vitals_w, top + vitals_h};
    const X2Rect potions = {left, vitals.bottom + hud_gap, left + vitals_w,
                            vitals.bottom + hud_gap + potions_h};
    const X2Rect portraits = {right - width * kHudPortraitsWidth, top, right,
                              top + height * kHudPortraitsHeight};
    out[kX2SlotVitals] = vitals;
    out[kX2SlotPotions] = potions;
    out[kX2SlotPortraits] = portraits;
  }

  /*
   * THE TWO THUMB CLUSTERS SHARE ONE BAND, so their natural sizes are only a
   * request. On a wide phone they fit with room to spare; on a square or
   * portrait viewport the short edge is large relative to the width and they
   * collide -- measured: at 1000x1000 the stick's right edge landed 55 px past
   * Powers' left one. Sizing from the short edge alone cannot see that,
   * because the collision is along the LONG one.
   *
   * So the natural sizes are scaled by whatever single factor makes them fit.
   * Shrinking only the offender would change the proportions the arrangement
   * was designed in.
   */
  {
    const float reach = button + gap * 0.5F;
    const float extent = reach + button * 0.5F;
    const float separation = button * 0.5F;
    const float needed = inset * 2.0F + stick + separation + extent * 2.0F;
    const float fit = needed > width ? width / needed : 1.0F;
    const float s_stick = stick * fit;
    const float s_button = button * fit;
    const float s_inset = inset * fit;
    const float s_reach = s_button + gap * fit * 0.5F;
    const float s_extent = s_reach + s_button * 0.5F;

    /* --- Movement, bottom left ---------------------------------------- */
    out[kX2SlotStick] = centred(left + s_inset + s_stick * 0.5F,
                                bottom - s_inset - s_stick * 0.5F, s_stick);

    /* --- Actions, bottom right ---------------------------------------- */
    /*
     * A diamond in the arrangement a right thumb reaches: the attacks on the
     * outer and lower positions where it rests, Use above them, Powers
     * inboard. Placed by the cluster's CENTRE with each button at an offset,
     * so the whole group moves as one. The centre is inset by the full
     * `extent`, not by one button -- reserving one button put Heavy past the
     * right safe edge and Light past the bottom one on every viewport.
     */
    cluster_x = right - s_inset - s_extent;
    cluster_y = bottom - s_inset - s_extent;
    out[kX2SlotLightAttack] = centred(cluster_x, cluster_y + s_reach, s_button);
    out[kX2SlotHeavyAttack] = centred(cluster_x + s_reach, cluster_y, s_button);
    out[kX2SlotUse] = centred(cluster_x, cluster_y - s_reach, s_button);
    out[kX2SlotPowers] = centred(cluster_x - s_reach, cluster_y, s_button);

    /* --- Jump, above the movement thumb -------------------------------
     * Jump is a MOVEMENT verb, so it belongs to the left thumb, not to the
     * combat diamond. Sitting directly above the stick it is reachable
     * without leaving the stick, and it cannot collide with the diamond
     * because the two clusters are at opposite ends of the band. */
    out[kX2SlotJump] =
        centred(out[kX2SlotStick].left + s_stick * 0.5F,
                out[kX2SlotStick].top - gap * fit - s_button * 0.5F, s_button);

    /* --- Pause, top centre --------------------------------------------
     * Between the two HUD corners, which is the one part of the top edge the
     * retail HUD does not claim. A touch player with no controller has no
     * other way to reach the menus at all. */
    out[kX2SlotPause] = centred((left + right) * 0.5F,
                                top + s_inset + s_button * 0.5F, s_button);
  }

  return 1;
}
