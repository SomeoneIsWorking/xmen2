#ifndef X2_CUTSCENE_SKIP_PROBE_H
#define X2_CUTSCENE_SKIP_PROBE_H

#include <stddef.h>
#include <stdint.h>

struct CPU;

/* Append the exact retail cutscene-skip action and publication boundaries.
   `input_manager` is FUN_005d8920's result from the caller's guest-thread
   snapshot; no state is cached or changed here. */
size_t cutscene_skip_probe_report(struct CPU *cpu, unsigned controller,
                                  uint32_t input_manager,
                                  char *out, size_t size);

#endif
