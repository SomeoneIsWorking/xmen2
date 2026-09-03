#include "player_participation.h"

#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>

#define EXE_PREFERRED 0x00400000u
#define PARTICIPATION_SINGLETON_RVA (0x0048de40u - EXE_PREFERRED)

#define PARTICIPATION_ACTIVE 0x10u
#define PARTICIPATION_JOIN 0x14u
#define PARTICIPATION_LEAVE 0x18u
#define PARTICIPATION_RECONCILE 0x68u

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

static uint32_t thiscall_player(const CPU *source, uint32_t object,
                                uint32_t slot, unsigned player) {
  CPU call = *source;
  uint32_t vtable = RD32(object);
  call.esp -= 4u;
  WR32(call.esp, player);
  call.ecx = object;
  x86_guest_call_args(&call, RD32(vtable + slot), 4u);
  return call.eax;
}

static void thiscall0(const CPU *source, uint32_t object, uint32_t slot) {
  CPU call = *source;
  uint32_t vtable = RD32(object);
  call.ecx = object;
  x86_guest_call_args(&call, RD32(vtable + slot), 0u);
}

static void apply_to_manager(CPU *cpu, uint32_t manager, uint8_t join_players,
                             uint8_t leave_players) {
  unsigned player;
  int changed = 0;

  for (player = 0; player < 4u; player++) {
    uint8_t bit = (uint8_t)(1u << player);
    int active;
    if (!((join_players | leave_players) & bit))
      continue;
    active = (uint8_t)thiscall_player(cpu, manager, PARTICIPATION_ACTIVE,
                                      player) != 0;
    if ((leave_players & bit) && active) {
      thiscall_player(cpu, manager, PARTICIPATION_LEAVE, player);
      changed = 1;
    } else if ((join_players & bit) && !active) {
      thiscall_player(cpu, manager, PARTICIPATION_JOIN, player);
      changed = 1;
    }
  }
  if (changed) {
    thiscall0(cpu, manager, PARTICIPATION_RECONCILE);
    fprintf(stderr,
            "PLAYER-PARTICIPATION: retail reconcile; join=0x%02x "
            "leave=0x%02x.\n",
            join_players, leave_players);
  }
}

void x2_player_participation_apply(CPU *cpu, uint8_t join_players,
                                   uint8_t leave_players) {
  uint32_t base, manager;

  if (!cpu || !(join_players | leave_players))
    return;
  base = exe_base();
  if (!base)
    return;
  manager = guest_call0(cpu, base + PARTICIPATION_SINGLETON_RVA);
  if (!manager || !RD32(manager))
    return;
  apply_to_manager(cpu, manager, join_players, leave_players);
}

void x2_player_participation_enforce_eligibility(CPU *cpu,
                                                 uint8_t eligible_players) {
  uint32_t base, manager;
  uint8_t leave_players = 0;
  unsigned player;

  if (!cpu)
    return;
  base = exe_base();
  if (!base)
    return;
  manager = guest_call0(cpu, base + PARTICIPATION_SINGLETON_RVA);
  if (!manager || !RD32(manager))
    return;
  for (player = 0; player < 4u; player++) {
    uint8_t bit = (uint8_t)(1u << player);
    if (!(eligible_players & bit) &&
        (uint8_t)thiscall_player(cpu, manager, PARTICIPATION_ACTIVE, player) !=
            0)
      leave_players |= bit;
  }
  if (leave_players)
    apply_to_manager(cpu, manager, 0u, leave_players);
}
