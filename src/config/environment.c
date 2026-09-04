#include "environment.h"

#include <stdlib.h>
#include <string.h>

extern char **environ;

static const char *const k_override_names[kX2ConfigOverrideCount] = {
    [kX2ConfigDisplay] = "DISPLAY",
    [kX2ConfigGamePcDir] = "GAME_PC_DIR",
    [kX2ConfigAssets] = "X2_ASSETS",
    [kX2ConfigBootCmdTrace] = "X2_BOOT_CMD_TRACE",
    [kX2ConfigBootMap] = "X2_BOOT_MAP",
    [kX2ConfigControl] = "X2_CONTROL",
    [kX2ConfigDrawObj] = "X2_DRAW_OBJ",
    [kX2ConfigDrawRange] = "X2_DRAW_RANGE",
    [kX2ConfigDrawTextures] = "X2_DRAW_TEXTURES",
    [kX2ConfigEpCount] = "X2_EPCOUNT",
    [kX2ConfigExitRing] = "X2_EXIT_RING",
    [kX2ConfigFiles] = "X2_FILES",
    [kX2ConfigFmvProbe] = "X2_FMV_PROBE",
    [kX2ConfigFrameDump] = "X2_FRAME_DUMP",
    [kX2ConfigFrameTable] = "X2_FRAME_TABLE",
    [kX2ConfigGpuDebug] = "X2_GPU_DEBUG",
    [kX2ConfigGuestWatch] = "X2_GUEST_WATCH",
    [kX2ConfigHeartbeat] = "X2_HEARTBEAT",
    [kX2ConfigHotEp] = "X2_HOTEP",
    [kX2ConfigInputFifo] = "X2_INPUT_FIFO",
    [kX2ConfigInputScript] = "X2_INPUT_SCRIPT",
    [kX2ConfigLightLog] = "X2_LIGHTLOG",
    [kX2ConfigLightAddress] = "X2_LIGHT_ADDR",
    [kX2ConfigLightDump] = "X2_LIGHT_DUMP",
    [kX2ConfigLightDumpMin] = "X2_LIGHT_DUMP_MIN",
    [kX2ConfigLightDumpSkip] = "X2_LIGHT_DUMP_SKIP",
    [kX2ConfigLightRaw] = "X2_LIGHT_RAW",
    [kX2ConfigLightSurvey] = "X2_LIGHT_SURVEY",
    [kX2ConfigLightSurveyEvery] = "X2_LIGHT_SURVEY_EVERY",
    [kX2ConfigMaterialDump] = "X2_MATERIAL_DUMP",
    [kX2ConfigMaxFrames] = "X2_MAX_FRAMES",
    [kX2ConfigNativeFmv] = "X2_NATIVE_FMV",
    [kX2ConfigPadGlyphProbe] = "X2_PAD_GLYPH_PROBE",
    [kX2ConfigPeek] = "X2_PEEK",
    [kX2ConfigPhysicalMemoryMb] = "X2_PHYS_MB",
    [kX2ConfigProfile] = "X2_PROFILE",
    [kX2ConfigPromptGlyphs] = "X2_PROMPT_GLYPHS",
    [kX2ConfigQuantum] = "X2_QUANTUM",
    [kX2ConfigSaveDir] = "X2_SAVE_DIR",
    [kX2ConfigSaveTrace] = "X2_SAVE_TRACE",
    [kX2ConfigStateBlockDump] = "X2_SB_DUMP",
    [kX2ConfigScripts] = "X2_SCRIPTS",
    [kX2ConfigSecurityWatch] = "X2_SECURITY_WATCH",
    [kX2ConfigSelectorProbe] = "X2_SELECTOR_PROBE",
    [kX2ConfigSelectorTexture] = "X2_SELECTOR_TEXTURE",
    [kX2ConfigSettingsOpen] = "X2_SETTINGS_OPEN",
    [kX2ConfigShot] = "X2_SHOT",
    [kX2ConfigShotAfterFile] = "X2_SHOT_AFTER_FILE",
    [kX2ConfigShotEvery] = "X2_SHOT_EVERY",
    [kX2ConfigShotKeep] = "X2_SHOT_KEEP",
    [kX2ConfigShotMinDraws] = "X2_SHOT_MIN_DRAWS",
    [kX2ConfigShotVertexShader] = "X2_SHOT_VS",
    [kX2ConfigSpawnCritter] = "X2_SPAWN_CRITTER",
    [kX2ConfigSpin] = "X2_SPIN",
    [kX2ConfigStackCheck] = "X2_STACKCHECK",
    [kX2ConfigTextureLevels] = "X2_TEXTURE_LEVELS",
    [kX2ConfigTextureLumaAll] = "X2_TEXTURE_LUMA_ALL",
    [kX2ConfigTextureProbe] = "X2_TEXTURE_PROBE",
    [kX2ConfigTextScale] = "X2_TEXT_SCALE",
    [kX2ConfigUiResourceDir] = "X2_UI_RESOURCE_DIR",
    [kX2ConfigUnbounded] = "X2_UNBOUNDED",
    [kX2ConfigUnpaced] = "X2_UNPACED",
    [kX2ConfigVerbose] = "X2_VERBOSE",
    [kX2ConfigVirtualPad] = "X2_VIRTUAL_PAD",
    [kX2ConfigVirtualPadId] = "X2_VIRTUAL_PAD_ID",
    [kX2ConfigVsConstants] = "X2_VSCONST",
    [kX2ConfigWatch] = "X2_WATCH",
    [kX2ConfigWatchLog] = "X2_WATCH_LOG",
    [kX2ConfigWatchMemory] = "X2_WATCH_MEM",
    [kX2ConfigWatchMax] = "X2_WATCH_MAX",
    [kX2ConfigWatchSelftest] = "X2_WATCH_SELFTEST",
    [kX2ConfigFault] = "X2_FAULT",
    [kX2ConfigFaultStack] = "X2_FAULT_STACK",
    [kX2ConfigFaultSelftest] = "X2_FAULT_SELFTEST",
    [kX2ConfigWriteWatch] = "X2_WRITE_WATCH",
};

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
  return name && value ? setenv(name, value, overwrite) : -1;
}

int x2_config_override_unset(X2ConfigOverride variable) {
  const char *name = x2_config_override_name(variable);
  return name ? unsetenv(name) : -1;
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
  return value ? setenv(name, value, 1) : unsetenv(name);
}

void x2_guest_environment_visit(X2GuestEnvironmentVisitor visitor, void *user) {
  if (!visitor)
    return;
  for (char **entry = environ; entry && *entry; ++entry)
    visitor(*entry, user);
}
