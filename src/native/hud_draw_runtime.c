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
  SCENE_MATRIX = 0x00570970u
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
static X2Rect g_portraits[4];
static unsigned g_portrait_mask;
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

static int prepare(void) {
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
  return x2_hud_layout_mobile(&settings->hud, settings->touch_controls) &&
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
  x2_touch_runtime_hud_regions(g_portraits, g_portrait_mask);
  g_portrait_mask = 0;
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

static void inventory_draw(CPU *cpu) {
  HudScope saved = g_scope;
  g_scope = (HudScope){0};
  if (prepare()) {
    float left = g_retail_space.left +
                 g_retail_space.width * (float)RDF32(VIEWPORT + 0x58u);
    /* Health potion starts at Z=202.0f; energy potion is at Z=182.0f. Group
       extent spans Z=[162.0f, 202.0f] (height 40.0f). */
    placement(2, (X2HudSpace){left, 202.0f, 60.0f, 40.0f}, g_layout.potions);
  }
  ++g_total[2];
  g_groups[2] += (unsigned)g_scope.active;
  x86_guest_body(cpu, "XMen2.exe", INVENTORY_DRAW);
  if (g_scope.active) {
    float xyz[3];
    const uint32_t centers[] = {POTION_CENTER, 0x00a0a118u};
    for (unsigned i = 0; i < 2; ++i) {
      read_floats(centers[i], xyz, 3);
      x2_hud_transform_point(g_scope.transform, xyz);
      write_floats(centers[i], xyz, 3);
    }
  }
  g_scope = saved;
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
      g_portraits[slot] =
          g_scope.active
              ? g_layout.portraits[slot]
              : x2_hud_output_rect(g_space, g_viewport.width, g_viewport.height,
                                   xyz[0], xyz[2], 20.0f);
      g_portrait_mask |= 1u << slot;
    } else {
      g_portrait_mask &= ~(1u << slot);
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
  x2_hud_transform_point(g_scope.transform, point);
  for (unsigned i = 0; i < 2; ++i)
    dimensions[i] = old_size[i] * g_scope.transform.scale;
  write_floats(position, point, 3);
  write_floats(size, dimensions, 2);
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
  x2_hud_transform_point(g_scope.transform, xyz);
  WR32(args, (uint32_t)(int32_t)lroundf(xyz[0]));
  WR32(args + 4u, (uint32_t)(int32_t)lroundf(xyz[2]));
  for (unsigned i = 2; i < 4; ++i)
    WR32(args + i * 4u, (uint32_t)(int32_t)lroundf((float)(int32_t)original[i] *
                                                   g_scope.transform.scale));
  WRF32(args + 16u, (float)RDF32(args + 16u) * g_scope.transform.scale);
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
  x2_hud_portrait_position_mapper(capture_portrait, NULL);
}
