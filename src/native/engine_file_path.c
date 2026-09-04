#include "x2_log.h"
/*
 * The PC build asks libIGCore to populate igFile's search path from the
 * Windows registry during engine initialisation.  The native host has a
 * selected install behind its virtual C: drive instead, so no registry value
 * exists to populate that state.  Do not write libIGCore's internal string:
 * use its retained public setter, then read back through its retained getter.
 */
#include "guest_heap.h"
#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "guest_body.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum {
  IGCORE_PREFERRED_BASE = 0x10000000u,
  IGFILE_SET_SEARCH_PATH = 0x10031480u,
  IGFILE_GET_SEARCH_PATH = 0x100315d0u,
  IGFILE_SET_SEARCH_PATH_FROM_REGISTRY = 0x10031540u
};

/* This is a guest path, deliberately independent of the host install path.
 * win_path.c resolves C: to GAME_PC_DIR, whether that came from desktop setup,
 * Android staging, or a maintainer override. */
static const char g_install_drive[] = "C:\\";

static int fail(char *why, int whyn, const char *message) {
  if (why && whyn > 0)
    snprintf(why, (size_t)whyn, "%s", message);
  return 0;
}

static X86Module *igcore_module(void) {
  X86Module *module;

  for (module = x86_modules(); module; module = module->next)
    if (module->name && !strcasecmp(module->name, "libIGCore.dll"))
      return module;
  return NULL;
}

static int entry_point(const X86Module *module, uint32_t linked,
                       uint32_t *mapped, char *why, int whyn) {
  uint32_t target;

  if (!module || !module->base || !*module->base ||
      module->preferred != IGCORE_PREFERRED_BASE)
    return fail(why, whyn, "libIGCore is not mapped at a valid base");
  target = *module->base + (linked - module->preferred);
  /* The address must belong to readable bytes in this exact image. A value
     measured against a different libIGCore build is refused before x86port
     can decode unrelated memory. */
  if (linked < module->preferred ||
      linked - module->preferred >= module->size ||
      !guest_memory_is_readable(target, 1u))
    return fail(why, whyn,
                "the libIGCore file-search entry point is outside the "
                "mapped image -- this is not the build it was measured on");
  *mapped = target;
  return 1;
}

static int apply_search_path(const CPU *source, char *why, int whyn) {
  X86Module *module;
  CPU call;
  uint32_t set_path, get_path, path, returned;

  if (!source)
    return fail(why, whyn, "no guest context for file-path setup");
  module = igcore_module();
  if (!module)
    return fail(why, whyn, "libIGCore is not linked");
  if (!entry_point(module, IGFILE_SET_SEARCH_PATH, &set_path, why, whyn) ||
      !entry_point(module, IGFILE_GET_SEARCH_PATH, &get_path, why, whyn))
    return 0;

  path = guest_malloc((uint32_t)sizeof g_install_drive);
  memcpy(guest_memory_pointer(path), g_install_drive, sizeof g_install_drive);

  call = *source;
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], path); /* cdecl const char * path */
  x86_guest_call_args(&call, set_path, 0u);
  guest_free(path); /* setSearchPath retained its own copy */

  call = *source;
  x86_guest_call_args(&call, get_path, 0u);
  returned = call.reg[kX86pEax];
  if (!returned ||
      !guest_memory_is_readable(returned, sizeof g_install_drive) ||
      memcmp(guest_memory_const_pointer(returned), g_install_drive,
             sizeof g_install_drive) != 0)
    return fail(why, whyn,
                "igFile did not retain the virtual install search path");

  if (why && whyn > 0)
    snprintf(why, (size_t)whyn, "igFile search path is %s", g_install_drive);
  return 1;
}

/*
 * This is the exact point at which the retail engine finishes asking its
 * registry for a file-search path.  Calling the setter immediately after DLL
 * attachment is invalid: igFile's allocator context does not exist yet.  The
 * retained body first preserves the registry behavior, then this portable
 * host supplies the virtual C: installation root only after that context is
 * live.
 */

static void x2_override_set_search_path_from_registry(CPU *C) {
  char why[160];

  x86_guest_body(C, "libIGCore.dll", 0x10031540u);
  if (!apply_search_path(C, why, (int)sizeof why)) {
    x2_log_error("ENGINE FILES: cannot configure search path: %s\n", why);
    abort();
  }
  x2_log_error("ENGINE FILES: %s\n", why);
}

__attribute__((constructor)) static void
x2_engine_file_path_register_override(void) {
  x86_register_override("libIGCore.dll", IGFILE_SET_SEARCH_PATH_FROM_REGISTRY,
                        x2_override_set_search_path_from_registry);
}
