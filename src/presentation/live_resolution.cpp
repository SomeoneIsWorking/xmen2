#include "live_resolution.h"

#include "../native/ui_text_scale.h"
#include "d3d8_live_resolution.h"
#include "display_geometry.h"
#include "display_mode_runtime.h"
#include "resolution_ladder.h"
#include "settings_store.h"
#include "window_settings.h"

#include <stdio.h>

void x2_live_resolution_select_next(X2Settings *settings) {
  unsigned display_w = 0, display_h = 0;
  unsigned height;

  if (!settings)
    return;
  /* An unavailable display is not a failure here: the ladder assumes 16:9,
     which is what the game shipped with and what the stored default already
     is. Refusing to change resolution because SDL has no display would strand
     the setting. */
  x2_display_pixel_size(&display_w, &display_h);
  height = x2_resolution_next_height(settings->height, display_h);
  settings->width =
      (uint16_t)x2_resolution_width_for(height, display_w, display_h);
  settings->height = (uint16_t)height;
}

static void report_rollback(char *why, int whyn, const char *failure,
                            int window_ok, const char *window_why, int d3d_ok,
                            const char *d3d_why) {
  if (!why || whyn <= 0)
    return;
  if (window_ok && d3d_ok) {
    snprintf(why, (size_t)whyn, "%s; previous resolution restored", failure);
    return;
  }
  snprintf(why, (size_t)whyn, "%s; rollback failed (%s%s%s)", failure,
           window_ok ? "" : window_why, !window_ok && !d3d_ok ? "; " : "",
           d3d_ok ? "" : d3d_why);
}

static void rollback(struct SDL_Window *window, X2Settings *settings,
                     const X2Settings *before, const char *failure, char *why,
                     int whyn) {
  char window_why[192] = "window rollback failed";
  char d3d_why[192] = "D3D8 rollback failed";
  char title_why[192] = "title display rollback failed";
  int window_ok, title_ok, d3d_ok;

  *settings = *before;
  window_ok = x2_window_settings_apply(window, before, window_why,
                                       (int)sizeof window_why);
  title_ok = x2_display_mode_runtime_apply(before->width, before->height,
                                           title_why, (int)sizeof title_why);
  d3d_ok = d3d8_live_resolution_apply(before->width, before->height, d3d_why,
                                      (int)sizeof d3d_why);
  if (!title_ok && why && whyn > 0) {
    snprintf(why, (size_t)whyn, "%s; rollback failed (%s)", failure, title_why);
    return;
  }
  report_rollback(why, whyn, failure, window_ok, window_why, d3d_ok, d3d_why);
}

int x2_live_resolution_apply(struct SDL_Window *window, X2Settings *settings,
                             const X2Settings *before, char *why, int whyn) {
  char failure[256];

  if (!window || !settings || !before) {
    if (why && whyn > 0)
      snprintf(why, (size_t)whyn,
               "window, settings and previous settings are required");
    return 0;
  }
  if (!d3d8_live_resolution_apply(settings->width, settings->height, failure,
                                  (int)sizeof failure)) {
    *settings = *before;
    if (why && whyn > 0)
      snprintf(why, (size_t)whyn, "%s", failure);
    return 0;
  }
  if (!x2_display_mode_runtime_apply(settings->width, settings->height, failure,
                                     (int)sizeof failure)) {
    char d3d_why[192] = "D3D8 rollback failed";
    int d3d_ok = d3d8_live_resolution_apply(before->width, before->height,
                                            d3d_why, (int)sizeof d3d_why);

    *settings = *before;
    if (why && whyn > 0) {
      if (d3d_ok)
        snprintf(why, (size_t)whyn, "%s", failure);
      else
        snprintf(why, (size_t)whyn, "%s; rollback failed (%s)", failure,
                 d3d_why);
    }
    return 0;
  }
  if (!x2_window_settings_apply(window, settings, failure,
                                (int)sizeof failure)) {
    rollback(window, settings, before, failure, why, whyn);
    return 0;
  }
  if (!x2_settings_store_save(failure, (int)sizeof failure)) {
    rollback(window, settings, before, failure, why, whyn);
    return 0;
  }
  /* Fonts already in memory are not reloaded by a resolution change, so the
     text would otherwise keep the size the boot resolution asked for. */
  x2_ui_text_scale_reapply();
  if (why && whyn > 0)
    snprintf(why, (size_t)whyn, "Saved; game renders at %ux%u now",
             settings->width, settings->height);
  return 1;
}
