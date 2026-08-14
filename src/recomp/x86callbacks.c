#include "x86callbacks.h"

#include <stdio.h>
#include <stdlib.h>

void x86_host_initterm(CPU *C)
{
    uint32_t first = RD32(C->esp + 4u);
    uint32_t last = RD32(C->esp + 8u);
    uint32_t slot;

    if (last < first || ((last - first) & 3u) != 0) {
        fprintf(stderr, "_initterm: invalid callback range 0x%08x..0x%08x\n",
                first, last);
        abort();
    }
    for (slot = first; slot < last; slot += 4u) {
        uint32_t target = RD32(slot);
        uint32_t before = C->esp;
        if (!target)
            continue;
        C->esp -= 4u;
        WR32(C->esp, RD32(before));
        x86_dispatch(C, target);
        if (C->esp != before) {
            fprintf(stderr, "_initterm: callback 0x%08x changed esp "
                            "0x%08x -> 0x%08x\n", target, before, C->esp);
            abort();
        }
    }
}
