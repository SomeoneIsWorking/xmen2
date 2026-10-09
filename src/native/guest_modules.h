/* The guest's module inventory.
 *
 * The inventory comes from the same list the installer validates against --
 * the player's own images -- and everything else about a module is read out
 * of its PE headers at run time.
 */
#pragma once

#include "x86rt_native.h"

#include <stdint.h>

namespace x2::native {

/* Where the game executable asks to be mapped; guest addresses in the
   disassembly are written against it. */
inline constexpr uint32_t kExePreferred = 0x00400000u;

/* Register every module this port loads, in load order. Call once, before the
   mapping loop. Returns 0, or non-zero having said which name it choked on. */
int guest_modules_register(void);

/* Where XMen2.exe is mapped, or 0 while it is not. */
inline uint32_t guest_exe_base(void) {
  for (const X86Module *module = x86_modules(); module; module = module->next) {
    if (module->preferred == kExePreferred && module->base && *module->base) {
      return *module->base;
    }
  }
  return 0;
}

} // namespace x2::native
