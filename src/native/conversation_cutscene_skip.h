#ifndef X2_CONVERSATION_CUTSCENE_SKIP_H
#define X2_CONVERSATION_CUTSCENE_SKIP_H

#include <stddef.h>
#include <stdint.h>

struct CPU;

/* `slot` is the retail FUN_00456440 result already computed by update(). */
int conversation_cutscene_skip_should_advance(struct CPU *cpu, uint32_t self,
                                               uint32_t slot,
                                               int action20_down);

/* Live, passive classification through the same production snapshot/policy. */
size_t conversation_cutscene_skip_probe(struct CPU *cpu, char *out,
                                        size_t size);

#endif
