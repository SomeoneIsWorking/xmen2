#include "x86_tail_policy.h"

#include <stdio.h>

typedef struct TailCase {
    const char *name;
    uint32_t dispatch_depth;
    uint32_t call_depth;
    X86TailRoute expected;
} TailCase;

static int expect_route(const TailCase *test)
{
    X86TailRoute actual = x86_tail_route(test->dispatch_depth,
                                         test->call_depth);
    if (actual == test->expected) return 0;
    fputs(test->name, stderr);
    fputs(": tail route did not match the dispatch/call depth contract\n",
          stderr);
    return 1;
}

int main(void)
{
    static const TailCase cases[] = {
        {"outside a dispatch", 0, 0, X86_TAIL_INLINE},
        {"outer dispatch frame", 1, 0, X86_TAIL_QUEUE},
        {"one direct call deeper", 1, 1, X86_TAIL_INLINE},
        {"nested dispatch frame", 4, 3, X86_TAIL_QUEUE},
        {"nested direct call", 4, 4, X86_TAIL_INLINE}
    };
    size_t i;
    int fails = 0;

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++)
        fails += expect_route(&cases[i]);

    return fails != 0;
}
