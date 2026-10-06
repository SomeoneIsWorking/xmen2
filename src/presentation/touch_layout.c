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
static const float kStickDiameter = 0.38F; /* of the short edge */
/* Every round button -- the four actions and the four powers -- is this one
   size, so no control reads as more important than its neighbours. */
static const float kButtonDiameter = 0.165F;
static const float kButtonGap = 0.025F;
static const float kEdgeInset = 0.06F;
static const float kHudVitalsWidth = 0.30F;  /* of the WIDTH */
static const float kHudVitalsHeight = 0.14F; /* of the HEIGHT */
static const float kHudPotionsHeight = 0.17F;
static const float kHudPortraitsWidth = 0.24F;
static const float kHudPortraitsHeight = 0.22F;
static const float kHudGap = 0.02F;
/* The powers ring the action diamond on its open side, inboard and up, where
   the right thumb reaches from the attacks without crossing them. Degrees
   from the cluster's right, counter-clockwise, in slot order. The angles are
   not evenly spread: each neighbouring pair -- and each power beside Jump or
   Use -- differs by a full diameter on one axis, so no two touch squares
   overlap, and the highest stays clear of the party portraits. */
static const float kPowerAngles[4] = {208.0F, 179.0F, 146.0F, 109.0F};
/* The game's own menu icons are 32 units of its 384-unit screen height; the
   port menu that waits for them is that size. */
static const float kMenuIconSize = 32.0F / 384.0F; /* of the short edge */

static const char *const kSlotNames[] = {
    "vitals",       "potions", "portraits", "stick",   "light-attack",
    "heavy-attack", "use",     "jump",      "power-1", "power-2",
    "power-3",      "power-4", "port-menu"};

_Static_assert((int)(sizeof kSlotNames / sizeof kSlotNames[0]) ==
                   (int)kX2SlotCount,
               "every X2LayoutSlot needs a name");

static const char *const kMenuSlotNames[] = {
    "dpad-up", "dpad-down", "dpad-left", "dpad-right",    "a",
    "b",       "x",         "y",         "left-shoulder", "right-shoulder"};

_Static_assert((int)(sizeof kMenuSlotNames / sizeof kMenuSlotNames[0]) ==
                   (int)kX2MenuSlotCount,
               "every X2MenuSlot needs a name");

const char *x2_menu_slot_name(int slot) {
  if (slot < 0 || slot >= (int)kX2MenuSlotCount)
    return "invalid-slot";
  return kMenuSlotNames[slot];
}

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
    /* The powers' arc reaches further inboard than the diamond does. */
    const float power = button;
    const float arc = reach + button * 0.5F + power * 0.5F + gap;
    const float inboard = arc + power * 0.5F;
    const float needed = inset * 2.0F + stick + separation + extent + inboard;
    /* The right cluster also has to fit UNDER the portraits: its height is
       the diamond's lower half plus whichever rises higher, Use or the arc's
       top power. A short landscape phone runs out of height first. */
    float arc_rise = 0.0F;
    for (int i = 0; i < 4; ++i) {
      const float angle = kPowerAngles[i] * 3.14159265F / 180.0F;
      arc_rise = fmaxf(arc_rise, sinf(angle) * arc + power * 0.5F);
    }
    const float needed_h = inset + extent + fmaxf(extent, arc_rise) + gap;
    const float room_h = bottom - out[kX2SlotPortraits].bottom;
    const float fit = fminf(1.0F, fminf(width / needed, room_h / needed_h));
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
     * outer and lower positions where it rests, Use above them, Jump
     * inboard so movement and jumping use opposite thumbs. Placed by the
     * cluster's CENTRE with each button at an offset, so the whole group moves
     * as one. The centre is inset by the full `extent`, not by one button --
     * reserving one button put Heavy past the right safe edge and Light past
     * the bottom one on every viewport.
     */
    cluster_x = right - s_inset - s_extent;
    cluster_y = bottom - s_inset - s_extent;
    out[kX2SlotLightAttack] = centred(cluster_x, cluster_y + s_reach, s_button);
    out[kX2SlotHeavyAttack] = centred(cluster_x + s_reach, cluster_y, s_button);
    out[kX2SlotUse] = centred(cluster_x, cluster_y - s_reach, s_button);
    out[kX2SlotJump] = centred(cluster_x - s_reach, cluster_y, s_button);

    /* Four power slots on an arc outside the diamond, centred as far out as
     * the diamond's reach plus both radii and a gap, so no power touches an
     * action button whatever the fit. */
    {
      const float s_power = power * fit;
      const float radius = arc * fit;
      for (int i = 0; i < 4; ++i) {
        const float angle = kPowerAngles[i] * 3.14159265F / 180.0F;
        out[kX2SlotPower1 + i] =
            centred(cluster_x + cosf(angle) * radius,
                    cluster_y - sinf(angle) * radius, s_power);
      }
    }

    /* The port menu belongs in the row of menu icons the game's mouse
     * overlay draws either side of the top centre; the controls put it
     * beside them once the game reports where they are. Until then it waits
     * just right of that pair, at their size. */
    {
      const float icon = shortest * kMenuIconSize;
      out[kX2SlotPortMenu] = centred((left + right) * 0.5F + icon * 1.5F + gap,
                                     top + gap + icon * 0.5F, icon);
    }
  }

  return 1;
}

