/*
 * The layout's invariants, over a sweep of real device shapes.
 *
 * The design this replaces could not state an invariant at all: the HUD was
 * placed by mirroring and the hit-zones by an independent guess, so "the
 * portraits are where you can tap them" was not a property of the code, it was
 * a coincidence to be checked by eye on a device. Every check below is one
 * that would have caught that.
 */
#include "../src/presentation/touch_layout.h"

#include <math.h>
#include <stdio.h>

static int g_checks;
static int g_failed;

#define CHECK(what, cond)                                                      \
  do {                                                                         \
    g_checks++;                                                                \
    if (!(cond)) {                                                             \
      g_failed++;                                                              \
      printf("    FAIL %s: %s\n", (what), #cond);                              \
    }                                                                          \
  } while (0)

/* Phone, tablet, ultrawide, square, and a portrait orientation -- the shapes
   a layout expressed in fractions of one axis silently breaks on. */
static const struct {
  const char *name;
  float width;
  float height;
  float safe_left;
  float safe_top;
  float safe_right;
  float safe_bottom;
} kViewports[] = {
    {"phone 2400x1080", 2400.0f, 1080.0f, 0, 0, 0, 0},
    {"phone with cutout", 2400.0f, 1080.0f, 120.0f, 0, 48.0f, 24.0f},
    {"tablet 2560x1600", 2560.0f, 1600.0f, 0, 0, 0, 0},
    {"ultrawide 3440x1440", 3440.0f, 1440.0f, 0, 0, 0, 0},
    {"square 1000x1000", 1000.0f, 1000.0f, 0, 0, 0, 0},
    {"portrait 1080x2400", 1080.0f, 2400.0f, 0, 40.0f, 0, 60.0f},
    {"desktop 800x600", 800.0f, 600.0f, 0, 0, 0, 0},
};

static float area(X2Rect r) { return (r.right - r.left) * (r.bottom - r.top); }

int main(void) {
  size_t v;

  printf("test the touch layout over %zu viewport(s)\n",
         sizeof kViewports / sizeof kViewports[0]);

  for (v = 0; v < sizeof kViewports / sizeof kViewports[0]; v++) {
    X2LayoutViewport viewport = {
        kViewports[v].width,      kViewports[v].height,
        kViewports[v].safe_left,  kViewports[v].safe_top,
        kViewports[v].safe_right, kViewports[v].safe_bottom};
    X2Rect slots[kX2SlotCount];
    const char *name = kViewports[v].name;
    int i, j;

    CHECK(name, x2_layout_build(viewport, slots));
    if (g_failed)
      break;

    for (i = 0; i < (int)kX2SlotCount; i++) {
      const X2Rect r = slots[i];
      /* Inside the safe region, on every edge. A control under a cutout
         is invisible; one under the gesture bar steals the gesture. */
      CHECK(x2_layout_slot_name(i), r.left >= viewport.safe_left - 0.5f);
      CHECK(x2_layout_slot_name(i), r.top >= viewport.safe_top - 0.5f);
      CHECK(x2_layout_slot_name(i),
            r.right <= viewport.width - viewport.safe_right + 0.5f);
      CHECK(x2_layout_slot_name(i),
            r.bottom <= viewport.height - viewport.safe_bottom + 0.5f);
      CHECK(x2_layout_slot_name(i), area(r) > 0.0f);
    }

    /* Nothing sits on top of anything else. This is the property the two
       independent guesses could not have. */
    for (i = 0; i < (int)kX2SlotCount; i++)
      for (j = i + 1; j < (int)kX2SlotCount; j++)
        CHECK(name, !x2_layout_rects_overlap(slots[i], slots[j]));

    /* And the requested arrangement, stated as geometry rather than
       trusted to the comments: vitals and potions top-left, faces
       top-right, stick bottom-left, actions bottom-right. */
    CHECK(name, slots[kX2SlotVitals].top < viewport.height * 0.5f);
    CHECK(name, slots[kX2SlotVitals].left < viewport.width * 0.5f);
    CHECK(name, slots[kX2SlotPotions].top > slots[kX2SlotVitals].top);
    CHECK(name, slots[kX2SlotPotions].left < viewport.width * 0.5f);
    CHECK(name, slots[kX2SlotPortraits].right > viewport.width * 0.5f);
    CHECK(name, slots[kX2SlotPortraits].top < viewport.height * 0.5f);
    CHECK(name, slots[kX2SlotStick].left < viewport.width * 0.5f);
    CHECK(name, slots[kX2SlotStick].bottom > viewport.height * 0.5f);
    {
      /* The combat diamond, named one by one rather than swept over a range:
         a range walks whatever slots are added between its ends, and Jump --
         a LEFT-thumb control -- was added between them. */
      const int cluster[] = {(int)kX2SlotLightAttack, (int)kX2SlotHeavyAttack,
                             (int)kX2SlotUse, (int)kX2SlotPowers};
      for (i = 0; i < (int)(sizeof cluster / sizeof cluster[0]); i++) {
        const int slot = cluster[i];
        CHECK(x2_layout_slot_name(slot),
              slots[slot].right > viewport.width * 0.5f);
        CHECK(x2_layout_slot_name(slot),
              slots[slot].bottom > viewport.height * 0.5f);
      }
    }
    /* Jump belongs to the movement thumb: left half, above the stick. */
    CHECK(name, slots[kX2SlotJump].left < viewport.width * 0.5f);
    CHECK(name, slots[kX2SlotJump].bottom <= slots[kX2SlotStick].top);
    /* Pause sits on the top edge between the two HUD corners. */
    CHECK(name, slots[kX2SlotPause].top < viewport.height * 0.5f);
    CHECK(name, slots[kX2SlotPause].left > slots[kX2SlotVitals].right ||
                    slots[kX2SlotPause].top > slots[kX2SlotVitals].bottom);
    /* The stick and the action cluster must not be reachable by one hand
       only because they are close: they belong to opposite thumbs. */
    CHECK(name, slots[kX2SlotStick].right < slots[kX2SlotPowers].left);
  }

  /* Refusals. A viewport with no usable area has no layout, and saying so is
     different from returning eight empty rectangles. */
  {
    X2Rect slots[kX2SlotCount];
    X2LayoutViewport empty = {0};
    X2LayoutViewport inverted = {100.0f, 100.0f, 80.0f, 0, 80.0f, 0};
    X2LayoutViewport nan_size = {NAN, 100.0f, 0, 0, 0, 0};
    X2LayoutViewport ok = {800.0f, 600.0f, 0, 0, 0, 0};
    CHECK("empty viewport", !x2_layout_build(empty, slots));
    CHECK("safe area wider than screen", !x2_layout_build(inverted, slots));
    CHECK("non-finite dimension", !x2_layout_build(nan_size, slots));
    CHECK("null destination", !x2_layout_build(ok, NULL));
  }

  /* The names are the denominator of every exhaustive check above. */
  {
    int i;
    for (i = 0; i < (int)kX2SlotCount; i++)
      CHECK("slot name", x2_layout_slot_name(i)[0] != '\0');
    CHECK("out-of-range name",
          x2_layout_slot_name((int)kX2SlotCount)[0] != '\0');
    CHECK("hud slots", x2_layout_slot_is_hud((int)kX2SlotVitals) &&
                           x2_layout_slot_is_hud((int)kX2SlotPortraits) &&
                           !x2_layout_slot_is_hud((int)kX2SlotStick));
  }

  printf("%d check(s), %d failure(s)\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
