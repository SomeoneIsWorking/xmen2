/* Native CHud presentation adapters. Retail submits sprite/text records and
   scene transforms here; rasterization occurs later. See docs/RE/hud.md. */
#include "hud_draw_runtime.h"
#include "../input/touch_runtime.h"
#include "../presentation/aspect_fit.h"
#include "../presentation/hud_geometry.h"
#include "../presentation/hud_layout.h"
#include "guest_body.h"
#include "hud_portrait_position.h"
#include "settings_store.h"
#include "x86rt_native.h"

#include <lucent/cvar_c.h>
#include <lucent/log_c.h>
#include <math.h>
#include <string.h>

enum {
  VIEWPORT = 0x00a0a138u,
  PORTRAIT_CENTERS = 0x00a0a0ccu,
  POTION_CENTER = 0x00a0a0bcu,
  PARTY_DRAW = 0x005a43d0u,
  VITALS_DRAW = 0x005a3320u,
  INVENTORY_DRAW = 0x005a5170u,
  PORTRAIT_DRAW = 0x005a1ab0u,
  SPRITE_SUBMIT = 0x0059a140u,
  TEXT_SUBMIT = 0x005f11b0u,
  SCENE_MATRIX = 0x00570970u,
  MOUSE_OVERLAY_DRAW = 0x005fc100u,
  MENU_ICON_SIZE = 0x00683ffcu
};

typedef struct {
  int active;
  unsigned group;
  uint32_t portrait;
  int position_seen;
  X2HudTransform transform;
} HudScope;

static HudScope g_scope;
static X2LayoutViewport g_viewport;
static X2HudSpace g_space, g_retail_space;
static X2HudPlacement g_layout;
/* What was drawn since the last publication, published whole each frame. */
static X2HudRegions g_regions;
static unsigned long g_groups[4], g_total[4], g_sprites, g_texts, g_matrices;
static unsigned long g_sprite_calls, g_text_calls, g_matrix_calls;
static unsigned g_trace_mask;

static void read_floats(uint32_t address, float *out, unsigned count) {
  for (unsigned i = 0; i < count; ++i)
    out[i] = (float)RDF32(address + i * 4u);
}
static void write_floats(uint32_t address, const float *values,
                         unsigned count) {
  for (unsigned i = 0; i < count; ++i)
    WRF32(address + i * 4u, values[i]);
}

/* The HUD space mapped onto the output: the one mapping every published
   region and relocated group goes through. */
static int prepare_space(void) {
  if (!x2_touch_runtime_viewport(&g_viewport))
    return 0;
  g_retail_space = x2_hud_space((float)RDF32(VIEWPORT + 0x10u),
                                (float)RDF32(VIEWPORT + 0x48u),
                                (float)RDF32(VIEWPORT + 0x4cu));
  const X2Settings *settings = x2_settings_store();
  X2AspectRect frame;
  if (!x2_aspect_fit((uint32_t)g_viewport.width, (uint32_t)g_viewport.height,
                     settings->width, settings->height, &frame))
    return 0;
  g_space = g_retail_space;
  float pixel_x = g_space.width / (float)frame.width;
  float pixel_z = g_space.height / (float)frame.height;
  g_space.left -= (float)frame.x * pixel_x;
  g_space.top += (float)frame.y * pixel_z;
  g_space.width = g_viewport.width * pixel_x;
  g_space.height = g_viewport.height * pixel_z;
  g_viewport.safe_left = fmaxf(g_viewport.safe_left, (float)frame.x);
  g_viewport.safe_top = fmaxf(g_viewport.safe_top, (float)frame.y);
  g_viewport.safe_right = fmaxf(
      g_viewport.safe_right, g_viewport.width - (float)(frame.x + frame.width));
  g_viewport.safe_bottom =
      fmaxf(g_viewport.safe_bottom,
            g_viewport.height - (float)(frame.y + frame.height));
  return 1;
}

/* That mapping, plus the mobile layout when it is the one in use. */
static int prepare(void) {
  const X2Settings *settings = x2_settings_store();
  return prepare_space() &&
         x2_hud_layout_mobile(&settings->hud, x2_touch_runtime_active()) &&
         x2_hud_layout_build(g_viewport, &settings->hud, &g_layout);
}

