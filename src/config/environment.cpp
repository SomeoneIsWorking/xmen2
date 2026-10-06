#include "environment.h"

#include <iterator>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
extern char **environ;
#endif

namespace {

#if defined(_WIN32)
// _putenv_s updates the CRT copy getenv reads and the process block a child
// inherits; an empty value removes the variable.
int environment_set(const char *name, const char *value, int overwrite) {
  if (!overwrite && getenv(name)) {
    return 0;
  }
  return _putenv_s(name, value) == 0 ? 0 : -1;
}
int environment_unset(const char *name) {
  return _putenv_s(name, "") == 0 ? 0 : -1;
}
char **environment_entries() { return _environ; }
#else
int environment_set(const char *name, const char *value, int overwrite) {
  return setenv(name, value, overwrite);
}
int environment_unset(const char *name) { return unsetenv(name); }
char **environment_entries() { return environ; }
#endif

} // namespace

static const char *const k_override_names[] = {
    "DISPLAY",
    "GAME_PC_DIR",
    "X2_ASSETS",
    "X2_BOOT_CMD_TRACE",
    "X2_BOOT_MAP",
    "X2_DRAW_OBJ",
    "X2_DRAW_RANGE",
    "X2_DRAW_TEXTURES",
    "X2_EPCOUNT",
    "X2_EXIT_RING",
    "X2_FILES",
    "X2_FMV_PROBE",
    "X2_FRAME_DUMP",
    "X2_FRAME_TABLE",
    "X2_GPU_DEBUG",
    "X2_HEARTBEAT",
    "X2_HOTEP",
    "X2_INPUT_FIFO",
    "X2_INPUT_SCRIPT",
    "X2_LIGHTLOG",
    "X2_LIGHT_ADDR",
    "X2_LIGHT_DUMP",
    "X2_LIGHT_DUMP_MIN",
    "X2_LIGHT_DUMP_SKIP",
    "X2_LIGHT_RAW",
    "X2_LIGHT_SURVEY",
    "X2_LIGHT_SURVEY_EVERY",
    "X2_MATERIAL_DUMP",
    "X2_MAX_FRAMES",
    "X2_NATIVE_FMV",
    "X2_PAD_GLYPH_PROBE",
    "X2_PHYS_MB",
    "X2_PROFILE",
    "X2_PROMPT_GLYPHS",
    "X2_QUANTUM",
    "X2_SAVE_DIR",
    "X2_SAVE_TRACE",
    "X2_SB_DUMP",
    "X2_SCRIPTS",
    "X2_SECURITY_WATCH",
    "X2_SELECTOR_PROBE",
    "X2_SELECTOR_TEXTURE",
    "X2_SETTINGS_OPEN",
    "X2_SHOT",
    "X2_SHOT_AFTER_FILE",
    "X2_SHOT_EVERY",
    "X2_SHOT_KEEP",
    "X2_SHOT_MIN_DRAWS",
    "X2_SHOT_VS",
    "X2_SPAWN_CRITTER",
    "X2_SPIN",
    "X2_STACKCHECK",
    "X2_TEXTURE_LEVELS",
    "X2_TEXTURE_LUMA",
    "X2_TEXTURE_LUMA_ALL",
    "X2_TEXTURE_PROBE",
    "X2_TEXT_SCALE",
    "X2_UI_RESOURCE_DIR",
    "X2_UNBOUNDED",
    "X2_UNPACED",
    "X2_VERBOSE",
    "X2_VIRTUAL_PAD",
    "X2_VIRTUAL_PAD_ID",
    "VK_DRIVER_FILES",
    "VK_ICD_FILENAMES",
    "X2_VSCONST",
    "X2_WATCH",
    "X2_WATCH_LOG",
    "X2_WATCH_MEM",
    "X2_WATCH_MAX",
    "X2_WATCH_SELFTEST",
    "X2_FAULT",
    "X2_FAULT_STACK",
    "X2_FAULT_SELFTEST",
    "SDL_AUDIODRIVER",
    "X2_LOG_DIR",
};
/* Positional, so a name added out of step with the enum shifts every later one.
 */
static_assert(std::size(k_override_names) == kX2ConfigOverrideCount);

const char *x2_config_override_name(X2ConfigOverride variable) {
  if (variable < 0 || variable >= kX2ConfigOverrideCount)
    return NULL;
  return k_override_names[variable];
}

const char *x2_config_override_get(X2ConfigOverride variable) {
  const char *name = x2_config_override_name(variable);
  return name ? getenv(name) : NULL;
}

int x2_config_override_set(X2ConfigOverride variable, const char *value,
                           int overwrite) {
  const char *name = x2_config_override_name(variable);
  return name && value ? environment_set(name, value, overwrite) : -1;
}

int x2_config_override_unset(X2ConfigOverride variable) {
  const char *name = x2_config_override_name(variable);
  return name ? environment_unset(name) : -1;
}

int x2_config_override_from_name(const char *name, X2ConfigOverride *variable) {
  if (!name || !variable)
    return 0;
  for (int index = 0; index < kX2ConfigOverrideCount; ++index) {
    if (strcmp(name, k_override_names[index]) == 0) {
      *variable = (X2ConfigOverride)index;
      return 1;
    }
  }
  return 0;
}

const char *x2_guest_environment_get(const char *name) {
  return name ? getenv(name) : NULL;
}

int x2_guest_environment_set(const char *name, const char *value) {
  if (!name || !name[0])
    return -1;
  return value ? environment_set(name, value, 1) : environment_unset(name);
}

void x2_guest_environment_visit(X2GuestEnvironmentVisitor visitor, void *user) {
  if (!visitor)
    return;
  for (char **entry = environment_entries(); entry && *entry; ++entry)
    visitor(*entry, user);
}
