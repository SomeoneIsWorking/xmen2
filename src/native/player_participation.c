#include "player_participation.h"
#include "player_participation_policy.h"
#include "x2_log.h"

#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>

#define EXE_PREFERRED 0x00400000u
#define PARTICIPATION_SINGLETON_RVA (0x0048de40u - EXE_PREFERRED)

#define PARTICIPATION_ACTIVE 0x10u
#define PARTICIPATION_JOIN 0x14u
#define PARTICIPATION_LEAVE 0x18u
#define PARTICIPATION_RECONCILE 0x68u
#define PLAYER_MANAGER_RVA (0x00551ed0u - EXE_PREFERRED)
#define PLAYER_CONTROLLER_MAP 0x4u
#define PLAYER_REMOTE_FLAGS 0x14u
#define PLAYER_LOCAL_CONTROLLERS 0x30u

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
  return call.reg[kX86pEax];
}

static uint32_t thiscall_player(const CPU *source, uint32_t object,
                                uint32_t slot, unsigned player) {
  CPU call = *source;
  uint32_t vtable = RD32(object);
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], player);
  call.reg[kX86pEcx] = object;
  x86_guest_call_args(&call, RD32(vtable + slot), 4u);
  return call.reg[kX86pEax];
}

static void thiscall0(const CPU *source, uint32_t object, uint32_t slot) {
  CPU call = *source;
  uint32_t vtable = RD32(object);
  call.reg[kX86pEcx] = object;
  x86_guest_call_args(&call, RD32(vtable + slot), 0u);
}

/* The game's player -> controller table and which controllers are local
   (player_participation_policy.h). */
static X2PlayerSeatMap read_seat_map(const CPU *cpu) {
  X2PlayerSeatMap map = {{0, 1, 2, 3}, 0x0fu};
  const uint32_t players = guest_call0(cpu, exe_base() + PLAYER_MANAGER_RVA);
  unsigned i;
  int32_t local_count;

  if (!players)
    return map;
  local_count = (int32_t)RD32(players + PLAYER_LOCAL_CONTROLLERS);
  map.local_controllers = 0;
  for (i = 0; i < X2_PARTICIPATION_PLAYERS; i++) {
    map.controller_of_player[i] =
        (int32_t)RD32(players + PLAYER_CONTROLLER_MAP + 4u * i);
    if ((int32_t)i < local_count && !RD8(players + PLAYER_REMOTE_FLAGS + i))
      map.local_controllers |= (uint8_t)(1u << i);
  }
  return map;
}

static void apply_to_manager(CPU *cpu, uint32_t manager,
                             const X2PlayerSeatMap *map, uint8_t join_players,
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
    x2_log_error("PLAYER-PARTICIPATION: retail reconcile; game players "
                 "join=0x%02x leave=0x%02x; player->controller %d %d %d %d, "
                 "local controllers 0x%x.\n",
                 join_players, leave_players, map->controller_of_player[0],
                 map->controller_of_player[1], map->controller_of_player[2],
                 map->controller_of_player[3], map->local_controllers);
  }
}

static uint32_t participation_manager(const CPU *cpu) {
  const uint32_t base = exe_base();
  uint32_t manager;
  if (!base)
    return 0;
  manager = guest_call0(cpu, base + PARTICIPATION_SINGLETON_RVA);
  return manager && RD32(manager) ? manager : 0;
}

void x2_player_participation_apply(CPU *cpu, uint8_t join_seats,
                                   uint8_t leave_seats) {
  uint32_t manager;
  X2PlayerSeatMap map;

  if (!cpu || !(join_seats | leave_seats))
    return;
  manager = participation_manager(cpu);
  if (!manager)
    return;
  map = read_seat_map(cpu);
  apply_to_manager(cpu, manager, &map,
                   x2_player_seats_to_players(&map, join_seats),
                   x2_player_seats_to_players(&map, leave_seats));
}

void x2_player_participation_enforce_eligibility(CPU *cpu,
                                                 uint8_t eligible_seats) {
  uint32_t manager;
  X2PlayerSeatMap map;
  uint8_t evict, leave_players = 0;
  unsigned player;

  if (!cpu)
    return;
  manager = participation_manager(cpu);
  if (!manager)
    return;
  map = read_seat_map(cpu);
  evict = (uint8_t)(x2_player_seats_governed(&map) &
                    ~x2_player_seats_to_players(&map, eligible_seats));
  for (player = 0; player < 4u; player++) {
    uint8_t bit = (uint8_t)(1u << player);
    if ((evict & bit) && (uint8_t)thiscall_player(
                             cpu, manager, PARTICIPATION_ACTIVE, player) != 0)
      leave_players |= bit;
  }
  if (leave_players)
    apply_to_manager(cpu, manager, &map, 0u, leave_players);
}