static void placement(unsigned group, X2HudSpace source, X2Rect target) {
  g_scope.active = 1;
  g_scope.group = group;
  g_scope.transform =
      x2_hud_fit(g_space, g_viewport.width, g_viewport.height, source, target);

  if (lucent_cvar_flag("hud.trace", 0) && !(g_trace_mask & (1u << group))) {
    g_trace_mask |= 1u << group;
    lucent_log_info("hud", "group %u source=(%g,%g %gx%g) affine=(%g,%g,%g)",
                    group, (double)source.left, (double)source.top,
                    (double)source.width, (double)source.height,
                    (double)g_scope.transform.scale,
                    (double)g_scope.transform.x, (double)g_scope.transform.z);
  }
}

static void capture_portrait(void *context, uint32_t portrait, float xyz[3]) {
  (void)context;
  if (g_scope.portrait != portrait)
    return;
  g_scope.position_seen = 1;
  unsigned slot = RD8(portrait + 0x4cu);
  if (slot >= 4)
    return;
  if (g_scope.active) {
    placement(3, (X2HudSpace){xyz[0] - 40.0f, xyz[2] + 40.0f, 80.0f, 80.0f},
              g_layout.portraits[slot]);
  }
}

void x2_hud_party_draw(CPU *cpu) {
  HudScope saved = g_scope;
  g_scope = (HudScope){0};
  ++g_total[0];
  x2_touch_runtime_hud_regions(&g_regions);
  g_regions.portrait_mask = 0;
  g_regions.potion_mask = 0;
  g_regions.menu_icon_mask = 0;
  if (prepare()) {
    uint32_t self = cpu->reg[kX86pEcx];
    placement(0,
              (X2HudSpace){(float)RDF32(self + 8u) - 24.0f,
                           (float)RDF32(self + 16u) + 24.0f, 48.0f, 48.0f},
              g_layout.selector);
  }
  g_groups[0] += (unsigned)g_scope.active;
  x86_guest_body(cpu, "XMen2.exe", PARTY_DRAW);
  g_scope = saved;
}

static void vitals_draw(CPU *cpu) {
  HudScope saved = g_scope;
  g_scope = (HudScope){0};
  if (prepare()) {
    uint32_t pos = RD32(cpu->reg[kX86pEsp] + 8u);
    float xyz[3];
    read_floats(pos, xyz, 3);
    float left = xyz[0] >= 256.0f ? xyz[0] - 131.0f : xyz[0];
    placement(1, (X2HudSpace){left, xyz[2] + 11.8f, 131.0f, 24.0f},
              g_layout.vitals);
  }
  ++g_total[1];
  g_groups[1] += (unsigned)g_scope.active;
  x86_guest_body(cpu, "XMen2.exe", VITALS_DRAW);
  g_scope = saved;
}

/*
 * CHud stacks the potions: a 20x20 icon with its top at Z=202 (health) or
 * Z=182 (energy), and its count as text to the icon's right, at Z=192 or
 * Z=172. Each potion goes into its own ring, the icon filling it and the
 * count as a badge at its lower right, so the two are separate touch buttons.
 * Every element is fitted from its OWN source coordinates, so nothing here
 * assumes where the retail viewport put the column.
 */
enum { POTION_ENERGY_ICON_TOP = 182, POTION_ENERGY_COUNT_Z = 172 };

static X2HudTransform potion_fit(X2HudSpace source, X2Rect target) {
  return x2_hud_fit(g_space, g_viewport.width, g_viewport.height, source,
                    target);
}

static unsigned potion_of_icon(float top) {
  return top > (float)POTION_ENERGY_ICON_TOP + 0.5f ? X2_HUD_POTION_HEALTH
                                                    : X2_HUD_POTION_ENERGY;
}

static unsigned potion_of_count(float z) {
  return z > (float)POTION_ENERGY_COUNT_Z + 0.5f ? X2_HUD_POTION_HEALTH
                                                 : X2_HUD_POTION_ENERGY;
}

static void inventory_draw(CPU *cpu) {
  HudScope saved = g_scope;
  g_scope = (HudScope){0};
  if (prepare()) {
    /* The whole column into both rings: what anything that is not an icon
       or a count (a scene matrix) is placed by. */
    const float left = g_retail_space.left +
                       g_retail_space.width * (float)RDF32(VIEWPORT + 0x58u);
    const X2Rect rings = {g_layout.potions[0].left, g_layout.potions[0].top,
                          g_layout.potions[X2_HUD_POTIONS - 1].right,
                          g_layout.potions[X2_HUD_POTIONS - 1].bottom};
    placement(2, (X2HudSpace){left, 202.0f, 60.0f, 40.0f}, rings);
  }
  ++g_total[2];
  g_groups[2] += (unsigned)g_scope.active;
  x86_guest_body(cpu, "XMen2.exe", INVENTORY_DRAW);
  if (g_scope.active) {
    float xyz[3];
    const uint32_t centers[] = {POTION_CENTER, 0x00a0a118u};
    for (unsigned i = 0; i < X2_HUD_POTIONS; ++i) {
      read_floats(centers[i], xyz, 3);
      x2_hud_transform_point(
          potion_fit((X2HudSpace){xyz[0] - 10.0f, xyz[2] + 10.0f, 20.0f, 20.0f},
                     x2_hud_potion_icon(g_layout.potions[i])),
          xyz);
      write_floats(centers[i], xyz, 3);
      g_regions.potions[i] = g_layout.potions[i];
    }
    g_regions.potion_mask = (1u << X2_HUD_POTIONS) - 1u;
  }
  g_scope = saved;
}

