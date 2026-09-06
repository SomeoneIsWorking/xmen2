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
                             NULL));
  X2HudPlacement placement;
  CHECK(!x2_hud_layout_build((X2LayoutViewport){1280, 720, 0, 0, 0, 0}, NULL,
                             &placement));

  for (unsigned v = 0; v < sizeof(viewports) / sizeof(viewports[0]); ++v) {
    X2LayoutViewport vp = {viewports[v].width,      viewports[v].height,
                           viewports[v].safe_left,  viewports[v].safe_top,
                           viewports[v].safe_right, viewports[v].safe_bottom};

    CHECK(x2_hud_layout_build(vp, &settings, &placement));

    /* Vitals in top-left */
    CHECK(placement.vitals.left >= vp.safe_left);
    CHECK(placement.vitals.top >= vp.safe_top);
    CHECK(placement.vitals.right > placement.vitals.left);
    CHECK(placement.vitals.bottom > placement.vitals.top);

    /* Potions directly below vitals */
    CHECK(placement.potions.left >= vp.safe_left);
    CHECK(placement.potions.top >= placement.vitals.bottom);
    CHECK(placement.potions.right > placement.potions.left);
    CHECK(placement.potions.bottom > placement.potions.top);

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
  CHECK(x2_hud_layout_build(vp, &base, &p_base));

  /* Scale vitals */
  scaled = base;
  scaled.vitals_scale_percent = 150;
  CHECK(x2_hud_layout_build(vp, &scaled, &p_scaled));
  CHECK(p_scaled.vitals.right - p_scaled.vitals.left >
        p_base.vitals.right - p_base.vitals.left);

  /* Scale potions */
  scaled = base;
  scaled.potions_scale_percent = 150;
  CHECK(x2_hud_layout_build(vp, &scaled, &p_scaled));
  CHECK(p_scaled.potions.right - p_scaled.potions.left >
        p_base.potions.right - p_base.potions.left);

  /* Scale portraits */
  scaled = base;
  scaled.portraits_scale_percent = 150;
  CHECK(x2_hud_layout_build(vp, &scaled, &p_scaled));
  CHECK(p_scaled.portraits[0].right - p_scaled.portraits[0].left >
        p_base.portraits[0].right - p_base.portraits[0].left);

  /* Safe inset expands margins */
  scaled = base;
  scaled.safe_inset_percent = 5;
  CHECK(x2_hud_layout_build(vp, &scaled, &p_scaled));
  CHECK(p_scaled.vitals.left > p_base.vitals.left);
  CHECK(p_scaled.vitals.top > p_base.vitals.top);
  CHECK(p_scaled.portraits[3].right < p_base.portraits[3].right);
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
  check_transforms();
  printf("test_hud_layout: %d checks passed\n", g_checks);
  return 0;
}
