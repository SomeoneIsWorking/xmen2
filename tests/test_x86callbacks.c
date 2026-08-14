#define _GNU_SOURCE
#include "x86callbacks.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

static uint32_t seen[4];
static int seen_count;

void x86_dispatch(CPU *C, uint32_t target)
{
    seen[seen_count++] = target;
    C->esp += 4u;
}

int main(void)
{
    CPU C;
    uint32_t *mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (mem == MAP_FAILED || (uintptr_t)mem > UINT32_MAX) {
        fprintf(stderr, "could not allocate 32-bit callback corpus\n");
        return 1;
    }
    memset(&C, 0, sizeof C);
    /* Leave real downward-growing stack space below the synthetic ESP.  The
       shipping adapter pushes one return word before each callback. */
    mem[64] = 0xabcdef01u;
    mem[4] = 0;
    mem[5] = 0x11111111u;
    mem[6] = 0x22222222u;
    C.esp = (uint32_t)(uintptr_t)&mem[64];
    mem[65] = (uint32_t)(uintptr_t)&mem[4];
    mem[66] = (uint32_t)(uintptr_t)&mem[7];

    x86_host_initterm(&C);
    if (seen_count != 2 || seen[0] != mem[5] || seen[1] != mem[6]) {
        fprintf(stderr, "_initterm scanned 3 slots but dispatched %d: "
                        "%08x %08x\n", seen_count, seen[0], seen[1]);
        return 1;
    }
    if (C.esp != (uint32_t)(uintptr_t)&mem[64]) {
        fprintf(stderr, "_initterm changed caller esp\n");
        return 1;
    }
    return 0;
}