/*
 * The game's mouse overlay (005fc100) draws a pause menu icon and a team
 * menu icon, 32 pixels each, at the top centre and leaves their hit boxes for
 * its click handler (005f9eb0): each a left edge and a top in pointer space, S
 * = [00683ffc] units square, the left icon at 00a0a10c/00a0a114 and the right
 * at 00a0a124/00a0a12c. Pointer space is the HUD space the portrait and potion
 * centres use (Z up), so each icon is published through the same mapping to
 * output pixels, and a tap at its centre is the click the retail handler
 * already acts on.
 */
enum { MENU_ICON_LEFT = 0x00a0a10cu, MENU_ICON_RIGHT = 0x00a0a124u };

static X2Rect menu_icon_region(uint32_t corner, float size) {
  const float half = size * 0.5f;
  return x2_hud_output_rect(g_space, g_viewport.width, g_viewport.height,
                            (float)RDF32(corner) + half,
                            (float)RDF32(corner + 8u) - half, half);
}

static void mouse_overlay_draw(CPU *cpu) {
  x86_guest_body(cpu, "XMen2.exe", MOUSE_OVERLAY_DRAW);
  /* Zero until the presenter has placed them: not drawn, nothing to tap. */
  if (RDF32(MENU_ICON_RIGHT) == 0.0 || !prepare_space())
    return;
  const float size = (float)RDF32(MENU_ICON_SIZE);
  g_regions.menu_icons[0] = menu_icon_region(MENU_ICON_LEFT, size);
  g_regions.menu_icons[1] = menu_icon_region(MENU_ICON_RIGHT, size);
  g_regions.menu_icon_mask = (1u << X2_HUD_MENU_ICONS) - 1u;
}

static void portrait_draw(CPU *cpu) {
  HudScope saved = g_scope;
  uint32_t portrait = cpu->reg[kX86pEcx];
  unsigned slot = RD8(portrait + 0x4cu);
  g_scope = (HudScope){0};
  g_scope.active = prepare();
  g_scope.portrait = portrait;
  ++g_total[3];
  x86_guest_body(cpu, "XMen2.exe", PORTRAIT_DRAW);
  g_groups[3] += (unsigned)(g_scope.active && g_scope.position_seen);
  if (slot < 4 && g_scope.position_seen && g_viewport.width > 0) {
    float xyz[3];
    uint32_t address = PORTRAIT_CENTERS + slot * 12u;
    read_floats(address, xyz, 3);
    if (g_scope.active) {
      x2_hud_transform_point(g_scope.transform, xyz);
      write_floats(address, xyz, 3);
    }
    if (RD8(portrait + 0x18u)) {
      g_regions.portraits[slot] =
          g_scope.active
              ? g_layout.portraits[slot]
              : x2_hud_output_rect(g_space, g_viewport.width, g_viewport.height,
                                   xyz[0], xyz[2], 20.0f);
      g_regions.portrait_mask |= 1u << slot;
    } else {
      g_regions.portrait_mask &= ~(1u << slot);
    }
  }
  g_scope = saved;
}

