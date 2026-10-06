/*
 * The four powers the player's hero has on RT, as the game's own ring sees
 * them.
 *
 * XMen2.exe's CHudInputMap update (FUN_005a6c60, vtable 0x0069dc6c slot 2)
 * runs every frame for each player and, while RT is held, draws four circles:
 * slot i shows the power named at `stats + 0xc4 + i * 0x15`, where stats is
 * FUN_0041d5a0(actor), resolved to a move through the actor's power styles
 * (FUN_0041b120 interns the name, FUN_00427ca0 looks it up). A slot whose move
 * does not resolve is drawn empty. The cast side (FUN_004fc970, table
 * 0x006dc37c) pairs slot i with action bits 4, 5, 8, 6 -- LowAttack,
 * HighAttack, Guard, Jump -- which the Xbox preset binds to A, B, X, Y.
 *
 * The touch overlay offers exactly those slots as buttons, so this asks the
 * same functions the ring asks, after the game's own update, and hands the
 * answer to the touch runtime. A move's `icon` attribute is the byte at
 * +0x13c (FightMove parser 0x004f6b80) and indexes its power style's
 * `iconfile` atlas; the style keeps that name as a string-pool handle at +0x68
 * (FUN_005002c0).
 *
 * The lookups run only while touch is the active input, and the name interning
 * only when the actor or its four slot names changed: assignments change in
 * menus, not per frame.
 */
#include "power_slots.h"

#include "guest_body.h"
#include "win_path.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "../input/touch_runtime.h"

#include <lucent/log_c.h>
#include <stdio.h>
#include <string.h>

enum {
  IMAGE_BASE = 0x00400000u,
  HUD_INPUT_MAP_UPDATE = 0x005a6c60u,
  HUD_PLAYER_INDEX = 0x1cu,
  HERO_HANDLES = 0x0070b814u,
  RESOLVE_HANDLE = 0x004654b0u, /* this = &handle -> actor */
  ACTOR_CHECK = 0x00402b60u,    /* cdecl(actor) -> actor or 0 */
  ACTOR_STATS = 0x0041d5a0u,    /* this = actor -> stats */
  SLOT_NAME = 0x004b8420u,      /* this = stats, (slot) -> char * */
  INTERN_NAME = 0x0041b120u,    /* this = &handle, (char *) */
  FIND_MOVE = 0x00427ca0u,      /* this = actor, (&handle) -> move */
  MOVE_STYLE_VMETHOD = 0xe0u,   /* move -> its power style */
  MOVE_ICON = 0x13cu,
  STYLE_ICONFILE = 0x68u,
  STRING_POOL = 0x00a2c440u,
  STRING_POOL_TEXT = 0x4008u,
  SLOT_NAME_BYTES = 0x15u,
  SLOT_NAMES = 0xc4u,
  GUEST_TEXT_MAX = 256u
};

typedef struct PowerSlotSource {
  uint32_t actor;
  uint8_t names[X2_POWER_SLOTS][SLOT_NAME_BYTES];
} PowerSlotSource;

static PowerSlotSource g_source;
static char g_atlas[512];
static int g_icons[X2_POWER_SLOTS] = {-1, -1, -1, -1};

const char *x2_power_slots_atlas(void) { return g_atlas; }

static uint32_t exe(uint32_t linked) {
  return x86_module_base("XMen2.exe") + linked - IMAGE_BASE;
}

/* A thiscall with at most one stack argument, run on a copy of the caller's
   context so the frame the game is in is left exactly as it was. */
static uint32_t call_this(const CPU *source, uint32_t target, uint32_t self,
                          const uint32_t *argument) {
  CPU call = *source;
  if (argument) {
    call.reg[kX86pEsp] -= 4u;
    WR32(call.reg[kX86pEsp], *argument);
  }
  call.reg[kX86pEcx] = self;
  x86_guest_call_args(&call, target, argument ? 4u : 0u);
  return call.reg[kX86pEax];
}

static uint32_t player_one_actor(const CPU *source) {
  CPU call = *source;
  const uint32_t handle = RD32(exe(HERO_HANDLES));
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], handle);
  const uint32_t actor =
      call_this(&call, exe(RESOLVE_HANDLE), call.reg[kX86pEsp], NULL);
  if (!actor)
    return 0;
  call = *source;
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], actor);
  x86_guest_call_args(&call, exe(ACTOR_CHECK), 0u);
  return call.reg[kX86pEax];
}

