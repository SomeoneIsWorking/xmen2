#ifndef X2_CONVERSATION_CUTSCENE_SKIP_H
#define X2_CONVERSATION_CUTSCENE_SKIP_H

#include <stddef.h>
#include <stdint.h>

#include "conversation_skip_policy.h"

struct CPU;

/* `slot` and `input` are the retail objects already resolved by update(). */
int conversation_cutscene_skip_should_advance(struct CPU *cpu, uint32_t self,
                                               uint32_t slot, uint32_t input);

/* The retail update returns before the action gate while the conversation is
 * hidden/disabled.  Observe that path so an armed sequence is retired as soon
 * as authored cleanup restores controls. */
void conversation_cutscene_skip_observe_inactive(uint32_t self);

/* Start a newly authorized manual/Continue sequence with no stale script
 * owner from the previous sequence. */
void conversation_cutscene_skip_begin_sequence(void);

/* Classify the current response through the same guest layout used by manual
 * skip and automatic Continue resume. */
ConversationSkipResponse conversation_cutscene_skip_response(
    struct CPU *cpu, uint32_t self);

/* True only while the exact script context claimed by waittimed remains the
 * bounded owner of the current fast-forward sequence. */
int conversation_cutscene_skip_owned_sequence_active(void);

/* Exposed for the focused ABI/state test; registered as a runtime override. */
void x2_override_004d9130(struct CPU *cpu);

/* Live, passive classification through the same production snapshot/policy. */
size_t conversation_cutscene_skip_probe(struct CPU *cpu, char *out,
                                        size_t size);

#endif
