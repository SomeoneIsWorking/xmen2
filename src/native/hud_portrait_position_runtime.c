#include "hud_portrait_position.h"

#include "guest_body.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <lucent/cvar_c.h>
#include <lucent/log_c.h>
#include <stdlib.h>
#include <string.h>

enum {
  PORTRAIT_POSITION = 0x005a1650u,
  PLAYER_MANAGER = 0x0048de40u,
  IMAGE_BASE = 0x00400000u,
  ANCHOR_POSITION = 8u,
  ANCHOR_SCALE = 0x14u,
  PORTRAIT_PARENT = 0x2cu,
  PLAYER_COUNT_VMETHOD = 4u
};

static X2HudPortraitMap g_mapper;
static void *g_mapper_context;
static unsigned long g_calls;
static unsigned long g_native_calls;
static unsigned long g_verified;
static unsigned long g_mismatches;

void x2_hud_portrait_position_mapper(X2HudPortraitMap mapper, void *context) {
  g_mapper = mapper;
  g_mapper_context = context;
}

static X2HudPortraitAnchor read_anchor(uint32_t address) {
  X2HudPortraitAnchor result;
  for (unsigned axis = 0; axis < 3; ++axis)
    result.xyz[axis] = RDF32(address + ANCHOR_POSITION + axis * 4u);
  result.scale = RDF32(address + ANCHOR_SCALE);
  return result;
}

static int player_count(const CPU *source) {
  CPU call = *source;
  x86_guest_call_args(
      &call, x86_module_base("XMen2.exe") + PLAYER_MANAGER - IMAGE_BASE, 0u);
  const uint32_t manager = call.reg[kX86pEax];
  call.reg[kX86pEcx] = manager;
  x86_guest_call_args(&call, RD32(RD32(manager) + PLAYER_COUNT_VMETHOD), 0u);
  return (int32_t)call.reg[kX86pEax];
}

static void portrait_position(CPU *cpu) {
  const uint32_t portrait = cpu->reg[kX86pEcx];
  const uint32_t output_address = RD32(cpu->reg[kX86pEsp] + 4u);
  float output[3];
  ++g_calls;
  if (player_count(cpu) > 1) {
    /* Multiplayer positions belong to CHud's per-player layout method. */
    x86_guest_body(cpu, "XMen2.exe", PORTRAIT_POSITION);
    for (unsigned axis = 0; axis < 3; ++axis)
      output[axis] = RDF32(output_address + axis * 4u);
  } else {
    const X2HudPortraitAnchor local = read_anchor(portrait);
    const X2HudPortraitAnchor parent =
        read_anchor(RD32(portrait + PORTRAIT_PARENT));
    x2_hud_portrait_position(&parent, &local, output);
    ++g_native_calls;
    if (lucent_cvar_flag("hud.verify", 0)) {
      CPU original = *cpu;
      float expected[3];
      x86_guest_body(&original, "XMen2.exe", PORTRAIT_POSITION);
      for (unsigned axis = 0; axis < 3; ++axis)
        expected[axis] = RDF32(output_address + axis * 4u);
      ++g_verified;
      if (memcmp(output, expected, sizeof output) != 0 ||
          original.reg[kX86pEax] != output_address ||
          original.reg[kX86pEsp] != cpu->reg[kX86pEsp] + 8u) {
        ++g_mismatches;
        lucent_log_error("hud",
                         "portrait position mismatch at %08x: native "
                         "(%g,%g,%g), guest (%g,%g,%g)",
                         portrait, (double)output[0], (double)output[1],
                         (double)output[2], (double)expected[0],
                         (double)expected[1], (double)expected[2]);
        abort();
      }
    }
    cpu->reg[kX86pEax] = output_address;
    cpu->reg[kX86pEsp] += 8u;
  }
  if (g_mapper)
    g_mapper(g_mapper_context, portrait, output);
  for (unsigned axis = 0; axis < 3; ++axis)
    WRF32(output_address + axis * 4u, output[axis]);
}

void x2_hud_portrait_position_report(void) {
  lucent_log_info("hud",
                  "portrait positions: %lu native of %lu calls; "
                  "%lu original comparisons, %lu mismatches",
                  g_native_calls, g_calls, g_verified, g_mismatches);
}

__attribute__((constructor)) static void register_portrait_position(void) {
  x86_register_override("XMen2.exe", PORTRAIT_POSITION, portrait_position);
}
