#include "hud_layout.h"
#include "hud_settings.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static int g_checks;
#define CHECK(cond)                                                            \
  do {                                                                         \
    g_checks++;                                                                \
    assert(cond);                                                              \
  } while (0)

static void check_layout_mode(void) {
  X2HudSettings settings;
  x2_hud_settings_defaults(&settings);

  settings.layout = X2_HUD_LAYOUT_AUTO;
  CHECK(x2_hud_layout_mobile(&settings, 1));
  CHECK(!x2_hud_layout_mobile(&settings, 0));

  settings.layout = X2_HUD_LAYOUT_MOBILE;
  CHECK(x2_hud_layout_mobile(&settings, 0));
  CHECK(x2_hud_layout_mobile(&settings, 1));

  settings.layout = X2_HUD_LAYOUT_RETAIL;
  CHECK(!x2_hud_layout_mobile(&settings, 0));
  CHECK(!x2_hud_layout_mobile(&settings, 1));
}

static void check_hud_layout_shapes(void) {
  static const struct {
    float width, height;
    float safe_left, safe_top, safe_right, safe_bottom;
  } viewports[] = {
      {1280, 720, 0, 0, 0, 0},      {1920, 1080, 0, 0, 0, 0},
      {2560, 1080, 20, 0, 20, 0},   /* 21:9 ultrawide */
      {1024, 768, 0, 0, 0, 0},      /* 4:3 */
      {1080, 2400, 30, 20, 30, 20}, /* phone with cutout */
      {1000, 1000, 0, 0, 0, 0},     /* square */
  };

  X2HudSettings settings;
  x2_hud_settings_defaults(&settings);

  /* Rejects invalid parameters */
  CHECK(!x2_hud_layout_build((X2LayoutViewport){1280, 720, 0, 0, 0, 0}, NULL,
                             -1.0f, NULL));
  X2HudPlacement placement;
  CHECK(!x2_hud_layout_build((X2LayoutViewport){1280, 720, 0, 0, 0, 0}, NULL,
                             -1.0f, &placement));

  for (unsigned v = 0; v < sizeof(viewports) / sizeof(viewports[0]); ++v) {
    X2LayoutViewport vp = {viewports[v].width,      viewports[v].height,
                           viewports[v].safe_left,  viewports[v].safe_top,
                           viewports[v].safe_right, viewports[v].safe_bottom};

    CHECK(x2_hud_layout_build(vp, &settings, -1.0f, &placement));

    /* Vitals in top-left */
    CHECK(placement.vitals.left >= vp.safe_left);
    CHECK(placement.vitals.top >= vp.safe_top);
    CHECK(placement.vitals.right > placement.vitals.left);
    CHECK(placement.vitals.bottom > placement.vitals.top);

    /* Potions: two round buttons side by side directly below vitals, each
       holding the retail icon and its count inside the ring. */
    for (unsigned i = 0; i < X2_HUD_POTIONS; ++i) {
      X2Rect ring = placement.potions[i];
      X2Rect icon = x2_hud_potion_icon(ring);
      X2Rect count = x2_hud_potion_count(ring);
      CHECK(ring.left >= vp.safe_left);
      CHECK(ring.top >= placement.vitals.bottom);
      CHECK(ring.right > ring.left);
      CHECK(fabsf((ring.right - ring.left) - (ring.bottom - ring.top)) < 0.01f);
      CHECK(icon.left > ring.left && icon.right < ring.right);
      CHECK(icon.top > ring.top && icon.bottom < ring.bottom);
      CHECK(count.left > ring.left && count.right <= ring.right);
      CHECK(count.top > ring.top && count.bottom <= ring.bottom);
    }
    CHECK(placement.potions[X2_HUD_POTION_ENERGY].left >
          placement.potions[X2_HUD_POTION_HEALTH].right);
    /* ...and stay inside the band the touch layout keeps clear for them,
       so the stick's reach below it never lands on a potion. */
    X2Rect slots[kX2SlotCount];
    CHECK(x2_layout_build(vp, slots));
    CHECK(placement.potions[X2_HUD_POTION_ENERGY].bottom <=
          slots[kX2SlotPotions].bottom);

    /* Portraits in top-right */
    for (unsigned i = 0; i < 4; ++i) {
      CHECK(placement.portraits[i].top >= vp.safe_top);
      CHECK(placement.portraits[i].bottom > placement.portraits[i].top);
      CHECK(placement.portraits[i].right > placement.portraits[i].left);
      if (i > 0)
        CHECK(placement.portraits[i].left >=
              placement.portraits[i - 1].right - 0.01f);
    }
    CHECK(placement.portraits[3].right <= vp.width - vp.safe_right);

    /* No collision between vitals and portraits */
    CHECK(placement.vitals.right < placement.portraits[0].left);

    /* Selector cross is moved offscreen */
    CHECK(placement.selector.right <= 0 || placement.selector.bottom <= 0);
  }
}

