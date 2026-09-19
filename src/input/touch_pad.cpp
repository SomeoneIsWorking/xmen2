#include "touch_pad.h"

#include "touch_census.h"

#include "../config/settings.h"
#include "../config/settings_store.h"
#include "../native/dinput_pad.h"
#include "../native/dinput_pad_virtual.h"
#include "../native/host_touch.h"
#include "../native/x2_log.h"
#include "transient_controller_assignment.h"

#include <SDL3/SDL.h>

namespace x2::input::touch_pad {

namespace {
/* Touch devices this host reported when asked; -1 means it never was. A
   precondition nobody can read is one nobody can disprove. */
int g_host_devices = -1;
int g_host_capable = -1;
} // namespace

/*
 * Attach the overlay's pad NOW, while the guest can still see it.
 *
 * The guest enumerates game controllers once, shortly after the window
 * exists, and this port cannot make it enumerate again: naming its own
 * re-enumeration routine needs a symbol for a function XMen2.exe does not
 * export (issue #173). So a pad attached on the first finger -- which can
 * only ever be after the game has booted -- is a pad the game will never
 * poll. Measured in a browser: 48 contacts, 51 zone actions and 32 axis
 * changes published to a pad the game read a button from exactly 0 times.
 *
 * Attached for a host that can produce touch at all, not for one that has
 * used it: the overlay still appears only when a finger lands. A desktop
 * with no touchscreen gets no phantom controller, and ALWAYS -- which exists
 * so the layout is reachable without a touchscreen -- still gets its pad.
 */
void prepare_for_host() {
  const unsigned mode = x2_settings_store()->touch_controls;
  g_host_devices = x2_host_touch_devices();
  g_host_capable = x2_host_touch_capable();
  if (mode == X2_TOUCH_CONTROLS_OFF) {
    return;
  }
  if (mode != X2_TOUCH_CONTROLS_ALWAYS && !g_host_capable) {
    return;
  }
  ensure();
}

/*
 * Claim player one for the pad that touch publishes through.
 *
 * A pad only reaches the guest once a player resolves to it, and a player
 * resolves only from an explicit transient assignment or a persisted
 * reservation. On a phone neither exists on a first run, so every touch was
 * routed into a pad no player was reading: the probe reported the game
 * polling buttons that were never down, while SDL's own touch-to-mouse
 * emulation carried presses to menus and nothing to gameplay.
 *
 * A controller the player already chose keeps player one, so plugging a real
 * pad in still wins; this only fills the vacancy.
 */
/*
 * The pad the overlay publishes through.
 *
 * dinput_pad_virtual_set/release are the only way a touch press reaches the
 * guest, and for most of this port's life nothing attached that pad except
 * the X2_VIRTUAL_PAD diagnostic and the Android bridge doing it by hand. So
 * touch was dead on every other platform: the overlay drew, the zones lit up,
 * and each press was refused with "this run has no synthetic pad to press".
 * The touch owner attaches its own.
 *
 * Attempted once. A failure is not retried on every contact -- it would say
 * the same thing sixty times a second -- but it is counted, so the census
 * reports a live overlay with nowhere to publish rather than a tidy row of
 * refusals with no cause.
 */
/*
 * How many touch devices this host reports, or -1 before it was asked.
 *
 * Kept because it is the precondition for the pad below, and a precondition
 * nobody can read is a precondition nobody can disprove: "no contact arrived"
 * reads very differently on a host that has no touchscreen than on one that
 * has three.
 */

void ensure() {
  static bool attempted = false;
  if (attempted) {
    return;
  }
  attempted = true;
  if (dinput_pad_virtual_attach_for_touch()) {
    x2_touch_census()->pad_attached++;
    return;
  }
  x2_touch_census()->pad_attach_refused++;
  x2_log_error("touch: no synthetic gamepad could be attached, so the "
               "on-screen controls cannot reach gameplay in this run\n");
}

void claim_player_one() {
  static bool attempted = false;
  if (attempted)
    return;
  if (x2_transient_controller_has_assignment(0)) {
    x2_touch_census()->player_one_held_by_transient++;
    return;
  }
  /*
   * A STORED reservation only holds player one while the controller it names
   * is actually here.
   *
   * This used to test that the setting existed at all, which is not what
   * holds a player: x2_player_input_sync resolves a reservation through
   * dinput_pad_for_persistent_id and leaves the player unassigned when that
   * device is absent. So a phone whose owner had once paired a Bluetooth pad
   * kept a reservation nothing could satisfy, player one ended up with no
   * controller whatsoever, and touch declined to fill the vacancy it exists
   * to fill. Same resolver, same answer.
   */
  const char *const reserved =
      x2_settings_player_controller(x2_settings_store(), 0);
  if (reserved && dinput_pad_for_persistent_id(reserved) >= 0) {
    x2_touch_census()->player_one_held_by_setting++;
    return;
  }
  const int slot = dinput_pad_virtual_slot();
  if (slot < 0) {
    x2_touch_census()->player_one_no_slot++;
    return; /* Not opened yet; try again on the next contact. */
  }
  attempted = true;
  if (x2_transient_controller_assign(slot, 0)) {
    x2_touch_census()->player_one_claimed++;
  } else {
    x2_touch_census()->player_one_refused++;
    x2_log_error("touch: could not assign the touch pad (slot %d) "
                 "to player 1; touch will not reach gameplay\n",
                 slot);
  }
}

int host_devices() { return g_host_devices; }

int host_capable() { return g_host_capable; }

} // namespace x2::input::touch_pad
