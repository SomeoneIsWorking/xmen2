/* BehavEd `sound` presentation seam, ported from XMen2.exe 004a7130. */
#include "cutscene_script_audio.h"

#include "cutscene_player.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "guest_body.h"
#include <atomic>
#include <string.h>

namespace x2::native {

namespace {

enum { FN_SCRIPT_SOUND = 0x004a7130u };

std::atomic<unsigned long> g_ordinary_commands;
std::atomic<unsigned long> g_silent_commands;
std::atomic<unsigned> g_last_context;

} // namespace

void override_004a7130(CPU *cpu) {
  uint32_t context;

  if (!cpu)
    return;
  /* The explicit parameter is the command's argument list.  Retail
   * 004d8b30 publishes the executing BehavEd context at 00787730. */
  (void)RD32(cpu->reg[kX86pEsp] + 4u);
  if (cutscene_player_silences_current_context(&context)) {
    g_last_context.store(context, std::memory_order_relaxed);
    /* The retail command's audio presentation is its only side effect and
     * it unconditionally returns zero with a caller-clean plain RET. */
    cpu->reg[kX86pEax] = 0u;
    cpu->reg[kX86pEsp] += 4u;
    g_silent_commands.fetch_add(1u, std::memory_order_relaxed);
    return;
  }
  g_last_context.store(context, std::memory_order_relaxed);
  x86_guest_body(cpu, "XMen2.exe", 0x004a7130u);
  g_ordinary_commands.fetch_add(1u, std::memory_order_relaxed);
}

void cutscene_script_audio_snapshot(CutsceneScriptAudioSnapshot *out) {
  if (!out)
    return;
  memset(out, 0, sizeof *out);
  out->ordinary_commands = g_ordinary_commands.load(std::memory_order_relaxed);
  out->silent_commands = g_silent_commands.load(std::memory_order_relaxed);
  out->last_context = g_last_context.load(std::memory_order_relaxed);
}

namespace {

__attribute__((constructor)) void
x2_cutscene_script_audio_register_override(void) {
  x86_register_override("XMen2.exe", FN_SCRIPT_SOUND, override_004a7130);
}

} // namespace

} // namespace x2::native