static void check_hud_scales(void) {
  X2LayoutViewport vp = {1280, 720, 0, 0, 0, 0};
  X2HudSettings base, scaled;
  X2HudPlacement p_base, p_scaled;

  x2_hud_settings_defaults(&base);
  CHECK(x2_hud_layout_build(vp, &base, -1.0f, &p_base));

  /* Scale vitals */
  scaled = base;
  scaled.vitals_scale_percent = 150;
  CHECK(x2_hud_layout_build(vp, &scaled, -1.0f, &p_scaled));
  CHECK(p_scaled.vitals.right - p_scaled.vitals.left >
        p_base.vitals.right - p_base.vitals.left);

  /* Scale potions */
  scaled = base;
  scaled.potions_scale_percent = 150;
  CHECK(x2_hud_layout_build(vp, &scaled, -1.0f, &p_scaled));
  CHECK(p_scaled.potions[0].right - p_scaled.potions[0].left >
        p_base.potions[0].right - p_base.potions[0].left);

  /* Scale portraits */
  scaled = base;
  scaled.portraits_scale_percent = 150;
  CHECK(x2_hud_layout_build(vp, &scaled, -1.0f, &p_scaled));
  CHECK(p_scaled.portraits[0].right - p_scaled.portraits[0].left >
        p_base.portraits[0].right - p_base.portraits[0].left);

  /* Safe inset expands margins */
  scaled = base;
  scaled.safe_inset_percent = 5;
  CHECK(x2_hud_layout_build(vp, &scaled, -1.0f, &p_scaled));
  CHECK(p_scaled.vitals.left > p_base.vitals.left);
  CHECK(p_scaled.vitals.top > p_base.vitals.top);
  CHECK(p_scaled.portraits[3].right < p_base.portraits[3].right);
}

/* The top band is one line: once the game has drawn its menu-icon row, the
   vitals and portraits start at that row's top even where the safe area's
   top is lower -- a phone that reports a status-bar inset in landscape. */
static void check_menu_row_alignment(void) {
  X2LayoutViewport vp = {2728, 1264, 240, 130, 240, 0};
  X2HudSettings settings;
  X2HudPlacement waiting, aligned;
  x2_hud_settings_defaults(&settings);

  CHECK(x2_hud_layout_build(vp, &settings, -1.0f, &waiting));
  CHECK(waiting.vitals.top > vp.safe_top);
  CHECK(x2_hud_layout_build(vp, &settings, 20.0f, &aligned));
  CHECK(aligned.vitals.top == 20.0f);
  for (unsigned i = 0; i < 4; ++i)
    CHECK(aligned.portraits[i].top == 20.0f);
  /* The potions stay under the vitals, whichever row they follow, and
     nothing moves sideways. */
  CHECK(aligned.potions[0].top > aligned.vitals.bottom);
  CHECK(aligned.potions[0].top < waiting.potions[0].top);
  CHECK(aligned.vitals.left == waiting.vitals.left);
  CHECK(aligned.portraits[3].right == waiting.portraits[3].right);
  /* A non-finite row is no row. */
  CHECK(x2_hud_layout_build(vp, &settings, NAN, &aligned));
  CHECK(aligned.vitals.top == waiting.vitals.top);
}

static void check_transforms(void) {
  X2HudSpace space = x2_hud_space(1.7777778f, 1.0f, 1.0f);
  CHECK(fabsf(space.width - 682.6666f) < 0.1f);
  CHECK(fabsf(space.height - 384.0f) < 0.1f);

  /* Transform point and matrix */
  X2HudTransform t = {2.0f, 10.0f, -5.0f};
  float pt[3] = {1.0f, 50.0f, 2.0f};
  x2_hud_transform_point(t, pt);
  CHECK(pt[0] == 1.0f * 2.0f + 10.0f);
  CHECK(pt[1] == 50.0f); /* Y (depth) untouched */
  CHECK(pt[2] == 2.0f * 2.0f - 5.0f);

  float mat[16] = {
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 4, 5, 6, 1,
  };
  x2_hud_transform_matrix(t, mat);
  CHECK(mat[0] == 2.0f);
  CHECK(mat[5] == 1.0f);
  CHECK(mat[10] == 2.0f);
  CHECK(mat[12] == 4.0f * 2.0f + 10.0f);
  CHECK(mat[13] == 5.0f);
  CHECK(mat[14] == 6.0f * 2.0f - 5.0f);

  /* Output rectangle mapping */
  X2Rect rect =
      x2_hud_output_rect(space, 1280.0f, 720.0f, 256.0f, 192.0f, 20.0f);
  /* Center is at (256, 192), which is screen center (640, 360) */
  float cx = (rect.left + rect.right) * 0.5f;
  float cy = (rect.top + rect.bottom) * 0.5f;
  CHECK(fabsf(cx - 640.0f) < 0.5f);
  CHECK(fabsf(cy - 360.0f) < 0.5f);
}

int main(void) {
  check_layout_mode();
  check_hud_layout_shapes();
  check_hud_scales();
  check_menu_row_alignment();
  check_transforms();
  printf("test_hud_layout: %d checks passed\n", g_checks);
  return 0;
}
