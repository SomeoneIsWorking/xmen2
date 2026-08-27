#ifndef X2_CUTSCENE_SCRIPT_AUDIO_H
#define X2_CUTSCENE_SCRIPT_AUDIO_H

struct CPU;

typedef struct CutsceneScriptAudioSnapshot {
    unsigned long ordinary_commands;
    unsigned long silent_commands;
    unsigned last_context;
} CutsceneScriptAudioSnapshot;

void cutscene_script_audio_snapshot(CutsceneScriptAudioSnapshot *out);
void x2_override_004a7130(struct CPU *cpu);

#endif
