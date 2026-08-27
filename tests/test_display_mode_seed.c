/* The boot-time publication of video.width x video.height into the retail
   Display\Resolution value, exercised through its shipping owner. The
   encoding it produces is what XMen2.exe itself parses ("%dx%d" at
   0x006a4b80), so a wrong separator or swapped operands here would publish a
   value the engine reads as something else. The store and settings bounds
   are stubs; the decision, encoding and call are the production code. */
#include "display_mode_seed.h"
#include "settings.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int checks;
#define CHECK(c) do { assert(c); checks++; } while (0)

static X2Settings g_settings_stub;
X2Settings *x2_settings_store(void) { return &g_settings_stub; }

static char g_stored[32];
static int g_has_stored;
static int g_set_calls;
static char g_set_value[32];

int advapi32_host_get_string(const char *path, const char *name,
                             char *out, int cap)
{
    (void)path; (void)name;
    if (!g_has_stored) return 0;
    snprintf(out, (size_t)cap, "%s", g_stored);
    return 1;
}

int advapi32_host_set_string(const char *path, const char *name,
                             const char *value)
{
    (void)path; (void)name;
    g_set_calls++;
    snprintf(g_set_value, sizeof g_set_value, "%s", value);
    snprintf(g_stored, sizeof g_stored, "%s", value);
    g_has_stored = 1;
    return 1;
}

int main(void)
{
    char value[32];

    /* Absent store: publish. The encoding is the retail one, lowercase x. */
    CHECK(x2_display_mode_seed_plan(NULL, 1280, 720, value,
                                    (int)sizeof value) == 1);
    CHECK(strcmp(value, "1280x720") == 0);

    /* Same size already stored: nothing to do. */
    CHECK(x2_display_mode_seed_plan("1280x720", 1280, 720, value,
                                    (int)sizeof value) == 0);

    /* Different stored size: republish over it -- that override is policy. */
    CHECK(x2_display_mode_seed_plan("800x600", 1920, 1080, value,
                                    (int)sizeof value) == 1);
    CHECK(strcmp(value, "1920x1080") == 0);

    /* Empty string counts as absent. */
    CHECK(x2_display_mode_seed_plan("", 2560, 1440, value,
                                    (int)sizeof value) == 1);
    CHECK(strcmp(value, "2560x1440") == 0);

    /* Dimensions that cannot be an output size are refused, not formatted. */
    CHECK(x2_display_mode_seed_plan(NULL, 0, 720, value,
                                    (int)sizeof value) == 0);
    CHECK(x2_display_mode_seed_plan(NULL, 1280, 0, value,
                                    (int)sizeof value) == 0);
    CHECK(x2_display_mode_seed_plan(NULL, 99999, 720, value,
                                    (int)sizeof value) == 0);

    /* publish() reads the settings store and writes the planned value once;
       a second call over an unchanged store is a no-op, so a boot that runs
       it twice cannot flip-flop the guest's own setting. */
    g_settings_stub.width = 1600;
    g_settings_stub.height = 900;
    g_has_stored = 0;
    g_set_calls = 0;
    CHECK(x2_display_mode_seed_publish() == 1);
    CHECK(g_set_calls == 1);
    CHECK(strcmp(g_set_value, "1600x900") == 0);
    CHECK(x2_display_mode_seed_is_current() == 1);
    CHECK(x2_display_mode_seed_publish() == 0);
    CHECK(g_set_calls == 1);

    snprintf(g_stored, sizeof g_stored, "%s", "800x600");
    CHECK(x2_display_mode_seed_is_current() == 0);
    CHECK(x2_display_mode_seed_publish() == 1);
    CHECK(x2_display_mode_seed_is_current() == 1);

    printf("test_display_mode_seed: %d publication checks passed\n", checks);
    return 0;
}
