#include "touch_census.h"

#include "gameplay_control.h"
#include "touch_source.h"

#include "../config/settings.h"
#include "../config/settings_store.h"

extern "C" {
#include "../native/guest_clock.h"
}

#include <lucent/log_c.h>

namespace {
X2TouchCensus g_census;
} // namespace

X2TouchCensus *x2_touch_census(void) { return &g_census; }

void x2_touch_census_read(X2TouchCensus *out) {
  if (out)
    *out = g_census;
}

void x2_touch_census_report(int has_window) {
  const unsigned long contacts_seen =
      g_census.contacts_down + g_census.contacts_moved + g_census.contacts_up +
      g_census.contacts_canceled;
  const unsigned mode = x2_settings_store()->touch_controls;
  const char *const mode_name = mode == X2_TOUCH_CONTROLS_ALWAYS ? "ALWAYS"
                                : mode == X2_TOUCH_CONTROLS_OFF  ? "OFF"
                                                                 : "AUTO";
  /*
   * A run in which nothing was touched and a run in which every touch was
   * thrown away both end with zero presses. Saying which, by name, is the
   * whole reason this exists: on a phone or in a browser this text is the
   * only account of the feature anyone gets.
   */
  if (contacts_seen == 0) {
    lucent_log_info(
        "touch",
        "no contact reached the port this run -- touch_controls=%s, "
        "source says %s, %s. Nothing was dropped; nothing arrived",
        mode_name, x2_touch_source_is_touch() ? "touch" : "not touch",
        has_window ? "a window was present" : "there was NO window");
    return;
  }
  lucent_log_info(
      "touch",
      "%lu contact event(s): %lu down, %lu moved, %lu up, %lu canceled",
      contacts_seen, g_census.contacts_down, g_census.contacts_moved,
      g_census.contacts_up, g_census.contacts_canceled);
  lucent_log_info(
      "touch",
      "%lu of %lu dropped before routing: %lu with no window, %lu with the "
      "overlay hidden (touch_controls=%s, source says %s, gate %s)",
      g_census.ignored_no_window + g_census.ignored_overlay_hidden,
      contacts_seen, g_census.ignored_no_window,
      g_census.ignored_overlay_hidden, mode_name,
      x2_touch_source_is_touch() ? "touch" : "not touch",
      x2_gameplay_control_name(
          (int)x2_gameplay_control_state(guest_clock_now_s())));
  lucent_log_info("touch",
                  "%lu zone action(s) routed; %lu cancellation(s) for a lost "
                  "window, rotation or layout change",
                  g_census.zone_presses, g_census.cancellations);
  /*
   * The far end. A press the pad refused never reached the guest, and a pad
   * no player was assigned to is one the guest never polls -- the two ways
   * this feature has actually failed on a device.
   */
  lucent_log_info("touch",
                  "published to the pad: %lu button change(s) (%lu refused), "
                  "%lu axis change(s) (%lu refused)",
                  g_census.buttons_published, g_census.buttons_refused,
                  g_census.axes_published, g_census.axes_refused);
  if (g_census.player_one_claimed == 0 && g_census.player_one_refused == 0) {
    lucent_log_info("touch",
                    "player one already had a controller, so the touch pad "
                    "was not claimed for it%s",
                    g_census.buttons_published || g_census.axes_published
                        ? " -- if nothing moved, that controller is what the "
                          "guest is reading"
                        : "");
  } else {
    lucent_log_info("touch", "the touch pad was %s for player one",
                    g_census.player_one_claimed
                        ? "claimed"
                        : "REFUSED -- touch cannot reach "
                          "gameplay in this run");
  }
}
