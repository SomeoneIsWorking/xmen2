#define _GNU_SOURCE
#include "x86watch_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static char *capture(void)
{
    char *text = NULL;
    size_t size = 0;
    FILE *out = open_memstream(&text, &size);
    x86_watch_trace_dump(out);
    fclose(out);
    return text;
}

static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

int main(void)
{
    char *text;

    x86_watch_trace_reset();
    text = capture();
    check(strstr(text, "boundary ring is EMPTY") != NULL,
          "empty trace reports the negative and its meaning");
    free(text);

    x86_watch_trace_note(0, 0x55b470, 0x70001000, 7);
    x86_watch_trace_note(1, 0xfecf30, 0x70000ffc, 7);
    text = capture();
    check(strstr(text, "last 2 of 2") != NULL,
          "nonempty trace reports its denominator");
    check(strstr(text, "ENTER guest  addr=0x0055b470") <
          strstr(text, "CALL host    addr=0x00fecf30"),
          "trace preserves oldest-first boundary order");
    free(text);

    if (failures) return 1;
    puts("x86watch trace: negative and ordered cases passed");
    return 0;
}
