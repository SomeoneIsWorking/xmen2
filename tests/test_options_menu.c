#include "options_menu.h"
#include "settings_overlay_state.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int native_stubs_registered(const char *module, uint32_t linked_ep);

static int check(int condition, const char *what)
{
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", what);
    return 1;
}

static int check_command(void (*command)(CPU *), const char *name)
{
    CPU C;
    int failures = 0;

    memset(&C, 0, sizeof C);
    C.eax = 0x12345678u;
    C.ecx = 0x23456789u;
    C.edx = 0x3456789au;
    C.esp = 0x00102000u;
    x2_settings_overlay_hide();

    command(&C);
    failures += check(x2_settings_overlay_visible(), name);
    failures += check(C.esp == 0x00102004u,
                      "options command must reproduce plain RET");
    failures += check(C.eax == 0x12345678u && C.ecx == 0x23456789u &&
                          C.edx == 0x3456789au,
                      "void options command must not invent guest registers");
    return failures;
}

int main(void)
{
    int failures = 0;

    failures += check(native_stubs_registered("XMen2.exe", 0x005f1c50u),
                      "retail `options` callback is not overridden");
    failures += check(native_stubs_registered("XMen2.exe", 0x005f1fa0u),
                      "retail `options_main` callback is not overridden");
    failures += check_command(x2_override_005f1c50,
                              "generic Options did not show RmlUi");
    failures += check_command(x2_override_005f1fa0,
                              "main-menu Options did not show RmlUi");

    x2_settings_overlay_hide();
    failures += check(!x2_settings_overlay_visible(),
                      "closing the overlay must release guest input");
    x2_settings_overlay_toggle();
    failures += check(x2_settings_overlay_visible(),
                      "the F1 visibility toggle must remain available");
    x2_settings_overlay_toggle();
    failures += check(!x2_settings_overlay_visible(),
                      "the second F1 toggle must release guest input");

    printf("options menu bridge: %d of 11 checks passed\n", 11 - failures);
    return failures != 0;
}
