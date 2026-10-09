#pragma once

#include <cstddef>
#include <cstdint>

struct X86pCpu;

namespace x2::native {

/* Append the exact retail cutscene-skip action and publication boundaries.
   `input_manager` is FUN_005d8920's result from the caller's guest-thread
   snapshot; no state is cached or changed here. */
size_t cutscene_skip_probe_report(struct X86pCpu *cpu, unsigned controller,
                                  uint32_t input_manager, char *out,
                                  size_t size);

} // namespace x2::native
