#include "x87crt.h"

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static jmp_buf fault_jmp;
static const char *fault_text;

void x87_fault(const char *what)
{
    fault_text = what;
    longjmp(fault_jmp, 1);
}

static int positive(void)
{
    CPU c;
    memset(&c, 0, sizeof c);
    c.esp = 0x1000;
    x87_crt_push(&c, 2.0L);
    x87_crt_push(&c, 8.0L);
    x87_crt_cipow(&c);
    if (c.depth != 1 || c.esp != 0x1004 || fabsl(c.st[c.top] - 256.0L) > 1e-12L) {
        fprintf(stderr, "x87crt positive: depth=%d esp=%08x result=%.18Lg\n",
                c.depth, c.esp, c.st[c.top]);
        return 1;
    }
    puts("x87crt positive: _CIpow consumed 2 operands and produced 1 result");
    x87_crt_push(&c, -17.75L);
    x87_crt_ftol(&c);
    if (c.depth != 1 || c.esp != 0x1008 || c.eax != 0xffffffefu
        || c.edx != 0xffffffffu) {
        fprintf(stderr, "x87crt positive: ftol depth=%d esp=%08x edx:eax=%08x:%08x\n",
                c.depth, c.esp, c.edx, c.eax);
        return 1;
    }
    puts("x87crt positive: ftol consumed 1 operand and returned EDX:EAX");
    return 0;
}

static int negative(void)
{
    CPU c;
    memset(&c, 0, sizeof c);
    fault_text = NULL;
    if (!setjmp(fault_jmp)) {
        (void)x87_crt_pop(&c);
        fputs("x87crt negative: empty stack was silently accepted\n", stderr);
        return 1;
    }
    if (!fault_text || !strstr(fault_text, "underflow")) {
        fprintf(stderr, "x87crt negative: wrong fault: %s\n",
                fault_text ? fault_text : "none");
        return 1;
    }
    puts("x87crt negative: empty stack was rejected as underflow");
    return 0;
}

int main(void)
{
    return positive() || negative();
}
