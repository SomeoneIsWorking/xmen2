#include "x86_tail_policy.h"

X86TailRoute x86_tail_route(uint32_t dispatch_depth, uint32_t call_depth) {
  if (dispatch_depth != 0 && call_depth == dispatch_depth - 1u)
    return X86_TAIL_QUEUE;
  return X86_TAIL_INLINE;
}
