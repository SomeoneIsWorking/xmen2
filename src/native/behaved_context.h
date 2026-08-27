#ifndef X2_BEHAVED_CONTEXT_H
#define X2_BEHAVED_CONTEXT_H

#include <stdint.h>

struct CPU;

/* Run one BehavEd context until it completes or an authored command suspends
 * it. The return value matches 004d8b30, including the pending-node high bits. */
uint32_t behaved_context_run(struct CPU *cpu, uint32_t context);

/* Native thiscall replacement for XMen2.exe 004d8b30. */
void x2_override_004d8b30(struct CPU *cpu);

#endif
