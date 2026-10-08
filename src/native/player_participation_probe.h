#pragma once

#include <cstddef>

struct X86pCpu;

namespace x2::native {

std::size_t player_participation_probe_report(struct X86pCpu *cpu, char *out,
                                              std::size_t size);

} // namespace x2::native
