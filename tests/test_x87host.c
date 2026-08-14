#include "x87host.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void x87_fault(const char *what)
{
    fprintf(stderr, "unexpected x87 fault: %s\n", what);
    __builtin_trap();
}

int main(void)
{
    CPU C;
    memset(&C, 0, sizeof C);
    C.top = 0;

    x87_host_begin(&C);
    x87_host_end(&C);
    if (C.depth != 0) {
        fprintf(stderr, "empty host x87 stack produced %d value(s)\n", C.depth);
        return 1;
    }

    x87_host_begin(&C);
    __asm__ __volatile__("fld1");
    x87_host_end(&C);
    if (C.depth != 1 || fabsl(X87_ST(&C, 0) - 1.0L) > 1e-12L) {
        fprintf(stderr, "ST0 return was not captured: depth=%d value=%Lf\n",
                C.depth, C.depth ? X87_ST(&C, 0) : 0.0L);
        return 1;
    }
    return 0;
}
