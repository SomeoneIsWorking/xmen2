#include "../native/x2_log.h"
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
#include "display_geometry.h"
#include "resolution_ladder.h"
#include "settings_store.h"

/* The exact store path and value name, as the retail engine itself writes
   them -- observed verbatim in a run's registry.txt:
   HKEY_CURRENT_USER\Software\Activision\X-Men Legends
   2\Settings\Display|Resolution|1|38303078363030 The engine formats the value
   with the "%dx%d" at XMen2.exe 0x006a4b80, so
   "<w>x<h>" below is the encoding it parses, not a choice. */
namespace x2::presentation {

#define X2_DISPLAY_KEY                                                         \
  "HKEY_CURRENT_USER\\Software\\Activision\\X-Men Legends 2\\Settings"         \
  "\\Display"
#define X2_DISPLAY_VALUE "Resolution"

namespace {

uint32_t g_published_width, g_published_height;

} // namespace

int display_mode_seed_format(unsigned w, unsigned h, char *out_value, int cap) {
  int length;

  if (!out_value || cap <= 0 || !w || !h || w > 16384u || h > 16384u)
    return 0;
  length = snprintf(out_value, (size_t)cap, "%ux%u", w, h);
  return length >= 0 && length < cap;
}

int display_mode_seed_plan(const char *stored, unsigned w, unsigned h,
                           char *out_value, int cap) {
  if (!display_mode_seed_format(w, h, out_value, cap))
    return 0;
  return !stored || strcmp(stored, out_value) != 0;
}

int display_mode_seed_is_current() {
  const X2Settings *settings = x2_settings_store();
  char stored[32];
  char expected[32];

  return display_mode_seed_format(settings->width, settings->height, expected,
                                  (int)sizeof expected) &&
         advapi32_host_get_string(X2_DISPLAY_KEY, X2_DISPLAY_VALUE, stored,
                                  (int)sizeof stored) &&
         strcmp(stored, expected) == 0;
}

int display_mode_seed_publish() {
  X2Settings *settings = x2_settings_store();
  char before[32];
  char value[32];

  int had_before = advapi32_host_get_string(X2_DISPLAY_KEY, X2_DISPLAY_VALUE,
                                            before, (int)sizeof before);

  if (!display_mode_seed_format(settings->width, settings->height, value,
                                (int)sizeof value))
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

/* The setting IS a height; the stored width is the one derived on the
   display where it was last chosen. A default or a moved profile carries a
   width for another shape -- the 1280x720 default pillarboxed on a 2728x1264
   phone -- so the width is re-derived from the display this run has before
   the mode is published. An unknown display (a headless run) keeps it. */
namespace {

void fit_display(X2Settings *settings) {
  unsigned display_w = 0, display_h = 0;

  if (!x2_display_pixel_size(&display_w, &display_h))
    return;
  settings->width =
      (uint16_t)x2_resolution_width_for(settings->height, display_w, display_h);
}

} // namespace

void display_mode_seed_boot() {
  X2Settings *settings = x2_settings_store();
  int acted, current;

  fit_display(settings);
  acted = display_mode_seed_publish();
  current = display_mode_seed_is_current();

  /* This early write supplies the warm-profile path. Retail first-run
     initialization may install its own 800x600 default afterwards;
     display_mode_runtime.cpp reconciles that exact settings-load boundary. */
  if (current) {
    x2_log_error("DISPLAY SEED: %s video %ux%u against retail "
                 "Display\\Resolution; the runtime settings-load "
                 "bridge will verify the parsed value.\n",
                 acted ? "published" : "no change needed for", settings->width,
                 settings->height);
  } else {
    x2_log_error("DISPLAY SEED: REFUSED video %ux%u; retail "
                 "Display\\Resolution does not contain the configured "
                 "mode.\n",
                 settings->width, settings->height);
  }
}

uint32_t display_mode_seed_width() { return g_published_width; }
uint32_t display_mode_seed_height() { return g_published_height; }

} // namespace x2::presentation
