#pragma once

#include <cstdint>

struct X86pCpu;

namespace x2::native {

void dinput_script_apply(struct X86pCpu *cpu, uint32_t out, uint32_t size);

} // namespace x2::native
