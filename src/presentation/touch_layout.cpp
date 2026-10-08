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
/* Every round button -- the four actions and the four powers -- is this one
   size, so no control reads as more important than its neighbours. */
const float kButtonDiameter = 0.165F;
const float kButtonGap = 0.025F;
const float kEdgeInset = 0.06F;
/* The powers ring the action diamond on its open side, inboard and up, where
   the right thumb reaches from the attacks without crossing them. Degrees
   from the cluster's right, counter-clockwise, in slot order. The angles are
   not evenly spread: each neighbouring pair -- and each power beside Jump or
   Use -- differs by a full diameter on one axis, so no two touch squares
   overlap, and the highest stays clear of the party portraits. */
const float kPowerAngles[4] = {208.0F, 179.0F, 146.0F, 109.0F};
/* The game's own menu icons are 32 units of its 384-unit screen height; the
   port menu that waits for them is that size. */
const float kMenuIconSize = 32.0F / 384.0F; /* of the short edge */

const char *const kSlotNames[] = {
    "vitals",       "potions", "portraits", "stick",   "light-attack",
    "heavy-attack", "use",     "jump",      "power-1", "power-2",
    "power-3",      "power-4", "port-menu"};

static_assert((int)(sizeof kSlotNames / sizeof kSlotNames[0]) ==
                  (int)kSlotCount,
              "every LayoutSlot needs a name");

const char *const kMenuSlotNames[] = {
    "dpad-up", "dpad-down", "dpad-left", "dpad-right",    "a",
    "b",       "x",         "y",         "left-shoulder", "right-shoulder"};

static_assert((int)(sizeof kMenuSlotNames / sizeof kMenuSlotNames[0]) ==
                  (int)kMenuSlotCount,
              "every MenuSlot needs a name");
} // namespace

const char *menu_slot_name(int slot) {
  if (slot < 0 || slot >= (int)kMenuSlotCount)
    return "invalid-slot";
  return kMenuSlotNames[slot];
}

const char *layout_slot_name(int slot) {
  if (slot < 0 || slot >= (int)kSlotCount)
    return "invalid-slot";
  return kSlotNames[slot];
}

int layout_slot_is_hud(int slot) {
  return slot >= (int)kSlotVitals && slot <= (int)kSlotPortraits;
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
} // namespace

int layout_build(LayoutViewport v, const HudPlacement *hud, Rect *out) {
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

  /* --- The retail HUD: copied from its owner, never re-guessed ------- */
  /* No placement means empty bands: the controls lay out as if the HUD were
     not on screen. */
  out[kSlotVitals] = hud ? hud->vitals : Rect{0.0F, 0.0F, 0.0F, 0.0F};
  out[kSlotPotions] =
      hud ? bounds(hud->potions, kHudPotions) : Rect{0.0F, 0.0F, 0.0F, 0.0F};
  out[kSlotPortraits] =
      hud ? bounds(hud->portraits, 4) : Rect{0.0F, 0.0F, 0.0F, 0.0F};

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
    const float room_h = bottom - out[kSlotPortraits].bottom;
    /* The stick sits under the potions, so it must fit the room left there. */
    const float room_stick = bottom - out[kSlotPotions].bottom;
    const float fit =
        fminf(1.0F, fminf(fminf(width / needed, room_h / needed_h),
                          room_stick / (inset + stick)));
    const float s_stick = stick * fit;
    const float s_button = button * fit;
    const float s_inset = inset * fit;
    const float s_reach = s_button + gap * fit * 0.5F;
    const float s_extent = s_reach + s_button * 0.5F;

    /* --- Movement, bottom left ---------------------------------------- */
    out[kSlotStick] = centred(left + s_inset + s_stick * 0.5F,
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
    out[kSlotLightAttack] = centred(cluster_x, cluster_y + s_reach, s_button);
    out[kSlotHeavyAttack] = centred(cluster_x + s_reach, cluster_y, s_button);
    out[kSlotUse] = centred(cluster_x, cluster_y - s_reach, s_button);
    out[kSlotJump] = centred(cluster_x - s_reach, cluster_y, s_button);

    /* Four power slots on an arc outside the diamond, centred as far out as
     * the diamond's reach plus both radii and a gap, so no power touches an
     * action button whatever the fit. */
    {
      const float s_power = power * fit;
      const float radius = arc * fit;
      for (int i = 0; i < 4; ++i) {
        const float angle = kPowerAngles[i] * 3.14159265F / 180.0F;
        out[kSlotPower1 + i] =
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
      const float mid = (left + right) * 0.5F;
      const float y = top + gap + icon * 0.5F;
      /* Right of the game's icon pair, shrunk to fit short of the portraits;
         under the portraits when the HUD leaves no room there. */
      const float menu_left = mid + icon + gap * 0.25F;
      const float room = out[kSlotPortraits].left - gap * 0.25F - menu_left;
      const float size = hud ? fminf(icon, room) : icon;
      Rect menu = {menu_left, top + gap, menu_left + size, top + gap + size};
      for (int i = kSlotVitals; i <= kSlotPortraits; ++i) {
        if (size < icon * 0.5F || layout_rects_overlap(menu, out[i])) {
          menu = centred(menu_left + icon * 0.5F,
                         out[kSlotPortraits].bottom + gap * 0.25F + icon * 0.5F,
                         icon);
          break;
        }
      }
      out[kSlotPortMenu] = menu;
    }
  }

  return 1;
}

int layout_build_menu(LayoutViewport v, Rect *out) {
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
  out[kMenuDpadUp] = centred(dpad_x, centre_y - reach, button);
  out[kMenuDpadDown] = centred(dpad_x, centre_y + reach, button);
  out[kMenuDpadLeft] = centred(dpad_x - reach, centre_y, button);
  out[kMenuDpadRight] = centred(dpad_x + reach, centre_y, button);
  out[kMenuA] = centred(face_x, centre_y + reach, button);
  out[kMenuB] = centred(face_x + reach, centre_y, button);
  out[kMenuX] = centred(face_x - reach, centre_y, button);
  out[kMenuY] = centred(face_x, centre_y - reach, button);
  {
    const float shoulder_y = centre_y - extent - gap - button * 0.5F;
    out[kMenuLeftShoulder] = centred(dpad_x, shoulder_y, button);
    out[kMenuRightShoulder] = centred(face_x, shoulder_y, button);
  }
  return 1;
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
