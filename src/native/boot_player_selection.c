#include "boot_player_selection.h"

#include "x86rt.h"
#include "x86rt_native.h"

#include <stdint.h>
#include <stdio.h>

enum {
  EXE_PREFERRED = 0x00400000u,
  PAD_MANAGER_RVA = 0x00151ed0u,
  PAD_CURRENT_PLAYER = 0x34u,
  PAD_SET_CURRENT_PLAYER = 0x68u,
  LOCAL_PLAYER_COUNT = 4u
};

static uint32_t mapped_exe_base(void) {
  const X86Module *module;
  for (module = x86_modules(); module; module = module->next)
    if (module->preferred == EXE_PREFERRED && *module->base)
      return *module->base;
  return 0u;
}

int x2_boot_player_select_primary(CPU *source, unsigned primary_player) {
  CPU call;
  uint32_t base;
  uint32_t manager;
  uint32_t setter;

  if (!source || primary_player >= LOCAL_PLAYER_COUNT)
    return 0;
  base = mapped_exe_base();
  if (!base)
    return 0;

  call = *source;
  x86_guest_call_args(&call, base + PAD_MANAGER_RVA, 0u);
  manager = call.eax;
  if (!manager ||
      !x86_peek32(RD32(manager) + PAD_SET_CURRENT_PLAYER, &setter) || !setter)
    return 0;

  call = *source;
  call.esp -= 4u;
  WR32(call.esp, primary_player);
  call.ecx = manager;
  x86_guest_call_args(&call, setter, 4u);
  if (RD32(manager + PAD_CURRENT_PLAYER) != primary_player) {
    fprintf(stderr,
            "BOOT PLAYER: CPadManager rejected primary local "
            "player %u.\n",
            primary_player + 1u);
    return 0;
  }
  fprintf(stderr,
          "BOOT PLAYER: title/menu presentation was bypassed; "
          "CPadManager selected primary local player %u through "
          "its retail setter.\n",
          primary_player + 1u);
  return 1;
}
