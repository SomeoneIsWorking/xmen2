#include "x86watch_stack.h"

void x86_watch_stack_report(FILE *out, uint32_t entry, uint32_t guest_esp,
                            uint32_t cpu_address, unsigned long cpu_size)
{
    uint32_t cpu_end = cpu_address + (uint32_t)cpu_size;

    fprintf(out,
            "[STACK] entry 0x%08x: guest_esp=0x%08x, this frame's CPU struct is "
            "0x%08x..0x%08x (%lu bytes)\n",
            entry, guest_esp, cpu_address, cpu_end, cpu_size);
    if (cpu_end <= guest_esp && guest_esp - cpu_end < 0x10000u)
        fprintf(out,
                "[STACK] SHARED STACK: the CPU struct ends %u bytes BELOW "
                "guest_esp, so guest pushes descend straight into this frame "
                "and any host call made from recompiled code runs its own "
                "frame over it.\n", (unsigned)(guest_esp - cpu_end));
    else if (cpu_address > guest_esp)
        fprintf(out,
                "[STACK] the CPU struct is ABOVE guest_esp by %u bytes -- guest "
                "pushes move away from it.\n",
                (unsigned)(cpu_address - guest_esp));
    else
        fprintf(out,
                "[STACK] SEPARATE STACKS: the CPU struct is %u bytes from "
                "guest_esp, far enough that they are different regions.\n",
                (unsigned)(guest_esp - cpu_end));
}