int x2_layout_build_menu(X2LayoutViewport v, X2Rect *out) {
  float left, top, right, bottom, width, height, shortest;
  float inset, button, gap, reach, extent, fit, centre_y, dpad_x, face_x;

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

  /* Two crosses of three buttons each way, a shoulder above each: the same
     button size as gameplay, scaled down by one factor if the two clusters
     would meet across the width or the shoulders would leave the top. */
  button = shortest * kButtonDiameter;
  gap = shortest * kButtonGap;
  inset = shortest * kEdgeInset;
  reach = button + gap * 0.5F;
  extent = reach + button * 0.5F;
  {
    const float needed_w = (inset + extent * 2.0F) * 2.0F + button;
    const float needed_h = inset + extent * 2.0F + gap + button;
    fit = fminf(1.0F, fminf(width / needed_w, height / needed_h));
  }
  button *= fit;
  gap *= fit;
  inset *= fit;
  reach *= fit;
  extent *= fit;

  centre_y = bottom - inset - extent;
  dpad_x = left + inset + extent;
  face_x = right - inset - extent;
  out[kX2MenuDpadUp] = centred(dpad_x, centre_y - reach, button);
  out[kX2MenuDpadDown] = centred(dpad_x, centre_y + reach, button);
  out[kX2MenuDpadLeft] = centred(dpad_x - reach, centre_y, button);
  out[kX2MenuDpadRight] = centred(dpad_x + reach, centre_y, button);
  out[kX2MenuA] = centred(face_x, centre_y + reach, button);
  out[kX2MenuB] = centred(face_x + reach, centre_y, button);
  out[kX2MenuX] = centred(face_x - reach, centre_y, button);
  out[kX2MenuY] = centred(face_x, centre_y - reach, button);
  {
    const float shoulder_y = centre_y - extent - gap - button * 0.5F;
    out[kX2MenuLeftShoulder] = centred(dpad_x, shoulder_y, button);
    out[kX2MenuRightShoulder] = centred(face_x, shoulder_y, button);
  }
  return 1;
}

X2Rect x2_layout_stick_reach(X2LayoutViewport viewport, const X2Rect *slots) {
  const X2Rect ring = slots[kX2SlotStick];
  float right = viewport.width * 0.5F;
  float top = viewport.height * 0.5F;
  for (int slot = kX2SlotLightAttack; slot <= kX2SlotPower4; ++slot) {
    right = fminf(right, slots[slot].left);
  }
  top = fmaxf(top, slots[kX2SlotPotions].bottom);
  const X2Rect reach = {viewport.safe_left, fminf(top, ring.top),
                        fmaxf(right, ring.right),
                        viewport.height - viewport.safe_bottom};
  return reach;
}
