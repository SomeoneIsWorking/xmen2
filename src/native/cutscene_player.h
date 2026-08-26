#ifndef X2_CUTSCENE_PLAYER_H
#define X2_CUTSCENE_PLAYER_H

#include <stdint.h>

struct CPU;

typedef struct CutscenePlayerSnapshot {
    uint32_t sequence;
    unsigned active;
    unsigned owned_contexts;
    unsigned event_window_active;
    unsigned event_refused;
    uint32_t event_owner;
    unsigned control_state;
    unsigned requests;
    unsigned invocations;
    unsigned completions;
    unsigned long input_polls;
    unsigned long input_edges;
    unsigned long authored_steps;
    unsigned long conversation_payloads;
    unsigned long behaved_steps;
    unsigned long event_steps;
    uint32_t last_event_target;
    uint32_t last_event_descriptor;
    unsigned long allocations;
    unsigned long inherited;
    unsigned long freed;
    unsigned long releases;
    unsigned long private_releases;
    unsigned long same_frame;
    unsigned long same_guest_time;
    unsigned long event_insertion_faults;
    unsigned long results[6];
} CutscenePlayerSnapshot;

/* Read-only publication for the live input probe. */
void cutscene_player_snapshot(struct CPU *cpu, CutscenePlayerSnapshot *out);

#endif
