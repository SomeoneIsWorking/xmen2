/* BehavEd `sound` presentation seam, ported from XMen2.exe 004a7130. */
#include "cutscene_script_audio.h"

#include "cutscene_player.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdatomic.h>
#include <string.h>
#include "guest_body.h"

enum { FN_SCRIPT_SOUND = 0x004a7130u };

static _Atomic unsigned long g_ordinary_commands;
static _Atomic unsigned long g_silent_commands;
static _Atomic unsigned g_last_context;


void x2_override_004a7130(CPU *cpu)
{
    uint32_t context;

    if (!cpu) return;
    /* The explicit parameter is the command's argument list.  Retail
     * 004d8b30 publishes the executing BehavEd context at 00787730. */
    (void)RD32(cpu->esp + 4u);
    if (cutscene_player_silences_current_context(&context)) {
        atomic_store_explicit(&g_last_context, context,
                              memory_order_relaxed);
        /* The retail command's audio presentation is its only side effect and
         * it unconditionally returns zero with a caller-clean plain RET. */
        cpu->eax = 0u;
        cpu->esp += 4u;
        atomic_fetch_add_explicit(&g_silent_commands, 1u,
                                  memory_order_relaxed);
        return;
    }
    atomic_store_explicit(&g_last_context, context, memory_order_relaxed);
    x86_guest_body(cpu, "XMen2.exe", 0x004a7130u);
    atomic_fetch_add_explicit(&g_ordinary_commands, 1u,
                              memory_order_relaxed);
}

void cutscene_script_audio_snapshot(CutsceneScriptAudioSnapshot *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->ordinary_commands = atomic_load_explicit(
        &g_ordinary_commands, memory_order_relaxed);
    out->silent_commands = atomic_load_explicit(
        &g_silent_commands, memory_order_relaxed);
    out->last_context = atomic_load_explicit(
        &g_last_context, memory_order_relaxed);
}

__attribute__((constructor))
static void x2_cutscene_script_audio_register_override(void)
{
    x86_register_override("XMen2.exe", FN_SCRIPT_SOUND,
                          x2_override_004a7130);
}
