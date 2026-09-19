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

void x2_touch_census_report(const char *tag, int has_window) {
  const char *const prefix = tag ? tag : "";
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
    /* The gate belongs here as much as in the busy line, and for longer:
       this is the branch a phone or a browser tab sits in for the whole of
       the logo and the loading route, and "the controls are not on screen
       yet" is the answer someone waiting to tap one actually needs. Leaving
       it out made a real run unreadable -- 93 heartbeats that could not say
       whether the port had reached gameplay. */
    lucent_log_info(
        "touch",
        "%sno contact reached the port this run -- touch_controls=%s, "
        "source says %s, gate %s, %s. Nothing was dropped; nothing arrived",
        prefix, mode_name, x2_touch_source_is_touch() ? "touch" : "not touch",
        x2_gameplay_control_name(
            (int)x2_gameplay_control_state(guest_clock_now_s())),
        has_window ? "a window was present" : "there was NO window");
    return;
  }
  lucent_log_info(
      "touch",
      "%s%lu contact event(s): %lu down, %lu moved, %lu up, %lu canceled",
      prefix, contacts_seen, g_census.contacts_down, g_census.contacts_moved,
      g_census.contacts_up, g_census.contacts_canceled);
  lucent_log_info(
      "touch",
      "%s%lu of %lu dropped before routing: %lu with no window, %lu with the "
      "overlay hidden (touch_controls=%s, source says %s, gate %s)",
      prefix, g_census.ignored_no_window + g_census.ignored_overlay_hidden,
      contacts_seen, g_census.ignored_no_window,
      g_census.ignored_overlay_hidden, mode_name,
      x2_touch_source_is_touch() ? "touch" : "not touch",
      x2_gameplay_control_name(
          (int)x2_gameplay_control_state(guest_clock_now_s())));
  lucent_log_info("touch",
                  "%s%lu zone action(s) routed; %lu cancellation(s) for a lost "
                  "window, rotation or layout change",
                  prefix, g_census.zone_presses, g_census.cancellations);
  /*
   * The far end. A press the pad refused never reached the guest, and a pad
   * no player was assigned to is one the guest never polls -- the two ways
   * this feature has actually failed on a device.
   */
  if (g_census.pad_attach_refused) {
    lucent_log_info("touch",
                    "%sTHERE IS NO PAD: the overlay asked for a synthetic "
                    "gamepad %lu time(s) and did not get one, so every press "
                    "below reached nothing",
                    prefix, g_census.pad_attach_refused);
  } else if (g_census.pad_attached == 0) {
    lucent_log_info("touch",
                    "%sthe overlay has not asked for a pad yet -- nothing has "
                    "been routed to one",
                    prefix);
  }
  lucent_log_info("touch",
                  "%spublished to the pad: %lu button change(s) (%lu refused), "
                  "%lu axis change(s) (%lu refused)",
                  prefix, g_census.buttons_published, g_census.buttons_refused,
                  g_census.axes_published, g_census.axes_refused);
  if (g_census.player_one_claimed == 0 && g_census.player_one_refused == 0) {
    /* Say which of the three it was, by the count that was actually taken. */
    if (g_census.player_one_held_by_transient) {
      lucent_log_info("touch",
                      "%sa controller chosen in this run already holds player "
                      "one (%lu time(s) asked), so the touch pad was not "
                      "claimed for it -- if nothing moves, THAT controller is "
                      "what the guest is reading",
                      prefix, g_census.player_one_held_by_transient);
    } else if (g_census.player_one_held_by_setting) {
      lucent_log_info("touch",
                      "%sa stored controller reservation holds player one (%lu "
                      "time(s) asked), so the touch pad was not claimed for it "
                      "-- clear it in the settings to play by touch",
                      prefix, g_census.player_one_held_by_setting);
    } else if (g_census.player_one_no_slot) {
      lucent_log_info("touch",
                      "%sthe touch pad had no inventory slot when player one "
                      "was asked for (%lu time(s)), so nothing was claimed and "
                      "touch cannot reach gameplay",
                      prefix, g_census.player_one_no_slot);
    } else {
      lucent_log_info("touch",
                      "%splayer one was never asked for -- no contact reached "
                      "the publish path, so this says nothing about who owns "
                      "the player",
                      prefix);
    }
  } else {
    lucent_log_info("touch", "%sthe touch pad was %s for player one", prefix,
                    g_census.player_one_claimed
                        ? "claimed"
                        : "REFUSED -- touch cannot reach "
                          "gameplay in this run");
  }
}
