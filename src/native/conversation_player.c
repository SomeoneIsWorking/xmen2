/* The conversation payload seam used by the BehavEd cutscene player.
 *
 * XMen2.exe's conversation manager is not a cutscene owner.  It can, however,
 * suspend an authored script sequence until a deterministic response is
 * chosen.  This adapter exposes that yield and the same vtable +0x18
 * chooseResponse transition used by retail input; presentation policy stays
 * with the cutscene player above it.
 */
#include "conversation_player.h"

#include "x86rt.h"
#include "x86rt_native.h"

#define EXE_PREFERRED 0x00400000u
#define EXE_RVA(va) ((uint32_t)(va) - EXE_PREFERRED)
#define CONV_SINGLETON_RVA EXE_RVA(0x00717aacu)
#define FN_LINE_BY_ID 0x004573f0u

#define CV_CUR_LINE 0x004bcu
#define CV_FLAGS 0x21b24u
#define CVF_VISIBLE 0x02u
#define CV_RESP_IDS 0x004c0u
#define CV_RESP_COUNT 0x004e0u
#define CV_TAG_INDEX 0x21b26u
#define RESPONSE_NONE 0xffffffffu
#define RESPONSE_SLOTS 8u
#define VT_CHOOSE_RESPONSE 0x18u

static uint32_t exe_base(void) {
  X86Module *module;

  for (module = x86_modules(); module; module = module->next)
    if (module->preferred == EXE_PREFERRED && module->base && *module->base)
      return *module->base;
  return 0;
}

static int manager(uint32_t *out) {
  uint32_t base = exe_base();

  if (!base || !x86_peek32(base + CONV_SINGLETON_RVA, out) || !*out)
    return 0;
  return 1;
}

static int peek8(uint32_t address, uint8_t *out) {
  uint32_t word;
  unsigned shift = (address & 3u) * 8u;

  if (!x86_peek32(address & ~3u, &word))
    return 0;
  *out = (uint8_t)(word >> shift);
  return 1;
}

ConversationPlayerState conversation_player_state(struct CPU *cpu) {
  uint32_t self, count, id;
  uint8_t flags;
  unsigned responses = 0, i;

  (void)cpu;
  if (!manager(&self) || !peek8(self + CV_FLAGS, &flags))
    return CONVERSATION_PLAYER_UNREADABLE;
  if (!(flags & CVF_VISIBLE))
    return CONVERSATION_PLAYER_INACTIVE;
  if (!x86_peek32(self + CV_RESP_COUNT, &count) || count > RESPONSE_SLOTS)
    return CONVERSATION_PLAYER_UNREADABLE;
  for (i = 0; i < count; i++) {
    if (!x86_peek32(self + CV_RESP_IDS + i * 4u, &id))
      return CONVERSATION_PLAYER_UNREADABLE;
    if (id != RESPONSE_NONE)
      responses++;
  }
  if (responses == 1u)
    return CONVERSATION_PLAYER_DETERMINISTIC;
  if (responses > 1u)
    return CONVERSATION_PLAYER_CHOICE;
  return CONVERSATION_PLAYER_WAITING;
}

int conversation_player_selection(struct CPU *cpu,
                                  ConversationPlayerSelection *out) {
  CPU call;
  uint32_t base;
  uint32_t self, vtable, function, selected;

  if (!cpu || !out ||
      conversation_player_state(cpu) != CONVERSATION_PLAYER_DETERMINISTIC ||
      !manager(&self) || !x86_peek32(self, &vtable) ||
      !x86_peek32(vtable + VT_CHOOSE_RESPONSE, &function) || !function)
    return 0;
  selected = (uint32_t)(int32_t)(int16_t)RD16(self + CV_TAG_INDEX);
  out->manager = self;
  out->choose_response = function;
  out->selected = selected;
  out->line_presenter = 0u;
  base = exe_base();
  call = *cpu;
  call.esp -= 4u;
  WR32(call.esp, RD32(self + CV_CUR_LINE));
  call.ecx = self;
  x86_guest_call_args(&call, base + EXE_RVA(FN_LINE_BY_ID), 4u);
  if (call.eax && x86_peek32(call.eax, &vtable))
    (void)x86_peek32(vtable, &out->line_presenter);
  return 1;
}
