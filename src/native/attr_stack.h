#ifndef X2_ATTR_STACK_H
#define X2_ATTR_STACK_H

#include <stdint.h>

struct X86pCpu;

#ifdef __cplusplus
extern "C" {
#endif

/* Native override for libIGSg.dll!0x10034d10: igAttrStack::customReset */
void x2_override_10034d10(struct X86pCpu *C);

/* Native override for libIGSg.dll!0x10034d30: igAttrStackManager::reset */
void x2_override_10034d30(struct X86pCpu *C);

/* Pure fast implementation for a single igAttrStack */
void attr_stack_custom_reset(uint32_t stack);

#ifdef __cplusplus
}
#endif

#endif /* X2_ATTR_STACK_H */