static void read_text(uint32_t address, char *out, size_t size) {
  size_t i = 0;
  for (; address && i + 1 < size; ++i) {
    out[i] = (char)RD8(address + i);
    if (!out[i])
      return;
  }
  out[i] = 0;
}

static void pooled_text(uint32_t handle, char *out, size_t size) {
  const uint32_t pool = exe(STRING_POOL);
  out[0] = 0;
  if (handle)
    read_text(pool + STRING_POOL_TEXT +
                  RD32(pool + 4u + (handle & 0xffffffu) * 4u),
              out, size);
}

/* "textures/ui/magneto_icons1.png" names the IGB the engine loads for it. */
static void atlas_host_path(const char *guest, char *out, size_t size) {
  char igb[GUEST_TEXT_MAX];
  const char *dot = strrchr(guest, '.');
  const size_t stem = dot ? (size_t)(dot - guest) : strlen(guest);
  out[0] = 0;
  if (!guest[0] || stem + 5u > sizeof igb)
    return;
  memcpy(igb, guest, stem);
  memcpy(igb + stem, ".igb", 5u);
  snprintf(out, size, "%s", win_path(igb));
}

static void look_up(const CPU *cpu, uint32_t actor, uint32_t stats) {
  char atlas[GUEST_TEXT_MAX] = "";
  for (uint32_t slot = 0; slot < X2_POWER_SLOTS; ++slot) {
    const uint32_t name = call_this(cpu, exe(SLOT_NAME), stats, &slot);
    CPU frame = *cpu;
    frame.reg[kX86pEsp] -= 4u;
    const uint32_t handle_at = frame.reg[kX86pEsp];
    WR32(handle_at, 0);
    call_this(&frame, exe(INTERN_NAME), handle_at, &name);
    const uint32_t move = call_this(&frame, exe(FIND_MOVE), actor, &handle_at);
    g_icons[slot] = -1;
    if (!move)
      continue;
    g_icons[slot] = RD8(move + MOVE_ICON);
    if (!atlas[0]) {
      const uint32_t style =
          call_this(cpu, RD32(RD32(move) + MOVE_STYLE_VMETHOD), move, NULL);
      pooled_text(RD32(style + STYLE_ICONFILE), atlas, sizeof atlas);
    }
  }
  atlas_host_path(atlas, g_atlas, sizeof g_atlas);
  if (!g_atlas[0])
    for (unsigned slot = 0; slot < X2_POWER_SLOTS; ++slot)
      g_icons[slot] = -1;
  lucent_log_info("touch", "power slots: atlas \"%s\" -> %s; icons %d %d %d %d",
                  atlas, g_atlas[0] ? g_atlas : "(none)", g_icons[0],
                  g_icons[1], g_icons[2], g_icons[3]);
}

static void publish(const CPU *cpu) {
  PowerSlotSource now;
  memset(&now, 0, sizeof now);
  now.actor = player_one_actor(cpu);
  const uint32_t stats =
      now.actor ? call_this(cpu, exe(ACTOR_STATS), now.actor, NULL) : 0u;
  if (stats) {
    for (unsigned slot = 0; slot < X2_POWER_SLOTS; ++slot)
      for (unsigned i = 0; i < SLOT_NAME_BYTES; ++i)
        now.names[slot][i] =
            RD8(stats + SLOT_NAMES + slot * SLOT_NAME_BYTES + i);
    if (memcmp(&now, &g_source, sizeof now) != 0)
      look_up(cpu, now.actor, stats);
  } else {
    now.actor = 0;
    g_atlas[0] = 0;
    for (unsigned slot = 0; slot < X2_POWER_SLOTS; ++slot)
      g_icons[slot] = -1;
  }
  g_source = now;
  x2_touch_runtime_power_slots(g_icons);
}

static void hud_input_map_update(CPU *cpu) {
  const uint32_t self = cpu->reg[kX86pEcx];
  const CPU entry = *cpu;
  x86_guest_body(cpu, "XMen2.exe", HUD_INPUT_MAP_UPDATE);
  if (RD32(self + HUD_PLAYER_INDEX) == 0u && x2_touch_runtime_active())
    publish(&entry);
}

__attribute__((constructor)) static void register_power_slots(void) {
  x86_register_override("XMen2.exe", HUD_INPUT_MAP_UPDATE,
                        hud_input_map_update);
}
