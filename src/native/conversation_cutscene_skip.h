#ifndef X2_CONVERSATION_CUTSCENE_SKIP_H
#define X2_CONVERSATION_CUTSCENE_SKIP_H

#include <stddef.h>
#include <stdint.h>

struct CPU;

/* `slot` and `input` are the retail objects already resolved by update(). */
int conversation_cutscene_skip_should_advance(struct CPU *cpu, uint32_t self,
                                               uint32_t slot, uint32_t input);

/* The retail update returns before the action gate while the conversation is
 * hidden/disabled.  Observe that path so an armed sequence is retired as soon
 * as authored cleanup restores controls. */
void conversation_cutscene_skip_observe_inactive(uint32_t self);

/* Live, passive classification through the same production snapshot/policy. */
size_t conversation_cutscene_skip_probe(struct CPU *cpu, char *out,
                                        size_t size);

#endif
