#include "x2_log.h"
/* Device-appropriate tutorial dialog text.
 *
 * CPopupDialog::create (XMen2.exe FUN_005ebbc0) loads the localized dialog
 * asset first, then hardcodes eight PC tutorial paths to igct.bnx keys. Those
 * PC strings add mouse and shortcut-key prose; the dialog assets themselves
 * contain the controller-authored text in every shipped language.
 *
 * The hardcoded branch reaches the shared localization lookup
 * (FUN_00629bf0) from exactly 0x005ec061. For a connected controller, this
 * scoped override asks the already-loaded dialog reader for its `text` field,
 * exactly as FUN_005ebbc0's ordinary branch does. Every other lookup, and the
 * same popup with no controller, super-calls the retail localization path.
 */
#include "dialog_prompts.h"

#include "player_input.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "guest_body.h"
#include <stdio.h>
#include <stdlib.h>

#define EXE_PREFERRED 0x00400000u
#define PC_HINT_LOCALIZATION_RETURN 0x005ec066u
#define DIALOG_READER_FROM_OVERRIDE_ESP 0x1cu
#define ASSET_TEXT_KEY 0x00685cbcu
#define EMPTY_TEXT 0x00681968u

static unsigned long g_asset_text, g_pc_text, g_other_lookups;

int dialog_prompts_use_asset_text(int player_uses_gamepad,
                                  uint32_t localization_return) {
  return player_uses_gamepad &&
         localization_return == PC_HINT_LOCALIZATION_RETURN;
}

static X86Module *exe_module(void) {
  X86Module *module;
  for (module = x86_modules(); module; module = module->next)
    if (module->preferred == EXE_PREFERRED && module->base && *module->base)
      return module;
  return NULL;
}

static uint32_t mapped_address(const X86Module *module, uint32_t linked) {
  return *module->base + (linked - module->preferred);
}

static void return_dialog_asset_text(CPU *C, const X86Module *module) {
  uint32_t outer_esp = C->reg[kX86pEsp];
  uint32_t return_address = RD32(outer_esp);

  /* FUN_00564b70(reader, "text", "") is RET 8. Its reader is the local
     parser object at caller ESP+0x14; localization's return and one argument
     put that object at override-entry ESP+0x1c. */
  C->reg[kX86pEcx] = outer_esp + DIALOG_READER_FROM_OVERRIDE_ESP;
  C->reg[kX86pEsp] -= 4u;
  WR32(C->reg[kX86pEsp], mapped_address(module, EMPTY_TEXT));
  C->reg[kX86pEsp] -= 4u;
  WR32(C->reg[kX86pEsp], mapped_address(module, ASSET_TEXT_KEY));
  C->reg[kX86pEsp] -= 4u;
  WR32(C->reg[kX86pEsp], return_address);
  x86_guest_body(C, "XMen2.exe", 0x00564b70u);
  if (C->reg[kX86pEsp] != outer_esp) {
    x2_log_error("DIALOG-PROMPTS: asset text reader returned with "
                 "ESP 0x%08x, expected 0x%08x; refusing a corrupted "
                 "guest stack.\n",
                 C->reg[kX86pEsp], outer_esp);
    abort();
  }
  C->reg[kX86pEsp] +=
      4u; /* FUN_00629bf0 is cdecl: RET, caller removes its key. */
}

void x2_override_00629bf0(CPU *C) {
  X86Module *module = exe_module();
  uint32_t return_address;
  uint32_t linked_return;

  if (!module) {
    g_other_lookups++;
    x86_guest_body(C, "XMen2.exe", 0x00629bf0u);
    return;
  }
  return_address = RD32(C->reg[kX86pEsp]);
  if (return_address < *module->base ||
      return_address >= *module->base + module->size)
    linked_return = 0;
  else
    linked_return = module->preferred + (return_address - *module->base);
  if (!dialog_prompts_use_asset_text(x2_player_input_uses_gamepad(0),
                                     linked_return)) {
    if (linked_return == PC_HINT_LOCALIZATION_RETURN)
      g_pc_text++;
    else
      g_other_lookups++;
    x86_guest_body(C, "XMen2.exe", 0x00629bf0u);
    return;
  }
  return_dialog_asset_text(C, module);
  g_asset_text++;
}

__attribute__((constructor)) static void
x2_dialog_prompts_register_override(void) {
  x86_register_override("XMen2.exe", 0x00629bf0, x2_override_00629bf0);
}

void dialog_prompts_report(void) {
  static int done;
  if (done++)
    return;
  x2_log_info("  Tutorial dialog text: %lu controller asset, %lu PC override, "
              "%lu unrelated localization lookup(s)\n",
              g_asset_text, g_pc_text, g_other_lookups);
}
