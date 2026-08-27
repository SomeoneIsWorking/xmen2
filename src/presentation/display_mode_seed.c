/*
 * Publishes the port's output size into the game's own registry at boot.
 * See display_mode_seed.h for the boundary and its provenance; this file
 * owns only the decision, the write and the announcement. The guest does the
 * rest: it parses "Resolution", validates the mode against what the adapter
 * enumerates, and builds its device -- which is how the port's resolution
 * reaches game-space rendering without anyone rewriting engine state behind
 * it.
 */
#include "display_mode_seed.h"

#include <stdio.h>
#include <string.h>

#include "advapi32.h"
#include "settings_store.h"

/* The exact store path and value name, as the retail engine itself writes
   them -- observed verbatim in a run's registry.txt:
   HKEY_CURRENT_USER\Software\Activision\X-Men Legends 2\Settings\Display|Resolution|1|38303078363030
   The engine formats the value with the "%dx%d" at XMen2.exe 0x006a4b80, so
   "<w>x<h>" below is the encoding it parses, not a choice. */
#define X2_DISPLAY_KEY \
    "HKEY_CURRENT_USER\\Software\\Activision\\X-Men Legends 2\\Settings" \
    "\\Display"
#define X2_DISPLAY_VALUE "Resolution"

static uint32_t g_published_width, g_published_height;

int x2_display_mode_seed_format(unsigned w, unsigned h,
                                char *out_value, int cap)
{
    int length;

    if (!out_value || cap <= 0 || !w || !h || w > 16384u || h > 16384u)
        return 0;
    length = snprintf(out_value, (size_t)cap, "%ux%u", w, h);
    return length >= 0 && length < cap;
}

int x2_display_mode_seed_plan(const char *stored, unsigned w, unsigned h,
                              char *out_value, int cap)
{
    if (!x2_display_mode_seed_format(w, h, out_value, cap)) return 0;
    return !stored || strcmp(stored, out_value) != 0;
}

int x2_display_mode_seed_is_current(void)
{
    const X2Settings *settings = x2_settings_store();
    char stored[32];
    char expected[32];

    return x2_display_mode_seed_format(settings->width, settings->height,
                                      expected, (int)sizeof expected)
        && advapi32_host_get_string(X2_DISPLAY_KEY, X2_DISPLAY_VALUE,
                                    stored, (int)sizeof stored)
        && strcmp(stored, expected) == 0;
}

int x2_display_mode_seed_publish(void)
{
    X2Settings *settings = x2_settings_store();
    char before[32];
    char value[32];

    int had_before = advapi32_host_get_string(
        X2_DISPLAY_KEY, X2_DISPLAY_VALUE, before, (int)sizeof before);

    if (!x2_display_mode_seed_format(settings->width, settings->height,
                                    value, (int)sizeof value))
        return 0;
    if (had_before && strcmp(before, value) == 0) {
        g_published_width = settings->width;
        g_published_height = settings->height;
        return 0;
    }
    if (!advapi32_host_set_string(X2_DISPLAY_KEY, X2_DISPLAY_VALUE, value))
        return 0;
    g_published_width = settings->width;
    g_published_height = settings->height;
    return 1;
}

void x2_display_mode_seed_boot(void)
{
    X2Settings *settings = x2_settings_store();
    int acted = x2_display_mode_seed_publish();
    int current = x2_display_mode_seed_is_current();

    /* This early write supplies the warm-profile path. Retail first-run
       initialization may install its own 800x600 default afterwards;
       display_mode_runtime.c reconciles that exact settings-load boundary. */
    if (current) {
        fprintf(stderr, "DISPLAY SEED: %s video %ux%u against retail "
                        "Display\\Resolution; the runtime settings-load "
                        "bridge will verify the parsed value.\n",
                acted ? "published" : "no change needed for",
                settings->width, settings->height);
    } else {
        fprintf(stderr, "DISPLAY SEED: REFUSED video %ux%u; retail "
                        "Display\\Resolution does not contain the configured "
                        "mode.\n", settings->width, settings->height);
    }
}

uint32_t x2_display_mode_seed_width(void)  { return g_published_width; }
uint32_t x2_display_mode_seed_height(void) { return g_published_height; }
