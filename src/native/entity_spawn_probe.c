/*
 * A deterministic, opt-in Scourge Critter rendering reproduction.
 *
 * X2_SPAWN_CRITTER=1 waits until Dead Zone's entry script has successfully
 * launched, then submits this through the retail console command path:
 *
 *   runscript spawn('_ACTIVE_HERO_','Critter_a','x2_probe_critter',...)
 *
 * This uses the map's own Critter_a archetype and the title's registered
 * BehavEd `spawn` command.  It does not move the party to an authored encounter
 * or mutate an asset.  The spawn handler and the entity factory it calls are
 * intercepted only to prove that the command reached retail code and returned
 * a concrete entity.
 */
#include "entity_spawn_probe.h"

#include "guest_heap.h"
#include "guest_memory.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "guest_body.h"

#define EXE_PREFERRED       0x00400000u
#define CONSOLE_EXEC_RVA    0x0015c410u /* FUN_0055c410, console +0x1c */
#define CONSOLE_MANAGER_RVA 0x003ac290u /* DAT_007ac290 */
#define SPAWN_HANDLER       0x004a4420u
#define SPAWN_ENTITY        0x004612e0u

#define DEADZONE_ENTRY "act1/deadzone/deadzone1/deadzone1"
#define PROBE_NAME     "x2_probe_critter"

static const char g_command[] =
    "runscript spawn('_ACTIVE_HERO_','Critter_a','" PROBE_NAME
    "',' 80.000 0.000 0.000 ',' 0.000 0.000 0.000 ')";

static int g_mode = -1;
static int g_submitted;
static int g_waiting;
static unsigned long g_spawn_handlers;
static uint32_t g_spawned_entity;
static uint32_t g_command_guest;

static uint32_t mapped_exe_base(void)
{
    const X86Module *module;
    for (module = x86_modules(); module; module = module->next)
        if (module->preferred == EXE_PREFERRED && module->base && *module->base)
            return *module->base;
    return 0;
}

static uint32_t copy_to_guest(const char *text)
{
    uint32_t bytes = (uint32_t)strlen(text) + 1u;
    uint32_t address = guest_malloc(bytes);
    if (address) memcpy(guest_memory_pointer(address), text, bytes);
    return address;
}


static void x2_spawn_probe_entity(CPU *C)
{
    x86_guest_body(C, "XMen2.exe", 0x004612e0u);
    if (g_waiting && C->eax) g_spawned_entity = C->eax;
}

static void x2_spawn_probe_handler(CPU *C)
{
    x86_guest_body(C, "XMen2.exe", 0x004a4420u);
    if (!g_waiting) return;

    g_spawn_handlers++;
    fprintf(stderr, "SPAWN PROBE: retail spawn handler #%lu; factory result "
                    "0x%08x%s\n", g_spawn_handlers, g_spawned_entity,
            g_spawned_entity ? " -- CREATED" : " -- creation failed");
    fflush(stderr);
    if (!g_spawned_entity) return;

    g_waiting = 0;
    guest_free(g_command_guest);
    g_command_guest = 0;
}

void entity_spawn_probe_after_script_launch(CPU *source, const char *script)
{
    CPU call;
    uint32_t base;
    const char *value;

    if (g_mode < 0) {
        value = getenv("X2_SPAWN_CRITTER");
        g_mode = value && *value && strcmp(value, "0") != 0;
    }
    if (!g_mode || g_submitted || strcmp(script, DEADZONE_ENTRY) != 0) return;
    g_submitted = 1;

    base = mapped_exe_base();
    g_command_guest = copy_to_guest(g_command);
    if (!base || !g_command_guest) {
        fprintf(stderr, "SPAWN PROBE: REFUSED -- could not prepare the retail "
                        "console command in guest memory\n");
        fflush(stderr);
        if (g_command_guest) guest_free(g_command_guest);
        g_command_guest = 0;
        return;
    }

    fprintf(stderr, "SPAWN PROBE: Dead Zone entry launched; dispatching the "
                    "retail command: %s\n", g_command);
    fflush(stderr);
    g_waiting = 1;
    g_spawned_entity = 0;
    call = *source;
    call.esp -= 4u;
    WR32(call.esp, g_command_guest);
    call.ecx = base + CONSOLE_MANAGER_RVA;
    x86_guest_call_args(&call, base + CONSOLE_EXEC_RVA, 4u);
}

__attribute__((constructor))
static void entity_spawn_probe_register(void)
{
    x86_register_override("XMen2.exe", SPAWN_HANDLER, x2_spawn_probe_handler);
    x86_register_override("XMen2.exe", SPAWN_ENTITY, x2_spawn_probe_entity);
}