static void sprite_submit(CPU *cpu) {
  ++g_sprite_calls;
  if (!g_scope.active) {
    x86_guest_body(cpu, "XMen2.exe", SPRITE_SUBMIT);
    return;
  }
  uint32_t position = RD32(cpu->reg[kX86pEsp] + 4u);
  uint32_t size = RD32(cpu->reg[kX86pEsp] + 8u);
  float old_position[3], old_size[2], point[3], dimensions[2];
  read_floats(position, old_position, 3);
  read_floats(size, old_size, 2);
  memcpy(point, old_position, sizeof point);
  const X2HudTransform transform =
      g_scope.group == 2
          ? potion_fit((X2HudSpace){old_position[0], old_position[2],
                                    old_size[0], old_size[1]},
                       x2_hud_potion_icon(
                           g_layout.potions[potion_of_icon(old_position[2])]))
          : g_scope.transform;
  x2_hud_transform_point(transform, point);
  for (unsigned i = 0; i < 2; ++i)
    dimensions[i] = old_size[i] * transform.scale;
  write_floats(position, point, 3);
  write_floats(size, dimensions, 2);
  /* Retail spins the energy icon (rotation = game time + 1, 005a5838) and
     holds health at 0. In a ring each potion is a button, so both stand
     still. The argument is by value, so the caller never reads it back. */
  if (g_scope.group == 2)
    WRF32(cpu->reg[kX86pEsp] + 16u, 0.0f);
  x86_guest_body(cpu, "XMen2.exe", SPRITE_SUBMIT);
  write_floats(position, old_position, 3);
  write_floats(size, old_size, 2);
  ++g_sprites;
}

static void text_submit(CPU *cpu) {
  ++g_text_calls;
  if (!g_scope.active) {
    x86_guest_body(cpu, "XMen2.exe", TEXT_SUBMIT);
    return;
  }
  uint32_t args = cpu->reg[kX86pEsp] + 8u;
  uint32_t original[5];
  for (unsigned i = 0; i < 5; ++i)
    original[i] = RD32(args + i * 4u);
  float xyz[3] = {(float)(int32_t)original[0], 0, (float)(int32_t)original[1]};
  /* A potion's count: its own 16-unit line, fitted into the ring's badge. */
  const X2HudTransform transform =
      g_scope.group == 2
          ? potion_fit(
                (X2HudSpace){xyz[0], xyz[2], 16.0f, 16.0f},
                x2_hud_potion_count(g_layout.potions[potion_of_count(xyz[2])]))
          : g_scope.transform;
  x2_hud_transform_point(transform, xyz);
  WR32(args, (uint32_t)(int32_t)lroundf(xyz[0]));
  WR32(args + 4u, (uint32_t)(int32_t)lroundf(xyz[2]));
  for (unsigned i = 2; i < 4; ++i)
    WR32(args + i * 4u, (uint32_t)(int32_t)lroundf((float)(int32_t)original[i] *
                                                   transform.scale));
  WRF32(args + 16u, (float)RDF32(args + 16u) * transform.scale);
  x86_guest_body(cpu, "XMen2.exe", TEXT_SUBMIT);
  for (unsigned i = 0; i < 5; ++i)
    WR32(args + i * 4u, original[i]);
  ++g_texts;
}

static void scene_matrix(CPU *cpu) {
  ++g_matrix_calls;
  if (!g_scope.active) {
    x86_guest_body(cpu, "XMen2.exe", SCENE_MATRIX);
    return;
  }
  uint32_t address = RD32(cpu->reg[kX86pEsp] + 4u);
  float original[16], matrix[16];
  read_floats(address, original, 16);
  memcpy(matrix, original, sizeof matrix);
  x2_hud_transform_matrix(g_scope.transform, matrix);
  write_floats(address, matrix, 16);
  x86_guest_body(cpu, "XMen2.exe", SCENE_MATRIX);
  write_floats(address, original, 16);
  ++g_matrices;
}

void x2_hud_draw_report(void) {
  lucent_log_info("hud",
                  "mobile groups party %lu/%lu, vitals %lu/%lu, inventory "
                  "%lu/%lu, portraits %lu/%lu; transformed sprite %lu/%lu, "
                  "text %lu/%lu, scene %lu/%lu submissions",
                  g_groups[0], g_total[0], g_groups[1], g_total[1], g_groups[2],
                  g_total[2], g_groups[3], g_total[3], g_sprites,
                  g_sprite_calls, g_texts, g_text_calls, g_matrices,
                  g_matrix_calls);
  x2_hud_portrait_position_report();
}

__attribute__((constructor)) static void register_hud(void) {
  x86_register_override("XMen2.exe", VITALS_DRAW, vitals_draw);
  x86_register_override("XMen2.exe", INVENTORY_DRAW, inventory_draw);
  x86_register_override("XMen2.exe", PORTRAIT_DRAW, portrait_draw);
  x86_register_override("XMen2.exe", SPRITE_SUBMIT, sprite_submit);
  x86_register_override("XMen2.exe", TEXT_SUBMIT, text_submit);
  x86_register_override("XMen2.exe", SCENE_MATRIX, scene_matrix);
  x86_register_override("XMen2.exe", MOUSE_OVERLAY_DRAW, mouse_overlay_draw);
  x2_hud_portrait_position_mapper(capture_portrait, NULL);
}
