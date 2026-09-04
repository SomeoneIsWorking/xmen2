/* Dialogue policy for the synchronous in-game cutscene player.
 *
 * Retail input stops the current conversation voice before chooseResponse.
 * chooseResponse then calls beginResponse (00458700), which starts the next
 * voice and returns true so applying the response waits for a later frame.
 * A one-step cutscene completion has no such frame boundary: it must stop the
 * current voice and make beginResponse report "not presented" so the retail
 * response/script transition applies immediately without starting dialogue.
 */
#include "cutscene_dialogue.h"

#include "conversation_player.h"
#include "guest_body.h"
#include "x86rt.h"
#include "x86rt_native.h"

#define EXE_PREFERRED 0x00400000u
#define EXE_RVA(va) ((uint32_t)(va) - EXE_PREFERRED)
#define FN_AUDIO 0x00592480u
#define FN_LINE_AUDIO 0x0045a170u
#define FN_BEGIN_RESPONSE 0x00458700u
#define CONV_SINGLETON_RVA EXE_RVA(0x00717aacu)
#define NULL_SOUND_HANDLE_RVA EXE_RVA(0x0069d05cu)
#define CV_ACCEPT_STATE 0x239a4u
#define CV_CHOSEN_RESPONSE 0x239a0u
#define CV_SOUND_HANDLE 0x21b80u
#define VT_STOP_SOUND 0x74u

typedef struct CutsceneDialogueRuntime {
  unsigned depth;
  unsigned payload_depth;
  CutsceneDialogueSnapshot counters;
} CutsceneDialogueRuntime;

static CutsceneDialogueRuntime g_dialogue;

static uint32_t exe_base(void) {
  X86Module *module;

  for (module = x86_modules(); module; module = module->next)
    if (module->preferred == EXE_PREFERRED && module->base && *module->base)
      return *module->base;
  return 0;
}

static int stop_active_voice(CPU *cpu, uint32_t manager) {
  CPU call;
  uint32_t base = exe_base();
  uint32_t audio, handle, null_handle, vtable, stop;

  if (!cpu || !base ||
      !x86_peek32(base + NULL_SOUND_HANDLE_RVA, &null_handle) ||
      !x86_peek32(manager + CV_SOUND_HANDLE, &handle))
    return 0;
  if (handle == null_handle)
    return 1;

  call = *cpu;
  x86_guest_call(&call, base + EXE_RVA(FN_AUDIO));
  audio = call.reg[kX86pEax];
  if (!audio || !x86_peek32(audio, &vtable) ||
      !x86_peek32(vtable + VT_STOP_SOUND, &stop) || !stop)
    return 0;
  call = *cpu;
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], handle);
  call.reg[kX86pEcx] = audio;
  x86_guest_call_args(&call, stop, 4u);

  WR8(manager + CV_ACCEPT_STATE, RD8(manager + CV_ACCEPT_STATE) & 0xfeu);
  WR32(manager + CV_SOUND_HANDLE, null_handle);
  g_dialogue.counters.active_voice_stops++;
  g_dialogue.counters.last_stopped_handle = handle;
  return 1;
}

static int active_voice(uint32_t manager, uint32_t *handle) {
  uint32_t base = exe_base(), null_handle;

  if (!base || !x86_peek32(base + NULL_SOUND_HANDLE_RVA, &null_handle) ||
      !x86_peek32(manager + CV_SOUND_HANDLE, handle))
    return -1;
  return *handle != null_handle;
}

static uint32_t current_manager(void) {
  uint32_t base = exe_base(), manager = 0u;

  if (base)
    (void)x86_peek32(base + CONV_SINGLETON_RVA, &manager);
  return manager;
}

void cutscene_dialogue_skip_begin(void) { g_dialogue.depth++; }

void cutscene_dialogue_skip_end(struct X86pCpu *cpu) {
  uint32_t handle, manager;
  int playing;

  if (!g_dialogue.depth)
    return;
  if (g_dialogue.depth == 1u) {
    manager = current_manager();
    playing = manager ? active_voice(manager, &handle) : 0;
    if (playing > 0) {
      g_dialogue.counters.skip_presentation_starts++;
      (void)stop_active_voice(cpu, manager);
    }
  }
  g_dialogue.depth--;
}

int cutscene_dialogue_advance(struct X86pCpu *cpu) {
  ConversationPlayerSelection selection;
  CPU call;

  if (!conversation_player_selection(cpu, &selection) ||
      !stop_active_voice(cpu, selection.manager))
    return 0;
  g_dialogue.counters.last_line_presenter = selection.line_presenter;
  g_dialogue.counters.last_manager = selection.manager;
  call = *cpu;
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], selection.selected);
  call.reg[kX86pEcx] = selection.manager;
  cutscene_dialogue_skip_begin();
  g_dialogue.payload_depth++;
  x86_guest_call_args(&call, selection.choose_response, 4u);
  g_dialogue.payload_depth--;
  cutscene_dialogue_skip_end(cpu);
  {
    uint32_t handle;
    int playing = active_voice(selection.manager, &handle);

    if (playing > 0) {
      g_dialogue.counters.skip_presentation_starts++;
      if (!stop_active_voice(cpu, selection.manager))
        return 0;
    } else if (playing < 0) {
      return 0;
    }
  }
  g_dialogue.counters.advances++;
  return 1;
}

int cutscene_dialogue_payload_active(void) {
  return g_dialogue.payload_depth != 0u;
}

/* XMen2.exe 00458700: conversation response-voice presenter. */
void x2_override_00458700(CPU *cpu) {
  if (!g_dialogue.depth) {
    x86_guest_body(cpu, "XMen2.exe", 0x00458700u);
    if ((uint8_t)cpu->reg[kX86pEax]) {
      g_dialogue.counters.ordinary_response_starts++;
    }
    return;
  }

  /* The retail function's only unconditional state mutation precedes its
   * presentation work. Preserve it, then return AL=false so chooseResponse
   * immediately applies the response and its authored scripts. */
  WR32(cpu->reg[kX86pEcx] + CV_CHOSEN_RESPONSE, 0u);
  cpu->reg[kX86pEax] &= ~0xffu;
  cpu->reg[kX86pEsp] += 8u; /* RET 4 */
  g_dialogue.counters.suppressed_response_starts++;
}

/* XMen2.exe 0045a170: conversation line-voice presenter. */
void x2_override_0045a170(CPU *cpu) {
  uint32_t manager, before = 0u, after = 0u;
  int before_playing, after_playing;

  if (g_dialogue.depth) {
    cpu->reg[kX86pEsp] += 8u; /* RET 4 */
    g_dialogue.counters.suppressed_line_starts++;
    return;
  }

  manager = current_manager();
  before_playing = manager ? active_voice(manager, &before) : -1;
  x86_guest_body(cpu, "XMen2.exe", 0x0045a170u);
  after_playing = manager ? active_voice(manager, &after) : -1;
  if (after_playing > 0 && (before_playing <= 0 || before != after))
    g_dialogue.counters.ordinary_line_starts++;
}

void cutscene_dialogue_snapshot(CutsceneDialogueSnapshot *out) {
  if (!out)
    return;
  *out = g_dialogue.counters;
}

__attribute__((constructor)) static void
x2_cutscene_dialogue_register_override(void) {
  x86_register_override("XMen2.exe", FN_BEGIN_RESPONSE, x2_override_00458700);
  x86_register_override("XMen2.exe", FN_LINE_AUDIO, x2_override_0045a170);
}
