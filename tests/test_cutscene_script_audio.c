#include "cutscene_script_audio.h"
#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

enum {
    ARENA_BASE = 0x32000000u,
    ARENA_SIZE = 0x10000u,
    STACK = ARENA_BASE + 0xf000u,
    SOUND_HANDLER = 0x004a7130u,
    OWNED_CONTEXT = 0x12345678u,
    FOREIGN_CONTEXT = 0x87654321u,
    ARGUMENT_LIST = 0x32001000u
};

static x86_override_fn registered;
static unsigned super_calls;
static unsigned silent;
static uint32_t current_context;
static unsigned failures;

volatile uint32_t x2_write_watch_addr;

void x2_write_watch_fire(uint32_t address, uint32_t value)
{
    (void)address;
    (void)value;
}

int cutscene_player_silences_current_context(uint32_t *context)
{
    *context = current_context;
    return silent && *context == OWNED_CONTEXT;
}

void fn_XMen2_004a7130(CPU *cpu)
{
    super_calls++;
    cpu->eax = 0xabcdef00u;
    cpu->esp += 4u;
}

void x86_register_override(const char *module, uint32_t entry,
                           x86_override_fn function)
{
    if (strcmp(module, "XMen2.exe") || entry != SOUND_HANDLER)
        failures++;
    registered = function;
}

static CPU call_with(uint32_t argument_list)
{
    CPU cpu;

    memset(&cpu, 0, sizeof cpu);
    cpu.esp = STACK;
    WR32(cpu.esp, 0xdeadbeefu);
    WR32(cpu.esp + 4u, argument_list);
    registered(&cpu);
    return cpu;
}

int main(void)
{
    CutsceneScriptAudioSnapshot snapshot;
    CPU cpu;

    if (guest_memory_init() != 0 ||
        guest_memory_map_fixed(ARENA_BASE, ARENA_SIZE,
                               PROT_READ | PROT_WRITE) != 0) {
        fprintf(stderr, "FAIL: could not map isolated guest arena\n");
        return 1;
    }
    if (!registered) failures++;
    current_context = OWNED_CONTEXT;
    cpu = call_with(ARGUMENT_LIST);
    failures += super_calls != 1u || cpu.eax != 0xabcdef00u ||
                cpu.esp != STACK + 4u;

    silent = 1u;
    current_context = FOREIGN_CONTEXT;
    cpu = call_with(ARGUMENT_LIST);
    failures += super_calls != 2u || cpu.eax != 0xabcdef00u ||
                cpu.esp != STACK + 4u;
    current_context = OWNED_CONTEXT;
    cpu = call_with(ARGUMENT_LIST);
    failures += super_calls != 2u || cpu.eax != 0u ||
                cpu.esp != STACK + 4u;

    cutscene_script_audio_snapshot(&snapshot);
    failures += snapshot.ordinary_commands != 2u ||
                snapshot.silent_commands != 1u ||
                snapshot.last_context != OWNED_CONTEXT;
    printf("cutscene script audio: %s -- only the owned BehavEd sound "
           "command is silent; ordinary and foreign contexts super-call\n",
           failures ? "FAILED" : "PASSED");
    return failures != 0;
}
