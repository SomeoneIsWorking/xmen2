#ifndef X2_CUTSCENE_EVENT_PLAYER_H
#define X2_CUTSCENE_EVENT_PLAYER_H

#include <stdint.h>

struct CPU;

/* XMen2.exe FUN_004b2990/FUN_004b2a10 bound the callback pool at 0x465
 * records. Each record is 0x18 bytes and has one entry in both allocator
 * bitsets. */
enum {
    CUTSCENE_EVENT_PLAYER_CAPACITY = 0x465u,
    CUTSCENE_EVENT_PLAYER_SLOT_WORDS =
        (CUTSCENE_EVENT_PLAYER_CAPACITY + 31u) / 32u
};

typedef enum CutsceneEventPlayerStep {
    CUTSCENE_EVENT_PLAYER_STEP_REFUSED = -1,
    CUTSCENE_EVENT_PLAYER_STEP_NONE = 0,
    CUTSCENE_EVENT_PLAYER_STEP_RAN = 1,
    CUTSCENE_EVENT_PLAYER_STEP_RAN_CORRUPT = 2
} CutsceneEventPlayerStep;

typedef struct CutsceneEventOwnershipWindow {
    uint32_t owner;
    uint32_t excluded[CUTSCENE_EVENT_PLAYER_SLOT_WORDS];
    uint32_t owned[CUTSCENE_EVENT_PLAYER_SLOT_WORDS];
    uint32_t reported[CUTSCENE_EVENT_PLAYER_SLOT_WORDS];
    uint8_t active;
} CutsceneEventOwnershipWindow;

typedef int (*CutsceneEventInsertionOwner)(const struct CPU *cpu,
                                           void *opaque);

/* The event owner is learned from a validated ordinary FUN_004b2d70 call. */
uint32_t cutscene_event_player_captured_owner(void);

/* Snapshot all currently queued slots as excluded. Returns zero when the
 * ordinary pump has not established a valid owner, and -1 for corrupt state. */
int cutscene_event_player_window_begin(CutsceneEventOwnershipWindow *window);

/* Claim slots which became queued since begin/the previous claim. Existing
 * unowned slots remain excluded. Returns the number newly claimed, or -1. */
int cutscene_event_player_window_claim_new(
    CutsceneEventOwnershipWindow *window);

/* Observe the exact FUN_004b2b40 insertion seam. This may be armed before an
 * ordinary pump has exposed the owner: the first validated insertion binds
 * the window, snapshots its pre-insertion slots, then consults owns_current. */
int cutscene_event_player_watch_insertions(
    CutsceneEventOwnershipWindow *window,
    CutsceneEventInsertionOwner owns_current, void *opaque);
void cutscene_event_player_unwatch_insertions(
    CutsceneEventOwnershipWindow *window);
unsigned long cutscene_event_player_insertion_faults(void);

/* True only while a callback already owned by the active window executes. */
int cutscene_event_player_executing_owned(void);

/* Read-only selection of the minimum-deadline queued slot owned by window.
 * Returns one with slot filled, zero when none exists, and -1 on refusal. */
int cutscene_event_player_next_owned(
    const CutsceneEventOwnershipWindow *window, uint32_t *slot);

/* Remove and execute exactly this queued owned callback, independent of its
 * deadline. Unowned deadline/slot pairs are preserved byte-for-byte. */
CutsceneEventPlayerStep cutscene_event_player_step_owned_slot(
    struct CPU *cpu, CutsceneEventOwnershipWindow *window, uint32_t slot);

CutsceneEventPlayerStep cutscene_event_player_step_owned(
    struct CPU *cpu, CutsceneEventOwnershipWindow *window);

/* Native thiscall replacement for XMen2.exe FUN_004b2d70(owner, now). */
void x2_override_004b2d70(struct CPU *cpu);
void x2_override_004b2b40(struct CPU *cpu);

#endif
