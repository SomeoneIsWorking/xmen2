#include "x2native_options.h"

#include <stdio.h>

static int check(int condition, const char *message)
{
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(void)
{
    X2NativeOptions o;
    char *plain[] = { "x2native" };
    char *headless[] = { "x2native", "--no-window" };
    char *bringup[] = { "x2native", "--run" };
    char *diagnostic[] = { "x2native", "--selftest" };
    int fails = 0;

    fails += check(x2native_options_parse(1, plain, &o) == 0,
                   "zero-argument parse failed");
    fails += check(o.run && o.d3d8 && o.window && o.product &&
                   o.input_record && !o.input_record[0],
                   "zero arguments did not select the inspectable, recorded "
                   "SDL3 GPU game");
    fails += check(x2native_options_parse(2, headless, &o) == 0,
                   "headless parse failed");
    fails += check(o.run && o.d3d8 && !o.window && o.product,
                   "--no-window replaced the product mode instead of extending it");
    fails += check(x2native_options_parse(2, bringup, &o) == 0,
                   "bring-up parse failed");
    fails += check(o.run && !o.d3d8,
                   "explicit --run no longer selects renderer-free bring-up");
    fails += check(x2native_options_parse(2, diagnostic, &o) == 0,
                   "selftest parse failed");
    fails += check(o.selftest && !o.run && !o.d3d8,
                   "a diagnostic was replaced by the default product");
    printf("x2native options: %d of 8 checks passed\n", 8 - fails);
    return fails ? 1 : 0;
}
