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

#include "../src/config/hud_settings.h"
#include "../src/presentation/hud_layout.h"

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

static float area(x2::presentation::Rect r) {
  return (r.right - r.left) * (r.bottom - r.top);
}

static int same_rect(x2::presentation::Rect a, x2::presentation::Rect b) {
  return a.left == b.left && a.top == b.top && a.right == b.right &&
         a.bottom == b.bottom;
}

static x2::presentation::Rect bounds(const x2::presentation::Rect *rects,
                                     unsigned count) {
  x2::presentation::Rect r = rects[0];
  for (unsigned i = 1; i < count; ++i) {
    r.left = fminf(r.left, rects[i].left);
    r.top = fminf(r.top, rects[i].top);
    r.right = fmaxf(r.right, rects[i].right);
    r.bottom = fmaxf(r.bottom, rects[i].bottom);
  }
  return r;
}

/* The HUD owner's placement at a player-chosen HUD size. */
static int hud_here(const x2::presentation::LayoutViewport *viewport,
                    unsigned scale_percent,
                    x2::presentation::HudPlacement *out) {
  X2HudSettings settings;
  x2_hud_settings_defaults(&settings);
  settings.vitals_scale_percent = scale_percent;
  settings.potions_scale_percent = scale_percent;
  settings.portraits_scale_percent = scale_percent;
  return x2::presentation::hud_layout_build(*viewport, &settings, -1.0f, out);
}

static const unsigned kHudScales[] = {X2_HUD_SCALE_MIN, 100, X2_HUD_SCALE_MAX};

