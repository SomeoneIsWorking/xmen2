#include "player_participation_probe.h"

#include "x86rt.h"
#include "x86rt_native.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define EXE_PREFERRED 0x00400000u
#define PARTICIPATION_SINGLETON_RVA (0x0048de40u - EXE_PREFERRED)
#define PARTICIPATION_ACTIVE 0x10u
#define PAD_MANAGER_RVA (0x00551ed0u - EXE_PREFERRED)
#define PAD_PLAYER 0x4cu
#define PAD_COUNT 0x70u
#define PLAYER_MASK 0x18u
#define PLAYER_PHYSICAL 0x2fcu
#define PLAYER_PHYSICAL_COUNT 30u

static void put(char *out, size_t size, size_t *at, const char *format, ...) {
  va_list args;
  int written;
  if (*at >= size)
    return;
  va_start(args, format);
  written = vsnprintf(out + *at, size - *at, format, args);
  va_end(args);
  if (written > 0)
    *at += (size_t)written < size - *at ? (size_t)written : size - *at;
}

static uint32_t exe_base(void) {
  X86Module *module;
  for (module = x86_modules(); module; module = module->next)
    if (module->preferred == EXE_PREFERRED && module->base && *module->base)
      return *module->base;
  return 0;
}

static uint32_t guest_call0(const CPU *source, uint32_t target) {
  CPU call = *source;
  x86_guest_call_args(&call, target, 0u);
  return call.eax;
}

static int active(const CPU *source, uint32_t manager, unsigned player) {
  CPU call = *source;
  uint32_t vtable = RD32(manager);
  call.esp -= 4u;
  WR32(call.esp, player);
  call.ecx = manager;
  x86_guest_call_args(&call, RD32(vtable + PARTICIPATION_ACTIVE), 4u);
  return (uint8_t)call.eax != 0;
}

static uint32_t thiscall(const CPU *source, uint32_t function, uint32_t object,
                         int has_argument, uint32_t argument) {
  CPU call = *source;
  if (has_argument) {
    call.esp -= 4u;
    WR32(call.esp, argument);
  }
  call.ecx = object;
  x86_guest_call_args(&call, function, has_argument ? 4u : 0u);
  return call.eax;
}

static void report_pad_players(CPU *cpu, uint32_t base, char *out, size_t size,
                               size_t *at) {
  uint32_t manager = base ? guest_call0(cpu, base + PAD_MANAGER_RVA) : 0u;
  uint32_t vtable = 0, function = 0, players = 0, player;

  if (!manager) {
    put(out, size, at,
        "pad manager: not constructed; no player input "
        "state read\n");
    return;
  }
  if (!x86_peek32(manager, &vtable) ||
      !x86_peek32(vtable + PAD_COUNT, &function) || !function) {
    put(out, size, at, "pad manager 0x%08x: player count unreadable\n",
        manager);
    return;
  }
  players = thiscall(cpu, function, manager, 0, 0u);
  put(out, size, at, "pad manager 0x%08x: %u player input object(s)\n", manager,
      players);
  if (players > 4u)
    players = 4u;
  for (player = 0; player < players; player++) {
    uint32_t object, player_vtable = 0, mask = 0, index, nonzero = 0;
    if (!x86_peek32(vtable + PAD_PLAYER, &function) || !function)
      continue;
    object = thiscall(cpu, function, manager, 1, player);
    if (!object) {
      put(out, size, at, "  player %u: no input object\n", player);
      continue;
    }
    if (x86_peek32(object, &player_vtable) &&
        x86_peek32(player_vtable + PLAYER_MASK, &function) && function)
      mask = thiscall(cpu, function, object, 0, 0u);
    put(out, size, at,
        "  player %u object 0x%08x logical mask 0x%08x "
        "physical:",
        player, object, mask);
    for (index = 0; index < PLAYER_PHYSICAL_COUNT; index++) {
      uint32_t bits = 0;
      float value;
      if (!x86_peek32(object + PLAYER_PHYSICAL + index * 4u, &bits))
        continue;
      memcpy(&value, &bits, sizeof value);
      if (value != 0.0f) {
        put(out, size, at, " [%u]=%.3f", index, (double)value);
        nonzero++;
      }
    }
    put(out, size, at, "%s\n", nonzero ? "" : " 0 of 30 floats non-zero");
  }
}

size_t x2_player_participation_probe_report(CPU *cpu, char *out, size_t size) {
  uint32_t base, manager;
  unsigned player, count = 0, mask = 0;
  size_t at = 0;

  if (!cpu || !out || !size)
    return 0;
  base = exe_base();
  manager = base ? guest_call0(cpu, base + PARTICIPATION_SINGLETON_RVA) : 0u;
  if (!manager || !RD32(manager)) {
    put(out, size, &at,
        "\nretail participation manager: not constructed; "
        "joined state unreadable\n");
  } else {
    for (player = 0; player < 4u; player++)
      if (active(cpu, manager, player)) {
        mask |= 1u << player;
        count++;
      }
    put(out, size, &at,
        "\nretail participation manager 0x%08x: joined mask 0x%x "
        "(%u active); P1 %s, P2 %s, P3 %s, P4 %s\n",
        manager, mask, count, mask & 1u ? "active" : "inactive",
        mask & 2u ? "active" : "inactive", mask & 4u ? "active" : "inactive",
        mask & 8u ? "active" : "inactive");
  }
  report_pad_players(cpu, base, out, size, &at);
  return at;
}
