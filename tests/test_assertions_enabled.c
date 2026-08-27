#include <assert.h>
#include <stdio.h>

#ifdef NDEBUG
#error "Project test targets must compile with assertions enabled (issue #134)"
#endif

int main(void)
{
    int evaluated = 0;

    assert(++evaluated == 1);
    if (evaluated != 1) {
        fprintf(stderr, "assert expression was not evaluated\n");
        return 1;
    }

    puts("assertions_enabled: 1 assertion expression evaluated");
    return 0;
}
