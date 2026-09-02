#ifndef X2_TOUCH_LAYOUT_H
#define X2_TOUCH_LAYOUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*
 * WHERE EVERYTHING GOES IN TOUCH MODE -- one authority, read by both owners.
 *
 * THIS FILE EXISTS BECAUSE THE PREVIOUS DESIGN HAD TWO. The retail HUD was
 * relocated by mirroring its authored anchor across the output axes, and the
 * touch hit-zones for the party portraits were an independent guess at where
 * that mirror had landed. Two sources of truth for one rectangle can only
 * agree by luck, and they did not: the portraits could not be tapped where
 * they were drawn.
 *
 * So the HUD elements and the touch zones are computed HERE, together, from
 * one viewport. `touch_hud_runtime.c` moves the retail scene objects to these
 * rectangles and `touch_controls.cpp` routes contacts against the same ones.
 * Neither computes a rectangle of its own.
 *
 * Coordinates are OUTPUT PIXELS with y downward, which is also the retail
 * HUD's own space -- CHud's authored anchor is (48, 552) in an 800x600 output,
 * i.e. bottom-left. There is no second coordinate convention to convert
 * between.
 */

typedef struct X2Rect {
  float left;
  float top;
  float right;
  float bottom;
} X2Rect;

/* The output, and the region of it a player can actually reach: a phone's
   cutout and gesture bar are not drawable, and a control placed under one is
   invisible or steals the system gesture. */
typedef struct X2LayoutViewport {
  float width;
  float height;
  float safe_left;
  float safe_top;
  float safe_right;
  float safe_bottom;
} X2LayoutViewport;

/*
 * Every placed thing, named. The order is the enumeration order and
 * kX2SlotCount is the denominator any exhaustive check counts against.
 *
 * The HUD slots are the retail elements this port MOVES; the control slots are
 * the ones it DRAWS. They share one enum because they share one rectangle
 * space and must not overlap -- a check that they do not is only possible if
 * one list holds both.
 */
typedef enum X2LayoutSlot {
  /* Retail HUD, relocated. Vitals and potions to the top left, the party
     portraits to the top right, per the requested layout. */
  kX2SlotVitals = 0,
  kX2SlotPotions,
  kX2SlotPortraits,
  /* Port-drawn touch controls: movement bottom left, actions bottom right. */
  kX2SlotStick,
  kX2SlotLightAttack,
  kX2SlotHeavyAttack,
  kX2SlotUse,
  kX2SlotJump,
  kX2SlotPowers,
  kX2SlotPause,
  kX2SlotCount /* MUST stay last */
} X2LayoutSlot;

/* Name of a slot, for traces and refusals. Never null, for every value below
   kX2SlotCount. */
const char *x2_layout_slot_name(int slot);

/* True when the slot is a retail HUD element rather than a port-drawn control.
   The two are placed by different owners and the distinction is the layout's
   to state, not each caller's to rediscover from the enumerator's spelling. */
int x2_layout_slot_is_hud(int slot);

/*
 * Fill `out` with kX2SlotCount rectangles for this viewport.
 *
 * Returns 0 without touching `out` when the viewport has no usable area --
 * a zero or negative safe region, a non-finite dimension. A caller that got 0
 * has no layout, which is a different fact from a layout of empty rectangles,
 * and the difference decides whether it should draw nothing or refuse.
 */
int x2_layout_build(X2LayoutViewport viewport, X2Rect *out);

/* Whether two placed rectangles overlap. Exposed because the invariant that
   the HUD and the controls do not sit on top of each other is worth asserting
   in a test at every aspect ratio, not just believing at one. */
int x2_layout_rects_overlap(X2Rect a, X2Rect b);

#ifdef __cplusplus
}
#endif

#endif /* X2_TOUCH_LAYOUT_H */
