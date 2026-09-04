#ifndef X2_CONFIG_ENVIRONMENT_H
#define X2_CONFIG_ENVIRONMENT_H

#ifdef __cplusplus
extern "C" {
#endif

/* The complete .env whitelist: bootstrap inputs, bounded diagnostics, and
 * compatibility names consumed by runtime_cvars. Runtime behavior is read
 * through runtime_cvars/settings rather than this registry; callers cannot
 * turn an arbitrary string into a process-environment read. */
typedef enum X2ConfigOverride {
  kX2ConfigDisplay,
  kX2ConfigGamePcDir,
  kX2ConfigAssets,
  kX2ConfigBootCmdTrace,
  kX2ConfigBootMap,
  kX2ConfigControl,
  kX2ConfigDrawObj,
  kX2ConfigDrawRange,
  kX2ConfigDrawTextures,
  kX2ConfigEpCount,
  kX2ConfigExitRing,
  kX2ConfigFiles,
  kX2ConfigFmvProbe,
  kX2ConfigFrameDump,
  kX2ConfigFrameTable,
  kX2ConfigGpuDebug,
  kX2ConfigGuestWatch,
  kX2ConfigHeartbeat,
  kX2ConfigHotEp,
  kX2ConfigInputFifo,
  kX2ConfigInputScript,
  kX2ConfigLightLog,
  kX2ConfigLightAddress,
  kX2ConfigLightDump,
  kX2ConfigLightDumpMin,
  kX2ConfigLightDumpSkip,
  kX2ConfigLightRaw,
  kX2ConfigLightSurvey,
  kX2ConfigLightSurveyEvery,
  kX2ConfigMaterialDump,
  kX2ConfigMaxFrames,
  kX2ConfigNativeFmv,
  kX2ConfigPadGlyphProbe,
  kX2ConfigPeek,
  kX2ConfigPhysicalMemoryMb,
  kX2ConfigProfile,
  kX2ConfigPromptGlyphs,
  kX2ConfigQuantum,
  kX2ConfigSaveDir,
  kX2ConfigSaveTrace,
  kX2ConfigStateBlockDump,
  kX2ConfigScripts,
  kX2ConfigSecurityWatch,
  kX2ConfigSelectorProbe,
  kX2ConfigSelectorTexture,
  kX2ConfigSettingsOpen,
  kX2ConfigShot,
  kX2ConfigShotAfterFile,
  kX2ConfigShotEvery,
  kX2ConfigShotKeep,
  kX2ConfigShotMinDraws,
  kX2ConfigShotVertexShader,
  kX2ConfigSpawnCritter,
  kX2ConfigSpin,
  kX2ConfigStackCheck,
  kX2ConfigTextureLevels,
  kX2ConfigTextureLumaAll,
  kX2ConfigTextureProbe,
  kX2ConfigTextScale,
  kX2ConfigUiResourceDir,
  kX2ConfigUnbounded,
  kX2ConfigUnpaced,
  kX2ConfigVerbose,
  kX2ConfigVirtualPad,
  kX2ConfigVirtualPadId,
  kX2ConfigVsConstants,
  kX2ConfigWatch,
  kX2ConfigWatchLog,
  kX2ConfigWatchMemory,
  kX2ConfigWatchMax,
  kX2ConfigWatchSelftest,
  kX2ConfigFault,
  kX2ConfigFaultStack,
  kX2ConfigFaultSelftest,
  kX2ConfigWriteWatch,
  kX2ConfigOverrideCount
} X2ConfigOverride;

const char *x2_config_override_get(X2ConfigOverride variable);
int x2_config_override_set(X2ConfigOverride variable, const char *value,
                           int overwrite);
int x2_config_override_unset(X2ConfigOverride variable);
int x2_config_override_from_name(const char *name, X2ConfigOverride *variable);
const char *x2_config_override_name(X2ConfigOverride variable);

/* The retail CRT/Win32 environment is a guest ABI, not title configuration.
 * Its arbitrary names are isolated here so product subsystems cannot use them
 * as an untyped configuration escape hatch. */
typedef void (*X2GuestEnvironmentVisitor)(const char *entry, void *user);
const char *x2_guest_environment_get(const char *name);
int x2_guest_environment_set(const char *name, const char *value);
void x2_guest_environment_visit(X2GuestEnvironmentVisitor visitor, void *user);

#ifdef __cplusplus
}
#endif

#endif /* X2_CONFIG_ENVIRONMENT_H */
