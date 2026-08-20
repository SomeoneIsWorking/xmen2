#include "x87host.h"

#include <stdint.h>

typedef struct __attribute__((aligned(16))) {
    unsigned char bytes[512];
} FxState;

void x87_host_begin(CPU *C)
{
    if (C->depth != 0)
        x87_fault("ordinary host call reached with live x87 operands");
    __asm__ __volatile__("fninit" ::: "memory");
}

void x87_host_end(CPU *C)
{
    FxState state;
    long double values[8];
    unsigned int count = 0, i;
    uint8_t tags;

    __asm__ __volatile__("fxsave %0" : "=m"(state) :: "memory");
    tags = state.bytes[4];
    while (tags) {
        /* Do not initialize this long double in C before FSTP. Clang keeps
           the x87 zero used for that store live across the inline assembly,
           which puts it above the guest return and makes us capture zero. */
        __asm__ __volatile__("fstpt %0" : "=m"(values[count]) :: "memory");
        count++;
        tags &= (uint8_t)(tags - 1u);
    }
    __asm__ __volatile__("fninit" ::: "memory");
    for (i = count; i > 0; --i)
        x87_push(C, values[i - 1]);
}
