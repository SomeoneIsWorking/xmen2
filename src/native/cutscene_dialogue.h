#ifndef X2_CUTSCENE_DIALOGUE_H
#define X2_CUTSCENE_DIALOGUE_H

struct X86pCpu;

typedef struct CutsceneDialogueSnapshot {
  unsigned long advances;
  unsigned long active_voice_stops;
  unsigned long ordinary_response_starts;
  unsigned long ordinary_line_starts;
  unsigned long suppressed_response_starts;
  unsigned long suppressed_line_starts;
  unsigned long skip_presentation_starts;
  unsigned last_manager;
  unsigned last_stopped_handle;
  unsigned last_line_presenter;
} CutsceneDialogueSnapshot;

/* Consume one deterministic conversation payload without presenting its
 * dialogue. The retail response transition still owns scripts and cleanup. */
void cutscene_dialogue_skip_begin(void);
void cutscene_dialogue_skip_end(struct X86pCpu *cpu);
int cutscene_dialogue_advance(struct X86pCpu *cpu);
int cutscene_dialogue_payload_active(void);
void cutscene_dialogue_snapshot(CutsceneDialogueSnapshot *out);

#endif
