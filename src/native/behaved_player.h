#ifndef X2_BEHAVED_PLAYER_H
#define X2_BEHAVED_PLAYER_H

#include <stdint.h>

struct CPU;

typedef int (*BehavedPlayerOwnsContext)(uint32_t context, void *opaque);

typedef enum BehavedPlayerStep {
    BEHAVED_PLAYER_STEP_REFUSED = -1,
    BEHAVED_PLAYER_STEP_NONE = 0,
    BEHAVED_PLAYER_STEP_RAN = 1,
    BEHAVED_PLAYER_STEP_COMPLETED = 2
} BehavedPlayerStep;

/* Read-only selection. Returns -1 for corrupt/unreadable scheduler state,
 * zero when no accepted context is scheduled, and one with `context` filled
 * for the minimum-deadline accepted entry. */
int behaved_player_next_owned(struct CPU *cpu,
                              BehavedPlayerOwnsContext owns, void *opaque,
                              uint32_t *context);

/* Resume exactly this scheduled context, independent of its deadline. */
BehavedPlayerStep behaved_player_step_context(struct CPU *cpu,
                                              uint32_t context);

/* Resume the earliest scheduled BehavEd context accepted by `owns`, without
 * consulting or changing its guest deadline. Exactly one context is resumed;
 * callers repeat until their authored ownership boundary is complete. */
BehavedPlayerStep behaved_player_step_owned(
    struct CPU *cpu, BehavedPlayerOwnsContext owns, void *opaque);

/* Native thiscall replacement for XMen2.exe FUN_004d9640. */
void x2_override_004d9640(struct CPU *cpu);

#endif
