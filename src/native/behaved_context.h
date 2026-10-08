#pragma once

#include <cstdint>

struct X86pCpu;

namespace x2::native {

/* Run one BehavEd context until it completes or an authored command suspends
 * it. The return value matches 004d8b30, including the pending-node high bits.
 */
std::uint32_t behaved_context_run(struct X86pCpu *cpu, std::uint32_t context);

/* Native thiscall replacement for XMen2.exe 004d8b30. */
void override_004d8b30(struct X86pCpu *cpu);

} // namespace x2::native
