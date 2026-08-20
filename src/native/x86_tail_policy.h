#ifndef X2_X86_TAIL_POLICY_H
#define X2_X86_TAIL_POLICY_H

#include <stdint.h>

typedef enum X86TailRoute {
    X86_TAIL_INLINE,
    X86_TAIL_QUEUE
} X86TailRoute;

/*
 * A generated tail jump at the current dispatch frame may return to the
 * dispatch loop. A tail jump reached through a deeper direct C call must run
 * before that caller resumes, or its guest return address remains on ESP.
 */
X86TailRoute x86_tail_route(uint32_t dispatch_depth, uint32_t call_depth);

#endif
