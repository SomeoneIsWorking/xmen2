#pragma once

#include <cstdint>

struct X86pCpu;

namespace x2::native {

/* Native override for libIGSg.dll!0x10034d10: igAttrStack::customReset */
void override_10034d10(struct X86pCpu *C);

/* Native override for libIGSg.dll!0x10034d30: igAttrStackManager::reset */
void override_10034d30(struct X86pCpu *C);

/* Pure fast implementation for a single igAttrStack */
void attr_stack_custom_reset(std::uint32_t stack);

} // namespace x2::native
