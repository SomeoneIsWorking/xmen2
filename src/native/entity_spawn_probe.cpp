/* Reports every BehavEd spawn, scripted or console, with its factory result. */
#include "x2_log.h"

#include "guest_body.h"
#include "guest_memory.h"
#include "x86rt_native.h"

#include <stdint.h>

#define SPAWN_HANDLER 0x004a4420u /* spawn(target, def, name, pos, orient) */
#define SPAWN_ENTITY 0x004612e0u  /* definition name -> entity */
#define ENTITY_HANDLE 0x1cu

static unsigned long g_spawns;
static uint32_t g_spawned_entity;

static void x2_spawn_probe_entity(CPU *C) {
  x86_guest_body(C, "XMen2.exe", SPAWN_ENTITY);
  if (C->reg[kX86pEax])
    g_spawned_entity = C->reg[kX86pEax];
}

static void x2_spawn_probe_handler(CPU *C) {
  g_spawned_entity = 0;
  x86_guest_body(C, "XMen2.exe", SPAWN_HANDLER);
  g_spawns++;
  x2_log_info("spawn: BehavEd spawn #%lu, entity 0x%08x handle %u\n", g_spawns,
              g_spawned_entity,
              g_spawned_entity ? RD32(g_spawned_entity + ENTITY_HANDLE) : 0u);
}

__attribute__((constructor)) static void entity_spawn_probe_register(void) {
  x86_register_override("XMen2.exe", SPAWN_HANDLER, x2_spawn_probe_handler);
  x86_register_override("XMen2.exe", SPAWN_ENTITY, x2_spawn_probe_entity);
}