int main(void) {
  size_t v;

  printf("test the touch layout over %zu viewport(s)\n",
         sizeof kViewports / sizeof kViewports[0]);

  for (v = 0; v < sizeof kViewports / sizeof kViewports[0]; v++) {
    x2::presentation::LayoutViewport viewport = {
        kViewports[v].width,      kViewports[v].height,
        kViewports[v].safe_left,  kViewports[v].safe_top,
        kViewports[v].safe_right, kViewports[v].safe_bottom};
    x2::presentation::Rect slots[x2::presentation::kSlotCount];
    const char *name = kViewports[v].name;
    x2::presentation::HudPlacement hud;
    int i, j;

    CHECK(name, hud_here(&viewport, 100, &hud));
    CHECK(name, layout_build(viewport, &hud, slots));
    if (g_failed)
      break;

    for (i = 0; i < (int)x2::presentation::kSlotCount; i++) {
      const x2::presentation::Rect r = slots[i];
      /* Inside the safe region, on every edge. A control under a cutout
         is invisible; one under the gesture bar steals the gesture. */
      CHECK(x2::presentation::layout_slot_name(i),
            r.left >= viewport.safe_left - 0.5f);
      CHECK(x2::presentation::layout_slot_name(i),
            r.top >= viewport.safe_top - 0.5f);
      CHECK(x2::presentation::layout_slot_name(i),
            r.right <= viewport.width - viewport.safe_right + 0.5f);
      CHECK(x2::presentation::layout_slot_name(i),
            r.bottom <= viewport.height - viewport.safe_bottom + 0.5f);
      CHECK(x2::presentation::layout_slot_name(i), area(r) > 0.0f);
    }

    /* Nothing sits on top of anything else. This is the property the two
       independent guesses could not have. */
    for (i = 0; i < (int)x2::presentation::kSlotCount; i++)
      for (j = i + 1; j < (int)x2::presentation::kSlotCount; j++)
        CHECK(name, !layout_rects_overlap(slots[i], slots[j]));

    /* The HUD slots are the owner's rectangles: copied, not placed twice. */
    CHECK("hud slot is the HUD owner's vitals",
          same_rect(slots[(int)x2::presentation::kSlotVitals], hud.vitals));
    CHECK("hud slot is the HUD owner's potion row",
          same_rect(slots[(int)x2::presentation::kSlotPotions],
                    bounds(hud.potions, x2::presentation::kHudPotions)));
    CHECK("hud slot is the HUD owner's portrait row",
          same_rect(slots[(int)x2::presentation::kSlotPortraits],
                    bounds(hud.portraits, 4)));

    /* And the requested arrangement, stated as geometry rather than
       trusted to the comments: vitals and potions top-left, faces
       top-right, stick bottom-left, actions bottom-right. */
    CHECK(name,
          slots[x2::presentation::kSlotVitals].top < viewport.height * 0.5f);
    CHECK(name,
          slots[x2::presentation::kSlotVitals].left < viewport.width * 0.5f);
    CHECK(name, slots[x2::presentation::kSlotPotions].top >
                    slots[x2::presentation::kSlotVitals].top);
    CHECK(name,
          slots[x2::presentation::kSlotPotions].left < viewport.width * 0.5f);
    CHECK(name, slots[x2::presentation::kSlotPortraits].right >
                    viewport.width * 0.5f);
    CHECK(name,
          slots[x2::presentation::kSlotPortraits].top < viewport.height * 0.5f);
    CHECK(name,
          slots[x2::presentation::kSlotStick].left < viewport.width * 0.5f);
    CHECK(name,
          slots[x2::presentation::kSlotStick].bottom > viewport.height * 0.5f);
    {
      /* The combat diamond, named one by one rather than swept over a range:
         a range would also include the left-thumb modifier when slots change.
       */
      const int cluster[] = {(int)x2::presentation::kSlotLightAttack,
                             (int)x2::presentation::kSlotHeavyAttack,
                             (int)x2::presentation::kSlotUse,
                             (int)x2::presentation::kSlotJump};
      for (i = 0; i < (int)(sizeof cluster / sizeof cluster[0]); i++) {
        const int slot = cluster[i];
        CHECK(x2::presentation::layout_slot_name(slot),
              slots[slot].right > viewport.width * 0.5f);
        CHECK(x2::presentation::layout_slot_name(slot),
              slots[slot].bottom > viewport.height * 0.5f);
      }
    }
    /* The powers belong to the right thumb with the abilities they chord:
       inboard of the diamond, never under the left thumb's stick. */
    for (i = (int)x2::presentation::kSlotPower1;
         i <= (int)x2::presentation::kSlotPower4; ++i) {
      CHECK(x2::presentation::layout_slot_name(i),
            slots[i].left > slots[x2::presentation::kSlotStick].right);
      CHECK(x2::presentation::layout_slot_name(i),
            slots[i].right <= slots[x2::presentation::kSlotHeavyAttack].left);
      CHECK(x2::presentation::layout_slot_name(i),
            slots[i].top > slots[x2::presentation::kSlotPortraits].bottom);
    }
    /* The stick's reach holds the ring, stays left of the centreline and of
       every action and power, below the potions, inside the safe region. */
    {
      const x2::presentation::Rect reach = layout_stick_reach(viewport, slots);
      const x2::presentation::Rect ring = slots[x2::presentation::kSlotStick];
      CHECK("stick reach holds the ring",
            reach.left <= ring.left && reach.top <= ring.top &&
                reach.right >= ring.right && reach.bottom >= ring.bottom);
      CHECK("stick reach is larger than the ring", area(reach) > area(ring));
      CHECK("stick reach is inside the safe region",
            reach.left >= viewport.safe_left - 0.5f &&
                reach.bottom <= viewport.height - viewport.safe_bottom + 0.5f);
      CHECK("stick reach is below the potions",
            reach.top >= slots[x2::presentation::kSlotPotions].bottom - 0.5f);
      for (i = (int)x2::presentation::kSlotLightAttack;
           i <= (int)x2::presentation::kSlotPower4; ++i)
        CHECK(x2::presentation::layout_slot_name(i),
              !layout_rects_overlap(reach, slots[i]));
    }
    /* The port menu waits in the top band just right of the centreline,
       where the game's own pause and team icons sit either side of it, and
       at their size: not an action button's. */
    {
      const x2::presentation::Rect menu =
          slots[x2::presentation::kSlotPortMenu];
      const float centre =
          (viewport.safe_left + viewport.width - viewport.safe_right) * 0.5f;
      CHECK(name, menu.bottom < viewport.height * 0.25f);
      CHECK(name, menu.left > centre);
      CHECK(name, menu.right - menu.left <
                      slots[x2::presentation::kSlotLightAttack].right -
                          slots[x2::presentation::kSlotLightAttack].left);
    }
    /* Every round button is one size: the actions are not larger than the
       powers beside them. */
    for (i = (int)x2::presentation::kSlotHeavyAttack;
         i <= (int)x2::presentation::kSlotPower4; ++i)
      CHECK("every action and power is one size",
            fabsf((slots[i].right - slots[i].left) -
                  (slots[x2::presentation::kSlotLightAttack].right -
                   slots[x2::presentation::kSlotLightAttack].left)) < 0.01f);
    /* The stick and the action cluster must not be reachable by one hand
       only because they are close: they belong to opposite thumbs. */
    CHECK(name, slots[x2::presentation::kSlotStick].right <
                    slots[x2::presentation::kSlotJump].left);
    /* A resting left thumb can move while the right thumb jumps. */
    CHECK(name, slots[x2::presentation::kSlotJump].right <=
                    slots[x2::presentation::kSlotLightAttack].left);
    for (i = (int)x2::presentation::kSlotLightAttack;
         i <= (int)x2::presentation::kSlotPower4; ++i)
      CHECK("face/power target at least 48 output pixels on tested devices",
            slots[i].right - slots[i].left >= 48.0f);
  }

  /* The controls clear the HUD at every size the player can choose. */
  for (v = 0; v < sizeof kViewports / sizeof kViewports[0]; v++) {
    x2::presentation::LayoutViewport viewport = {
        kViewports[v].width,      kViewports[v].height,
        kViewports[v].safe_left,  kViewports[v].safe_top,
        kViewports[v].safe_right, kViewports[v].safe_bottom};
    const char *name = kViewports[v].name;
    unsigned s;
    for (s = 0; s < sizeof kHudScales / sizeof kHudScales[0]; s++) {
      x2::presentation::HudPlacement hud;
      x2::presentation::Rect slots[x2::presentation::kSlotCount];
      x2::presentation::Rect bands[x2::presentation::kHudPotions + 5];
      int i, j;
      unsigned n = 0;

      if (!hud_here(&viewport, kHudScales[s], &hud) ||
          !layout_build(viewport, &hud, slots)) {
        CHECK(name, 0);
        continue;
      }
      bands[n++] = hud.vitals;
      for (i = 0; i < x2::presentation::kHudPotions; ++i)
        bands[n++] = hud.potions[i];
      for (i = 0; i < 4; ++i)
        bands[n++] = hud.portraits[i];
      for (i = (int)x2::presentation::kSlotStick;
           i < (int)x2::presentation::kSlotCount; i++)
        for (j = 0; j < (int)n; j++)
          CHECK(name, !layout_rects_overlap(slots[i], bands[j]));
      {
        const x2::presentation::Rect reach =
            layout_stick_reach(viewport, slots);
        for (i = 0; i < x2::presentation::kHudPotions; ++i)
          CHECK("stick reach clears a potion ring",
                !layout_rects_overlap(reach, hud.potions[i]));
      }
    }
  }

  /* No placement published: the HUD slots stay empty, the controls remain. */
  {
    x2::presentation::LayoutViewport viewport = {1280.0f, 720.0f, 0.0f,
                                                 0.0f,    0.0f,   0.0f};
    x2::presentation::HudPlacement hud;
    x2::presentation::Rect none[x2::presentation::kSlotCount],
        placed[x2::presentation::kSlotCount];
    CHECK("placement for the comparison", hud_here(&viewport, 100, &hud));
    CHECK("layout without a placement", layout_build(viewport, NULL, none));
    CHECK("layout with the placement", layout_build(viewport, &hud, placed));
    CHECK("no placement invents no HUD rectangle",
          area(none[(int)x2::presentation::kSlotVitals]) == 0.0f &&
              area(none[(int)x2::presentation::kSlotPotions]) == 0.0f &&
              area(none[(int)x2::presentation::kSlotPortraits]) == 0.0f);
    CHECK("no placement still places every control",
          area(none[(int)x2::presentation::kSlotStick]) > 0.0f &&
              area(none[(int)x2::presentation::kSlotLightAttack]) > 0.0f);
    {
      int i;
      for (i = (int)x2::presentation::kSlotStick;
           i < (int)x2::presentation::kSlotCount; i++)
        CHECK("the HUD only takes room, it never gives any back",
              (none[i].right - none[i].left) >=
                  (placed[i].right - placed[i].left) - 0.01f);
    }
  }

  /* The menu pad, over the same sweep: inside the safe region, no button on
     another, the d-pad under the left thumb and the face buttons under the
     right in the Xbox arrangement, each shoulder above its own cluster, and
     every button at least a thumb wide. */
  for (v = 0; v < sizeof kViewports / sizeof kViewports[0]; v++) {
    x2::presentation::LayoutViewport viewport = {
        kViewports[v].width,      kViewports[v].height,
        kViewports[v].safe_left,  kViewports[v].safe_top,
        kViewports[v].safe_right, kViewports[v].safe_bottom};
    x2::presentation::Rect menu[x2::presentation::kMenuSlotCount];
    const char *name = kViewports[v].name;
    const float middle =
        (viewport.safe_left + viewport.width - viewport.safe_right) * 0.5f;
    int i, j;

    CHECK(name, layout_build_menu(viewport, menu));
    for (i = 0; i < (int)x2::presentation::kMenuSlotCount; i++) {
      const x2::presentation::Rect r = menu[i];
      CHECK(x2::presentation::menu_slot_name(i),
            r.left >= viewport.safe_left - 0.5f);
      CHECK(x2::presentation::menu_slot_name(i),
            r.top >= viewport.safe_top - 0.5f);
      CHECK(x2::presentation::menu_slot_name(i),
            r.right <= viewport.width - viewport.safe_right + 0.5f);
      CHECK(x2::presentation::menu_slot_name(i),
            r.bottom <= viewport.height - viewport.safe_bottom + 0.5f);
      CHECK(x2::presentation::menu_slot_name(i), r.right - r.left >= 48.0f);
      for (j = i + 1; j < (int)x2::presentation::kMenuSlotCount; j++)
        CHECK(name, !layout_rects_overlap(menu[i], menu[j]));
    }
    for (i = (int)x2::presentation::kMenuDpadUp;
         i <= (int)x2::presentation::kMenuDpadRight; i++)
      CHECK(x2::presentation::menu_slot_name(i), menu[i].right <= middle);
    for (i = (int)x2::presentation::kMenuA; i <= (int)x2::presentation::kMenuY;
         i++)
      CHECK(x2::presentation::menu_slot_name(i), menu[i].left >= middle);
    CHECK("up above down", menu[x2::presentation::kMenuDpadUp].bottom <=
                               menu[x2::presentation::kMenuDpadDown].top);
    CHECK("left of right", menu[x2::presentation::kMenuDpadLeft].right <=
                               menu[x2::presentation::kMenuDpadRight].left);
    CHECK("A below Y", menu[x2::presentation::kMenuY].bottom <=
                           menu[x2::presentation::kMenuA].top);
    CHECK("X left of B", menu[x2::presentation::kMenuX].right <=
                             menu[x2::presentation::kMenuB].left);
    CHECK("left shoulder above the d-pad",
          menu[x2::presentation::kMenuLeftShoulder].bottom <=
                  menu[x2::presentation::kMenuDpadUp].top &&
              menu[x2::presentation::kMenuLeftShoulder].right <= middle);
    CHECK("right shoulder above the face buttons",
          menu[x2::presentation::kMenuRightShoulder].bottom <=
                  menu[x2::presentation::kMenuY].top &&
              menu[x2::presentation::kMenuRightShoulder].left >= middle);
  }
  {
    x2::presentation::Rect menu[x2::presentation::kMenuSlotCount];
    x2::presentation::LayoutViewport inverted = {100.0f, 100.0f, 80.0f,
                                                 0,      80.0f,  0};
    x2::presentation::LayoutViewport ok = {800.0f, 600.0f, 0, 0, 0, 0};
    int i;
    CHECK("menu pad: no usable area", !layout_build_menu(inverted, menu));
    CHECK("menu pad: null destination", !layout_build_menu(ok, NULL));
    for (i = 0; i < (int)x2::presentation::kMenuSlotCount; i++)
      CHECK("menu slot name", x2::presentation::menu_slot_name(i)[0] != '\0');
    CHECK("menu slot out of range",
          x2::presentation::menu_slot_name(
              (int)x2::presentation::kMenuSlotCount)[0] != '\0');
  }

  /* Refusals. A viewport with no usable area has no layout, and saying so is
     different from returning eight empty rectangles. */
  {
    x2::presentation::Rect slots[x2::presentation::kSlotCount];
    x2::presentation::LayoutViewport empty = {0};
    x2::presentation::LayoutViewport inverted = {100.0f, 100.0f, 80.0f,
                                                 0,      80.0f,  0};
    x2::presentation::LayoutViewport nan_size = {NAN, 100.0f, 0, 0, 0, 0};
    x2::presentation::LayoutViewport ok = {800.0f, 600.0f, 0, 0, 0, 0};
    CHECK("empty viewport", !layout_build(empty, NULL, slots));
    CHECK("safe area wider than screen", !layout_build(inverted, NULL, slots));
    CHECK("non-finite dimension", !layout_build(nan_size, NULL, slots));
    CHECK("null destination", !layout_build(ok, NULL, NULL));
  }

  /* The names are the denominator of every exhaustive check above. */
  {
    int i;
    for (i = 0; i < (int)x2::presentation::kSlotCount; i++)
      CHECK("slot name", x2::presentation::layout_slot_name(i)[0] != '\0');
    CHECK("out-of-range name",
          x2::presentation::layout_slot_name(
              (int)x2::presentation::kSlotCount)[0] != '\0');
    CHECK("hud slots", x2::presentation::layout_slot_is_hud(
                           (int)x2::presentation::kSlotVitals) &&
                           x2::presentation::layout_slot_is_hud(
                               (int)x2::presentation::kSlotPortraits) &&
                           !x2::presentation::layout_slot_is_hud(
                               (int)x2::presentation::kSlotStick));
  }

  printf("%d check(s), %d failure(s)\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
