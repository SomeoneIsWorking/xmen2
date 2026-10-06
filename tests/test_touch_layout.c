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
    {"phone logical 844x390", 844.0f, 390.0f, 32, 0, 20, 12},
    {"compact logical 640x360", 640.0f, 360.0f, 0, 0, 0, 12},
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
         a range would also include the left-thumb modifier when slots change.
       */
      const int cluster[] = {(int)kX2SlotLightAttack, (int)kX2SlotHeavyAttack,
                             (int)kX2SlotUse, (int)kX2SlotJump};
      for (i = 0; i < (int)(sizeof cluster / sizeof cluster[0]); i++) {
        const int slot = cluster[i];
        CHECK(x2_layout_slot_name(slot),
              slots[slot].right > viewport.width * 0.5f);
        CHECK(x2_layout_slot_name(slot),
              slots[slot].bottom > viewport.height * 0.5f);
      }
    }
    /* The powers belong to the right thumb with the abilities they chord:
       inboard of the diamond, never under the left thumb's stick. */
    for (i = (int)kX2SlotPower1; i <= (int)kX2SlotPower4; ++i) {
      CHECK(x2_layout_slot_name(i), slots[i].left > slots[kX2SlotStick].right);
      CHECK(x2_layout_slot_name(i),
            slots[i].right <= slots[kX2SlotHeavyAttack].left);
      CHECK(x2_layout_slot_name(i),
            slots[i].top > slots[kX2SlotPortraits].bottom);
    }
    /* The stick's reach holds the ring, stays left of the centreline and of
       every action and power, below the potions, inside the safe region. */
    {
      const X2Rect reach = x2_layout_stick_reach(viewport, slots);
      const X2Rect ring = slots[kX2SlotStick];
      CHECK("stick reach holds the ring",
            reach.left <= ring.left && reach.top <= ring.top &&
                reach.right >= ring.right && reach.bottom >= ring.bottom);
      CHECK("stick reach is larger than the ring", area(reach) > area(ring));
      CHECK("stick reach is inside the safe region",
            reach.left >= viewport.safe_left - 0.5f &&
                reach.bottom <= viewport.height - viewport.safe_bottom + 0.5f);
      CHECK("stick reach is below the potions",
            reach.top >= slots[kX2SlotPotions].bottom - 0.5f);
      for (i = (int)kX2SlotLightAttack; i <= (int)kX2SlotPower4; ++i)
        CHECK(x2_layout_slot_name(i),
              !x2_layout_rects_overlap(reach, slots[i]));
    }
    /* The port menu waits in the top band just right of the centreline,
       where the game's own pause and team icons sit either side of it, and
       at their size: not an action button's. */
    {
      const X2Rect menu = slots[kX2SlotPortMenu];
      const float centre =
          (viewport.safe_left + viewport.width - viewport.safe_right) * 0.5f;
      CHECK(name, menu.bottom < viewport.height * 0.25f);
      CHECK(name, menu.left > centre);
      CHECK(name, menu.right - menu.left < slots[kX2SlotLightAttack].right -
                                               slots[kX2SlotLightAttack].left);
    }
    /* Every round button is one size: the actions are not larger than the
       powers beside them. */
    for (i = (int)kX2SlotHeavyAttack; i <= (int)kX2SlotPower4; ++i)
      CHECK("every action and power is one size",
            fabsf((slots[i].right - slots[i].left) -
                  (slots[kX2SlotLightAttack].right -
                   slots[kX2SlotLightAttack].left)) < 0.01f);
    /* The stick and the action cluster must not be reachable by one hand
       only because they are close: they belong to opposite thumbs. */
    CHECK(name, slots[kX2SlotStick].right < slots[kX2SlotJump].left);
    /* A resting left thumb can move while the right thumb jumps. */
    CHECK(name, slots[kX2SlotJump].right <= slots[kX2SlotLightAttack].left);
    for (i = (int)kX2SlotLightAttack; i <= (int)kX2SlotPower4; ++i)
      CHECK("face/power target at least 48 output pixels on tested devices",
            slots[i].right - slots[i].left >= 48.0f);
  }

  /* The menu pad, over the same sweep: inside the safe region, no button on
     another, the d-pad under the left thumb and the face buttons under the
     right in the Xbox arrangement, each shoulder above its own cluster, and
     every button at least a thumb wide. */
  for (v = 0; v < sizeof kViewports / sizeof kViewports[0]; v++) {
    X2LayoutViewport viewport = {
        kViewports[v].width,      kViewports[v].height,
        kViewports[v].safe_left,  kViewports[v].safe_top,
        kViewports[v].safe_right, kViewports[v].safe_bottom};
    X2Rect menu[kX2MenuSlotCount];
    const char *name = kViewports[v].name;
    const float middle =
        (viewport.safe_left + viewport.width - viewport.safe_right) * 0.5f;
    int i, j;

    CHECK(name, x2_layout_build_menu(viewport, menu));
    for (i = 0; i < (int)kX2MenuSlotCount; i++) {
      const X2Rect r = menu[i];
      CHECK(x2_menu_slot_name(i), r.left >= viewport.safe_left - 0.5f);
      CHECK(x2_menu_slot_name(i), r.top >= viewport.safe_top - 0.5f);
      CHECK(x2_menu_slot_name(i),
            r.right <= viewport.width - viewport.safe_right + 0.5f);
      CHECK(x2_menu_slot_name(i),
            r.bottom <= viewport.height - viewport.safe_bottom + 0.5f);
      CHECK(x2_menu_slot_name(i), r.right - r.left >= 48.0f);
      for (j = i + 1; j < (int)kX2MenuSlotCount; j++)
        CHECK(name, !x2_layout_rects_overlap(menu[i], menu[j]));
    }
    for (i = (int)kX2MenuDpadUp; i <= (int)kX2MenuDpadRight; i++)
      CHECK(x2_menu_slot_name(i), menu[i].right <= middle);
    for (i = (int)kX2MenuA; i <= (int)kX2MenuY; i++)
      CHECK(x2_menu_slot_name(i), menu[i].left >= middle);
    CHECK("up above down",
          menu[kX2MenuDpadUp].bottom <= menu[kX2MenuDpadDown].top);
    CHECK("left of right",
          menu[kX2MenuDpadLeft].right <= menu[kX2MenuDpadRight].left);
    CHECK("A below Y", menu[kX2MenuY].bottom <= menu[kX2MenuA].top);
    CHECK("X left of B", menu[kX2MenuX].right <= menu[kX2MenuB].left);
    CHECK("left shoulder above the d-pad",
          menu[kX2MenuLeftShoulder].bottom <= menu[kX2MenuDpadUp].top &&
              menu[kX2MenuLeftShoulder].right <= middle);
    CHECK("right shoulder above the face buttons",
          menu[kX2MenuRightShoulder].bottom <= menu[kX2MenuY].top &&
              menu[kX2MenuRightShoulder].left >= middle);
  }
  {
    X2Rect menu[kX2MenuSlotCount];
    X2LayoutViewport inverted = {100.0f, 100.0f, 80.0f, 0, 80.0f, 0};
    X2LayoutViewport ok = {800.0f, 600.0f, 0, 0, 0, 0};
    int i;
    CHECK("menu pad: no usable area", !x2_layout_build_menu(inverted, menu));
    CHECK("menu pad: null destination", !x2_layout_build_menu(ok, NULL));
    for (i = 0; i < (int)kX2MenuSlotCount; i++)
      CHECK("menu slot name", x2_menu_slot_name(i)[0] != '\0');
    CHECK("menu slot out of range",
          x2_menu_slot_name((int)kX2MenuSlotCount)[0] != '\0');
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
