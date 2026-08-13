/* CPU-only discriminator tests for X2_RECORD_ARM.  Each CTest case gets a new
 * process because the shipping recorder deliberately initializes only once. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "x86rt.h"

void x86_record_range(uint32_t lo, uint32_t hi);
void x86_record_report(void);

/* Recording an unreadable guest stack is a valid, explicitly reported state;
 * this harness has no mapped guest address space. */
int x86_peek(uint32_t addr, void *dst, size_t n)
{
    (void)addr; (void)dst; (void)n;
    return 0;
}

int main(int argc, char **argv)
{
    CPU C = {0};
    if (argc != 2) {
        fprintf(stderr, "usage: %s invalid|missing|never|capture\n", argv[0]);
        return 2;
    }

    x86_record_range(0x1000, 0x10ff);
    if (!strcmp(argv[1], "invalid")) {
        setenv("X2_RECORD_ARM", "0x10oops", 1);
    } else if (!strcmp(argv[1], "missing")) {
        setenv("X2_RECORD_ARM", "0x2000", 1);
    } else if (!strcmp(argv[1], "never")) {
        setenv("X2_RECORD_ARM", "0x1004", 1);
        x86_record(0x1000, &C, "before arm");
    } else if (!strcmp(argv[1], "capture")) {
        setenv("X2_RECORD_ARM", "0x1004", 1);
        x86_record(0x1000, &C, "before arm");
        x86_record(0x1004, &C, "arm");
        x86_record(0x1008, &C, "after arm");
    } else {
        fprintf(stderr, "unknown case: %s\n", argv[1]);
        return 2;
    }

    x86_record_report();
    return 0;
}
