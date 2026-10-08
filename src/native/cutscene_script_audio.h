#pragma once

struct X86pCpu;

namespace x2::native {

struct CutsceneScriptAudioSnapshot {
  unsigned long ordinary_commands;
  unsigned long silent_commands;
  unsigned last_context;
};

void cutscene_script_audio_snapshot(CutsceneScriptAudioSnapshot *out);
void override_004a7130(struct X86pCpu *cpu);

} // namespace x2::native
