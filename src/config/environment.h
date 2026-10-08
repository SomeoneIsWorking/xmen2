#pragma once

namespace x2::config {

/* The complete .env whitelist: bootstrap inputs, bounded diagnostics, and
 * compatibility names consumed by runtime_cvars. Runtime behavior is read
 * through runtime_cvars/settings rather than this registry; callers cannot
 * turn an arbitrary string into a process-environment read. */
enum class ConfigOverride {
  Display,
  GamePcDir,
  Assets,
  BootCmdTrace,
  BootMap,
  DrawObj,
  DrawRange,
  DrawTextures,
  EpCount,
  ExitRing,
  Files,
  FmvProbe,
  FrameDump,
  FrameTable,
  GpuDebug,
  Heartbeat,
  HotEp,
  InputFifo,
  InputScript,
  LightLog,
  LightAddress,
  LightDump,
  LightDumpMin,
  LightDumpSkip,
  LightRaw,
  LightSurvey,
  LightSurveyEvery,
  MaterialDump,
  MaxFrames,
  NativeFmv,
  PadGlyphProbe,
  PhysicalMemoryMb,
  Profile,
  PromptGlyphs,
  Quantum,
  SaveDir,
  SaveTrace,
  StateBlockDump,
  Scripts,
  SecurityWatch,
  SelectorProbe,
  SelectorTexture,
  SettingsOpen,
  Shot,
  ShotAfterFile,
  ShotEvery,
  ShotKeep,
  ShotMinDraws,
  ShotVertexShader,
  SpawnCritter,
  Spin,
  StackCheck,
  TextureLevels,
  TextureLuma,
  TextureLumaAll,
  TextureProbe,
  TextScale,
  UiResourceDir,
  Unbounded,
  Unpaced,
  Verbose,
  VirtualPad,
  VirtualPadId,
  VulkanDriverFiles,
  VulkanIcdFilenames,
  VsConstants,
  Watch,
  WatchLog,
  WatchMemory,
  WatchMax,
  WatchSelftest,
  Fault,
  FaultStack,
  FaultSelftest,
  /* SDL's driver; `dummy` keeps a hidden run on SDL's real-cadence clock. */
  SdlAudioDriver,
  LogDir,
  Count
};

const char *config_override_get(ConfigOverride variable);
int config_override_set(ConfigOverride variable, const char *value,
                        int overwrite);
int config_override_unset(ConfigOverride variable);
int config_override_from_name(const char *name, ConfigOverride *variable);
const char *config_override_name(ConfigOverride variable);

/* The retail CRT/Win32 environment is a guest ABI, not title configuration.
 * Its arbitrary names are isolated here so product subsystems cannot use them
 * as an untyped configuration escape hatch. */
using GuestEnvironmentVisitor = void (*)(const char *entry, void *user);
const char *guest_environment_get(const char *name);
int guest_environment_set(const char *name, const char *value);
void guest_environment_visit(GuestEnvironmentVisitor visitor, void *user);

} // namespace x2::config
