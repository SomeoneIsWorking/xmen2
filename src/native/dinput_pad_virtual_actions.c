#include "x2_log.h"
/* Input actions for the synthetic pad are separate from its lifecycle and
 * device-discovery code. Touch controls and the HTTP test channel use this
 * owner to publish and release virtual state. */
#include "dinput_pad_virtual.h"
#include "dinput_pad_virtual_internal.h"

#include "dinput_pad.h"
#include "dinput_pad_report.h"
#include "guest_clock.h"

#include <stdio.h>
#include <string.h>

/*
 * A press the game never READ did not happen.
 *
 * A finger down and the same finger up can arrive in one pump -- the browser
 * queues them and SDL hands both over before the guest polls again. Measured:
 * a touch press and its release were logged in the same millisecond with the
 * game reading a button 0 times in between, so all 48 published contacts were
 * invisible. A real thumb is slower than one poll; this makes the synthetic
 * one no faster, without inventing a duration. The ceiling in virtual_expire
 * bounds a press whose reader never comes.
 *
 * `wait_for_a_reader` is 0 for a press being WITHDRAWN -- a lost window, a
 * rotation, the controls being turned off. Nothing is owed to a reader there:
 * the press is being taken back, not completed.
 */
static int release_button(int i, int wait_for_a_reader) {
#ifdef X2_WITH_SDL
  X2PadPollCounts counts;
  dinput_pad_poll_counts(&counts);
  /* Only a button that is actually down is owed a look. A release for one
     nobody pressed used to take this branch too, which both left a deadline
     armed for a press that never happened and inflated the deferral count the
     beat reports -- 4 waited releases from 2 presses. */
  if (wait_for_a_reader && SDL_GetJoystickButton(g_virt_js, i) &&
      counts.button_reads == g_vbtn_reads_at_set[i]) {
    g_vbtn_release_pending[i] = 1;
    g_vbtn_until[i] = guest_clock_now_s() + X2_VIRTUAL_RELEASE_CEILING_S;
    g_vpad_releases_deferred++;
    return 1;
  }
  if (!SDL_SetJoystickVirtualButton(g_virt_js, i, false))
    return 0;
  g_vbtn_release_pending[i] = 0;
  g_vbtn_until[i] = 0.0;
  SDL_UpdateJoysticks();
  SDL_UpdateGamepads();
  return 1;
#else
  (void)i;
  (void)wait_for_a_reader;
  return 0;
#endif
}

int dinput_pad_virtual_release_now(const char *what) {
#ifdef X2_WITH_SDL
  int i;
  if (!g_virt_js || !what)
    return 0;
  for (i = 0; i < X2_VIRTUAL_BUTTON_COUNT; i++)
    if (!strcmp(what, g_vbtn_name[i]))
      return release_button(i, 0);
  return dinput_pad_virtual_release(what);
#else
  (void)what;
  return 0;
#endif
}

int dinput_pad_virtual_release(const char *what) {
#ifdef X2_WITH_SDL
  int i;
  if (!g_virt_js || !what)
    return 0;
  for (i = 0; i < X2_VIRTUAL_BUTTON_COUNT; i++) {
    if (!strcmp(what, g_vbtn_name[i])) {
      return release_button(i, 1);
    }
  }
  if (!strcmp(what, "up") || !strcmp(what, "down") || !strcmp(what, "left") ||
      !strcmp(what, "right")) {
    if (!SDL_SetJoystickVirtualHat(g_virt_js, 0, SDL_HAT_CENTERED))
      return 0;
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    return 1;
  }
  for (i = 0; i < X2_VIRTUAL_AXIS_COUNT; i++) {
    if (!strcmp(what, g_vaxis_name[i])) {
      const short rest = axis_is_trigger(i) ? trigger_raw(0.0) : 0;
      X2PadPollCounts counts;
      dinput_pad_poll_counts(&counts);
      if (g_vaxis_value[i] != rest &&
          counts.axis_reads == g_vaxis_reads_at_set[i]) {
        /* Same rule as a button, including its guard: a stick the game never
           sampled was never moved, and a stick already at rest is owed
           nothing. */
        g_vaxis_release_pending[i] = 1;
        g_vaxis_until[i] = guest_clock_now_s() + X2_VIRTUAL_RELEASE_CEILING_S;
        g_vpad_releases_deferred++;
        return 1;
      }
      if (!SDL_SetJoystickVirtualAxis(g_virt_js, i, rest))
        return 0;
      g_vaxis_value[i] = rest;
      g_vaxis_until[i] = 0.0;
      SDL_UpdateJoysticks();
      SDL_UpdateGamepads();
      return 1;
    }
  }
  return 0;
#else
  (void)what;
  return 0;
#endif
}

/* Release whatever has been held long enough. Called once a frame beside the
 * attach/detach schedule, so a press lasts real frames rather than one poll. */
void virtual_expire(void) {
#ifdef X2_WITH_SDL
  double now = guest_clock_now_s();
  int changed = 0;
  int i;
  if (!g_virt_js)
    return;
  {
    /* A deferred release lands as soon as the game has looked, which is what
       it was waiting for; the deadline below is only its ceiling. */
    X2PadPollCounts counts;
    dinput_pad_poll_counts(&counts);
    for (i = 0; i < X2_VIRTUAL_BUTTON_COUNT; i++)
      if (g_vbtn_release_pending[i] &&
          counts.button_reads != g_vbtn_reads_at_set[i]) {
        g_vbtn_release_pending[i] = 0;
        g_vbtn_until[i] = 0.0;
        SDL_SetJoystickVirtualButton(g_virt_js, i, false);
        changed = 1;
      }
    for (i = 0; i < X2_VIRTUAL_AXIS_COUNT; i++)
      if (g_vaxis_release_pending[i] &&
          counts.axis_reads != g_vaxis_reads_at_set[i]) {
        const short rest = axis_is_trigger(i) ? trigger_raw(0.0) : 0;
        g_vaxis_release_pending[i] = 0;
        g_vaxis_until[i] = 0.0;
        g_vaxis_value[i] = rest;
        SDL_SetJoystickVirtualAxis(g_virt_js, i, rest);
        changed = 1;
      }
  }
  for (i = 0; i < X2_VIRTUAL_BUTTON_COUNT; i++)
    if (g_vbtn_until[i] != 0.0 && now >= g_vbtn_until[i]) {
      double held = now - g_vbtn_until[i];
      g_vbtn_until[i] = 0.0;
      g_vbtn_release_pending[i] = 0;
      g_vbtn_clears++;
      if (g_vbtn_clears <= 4)
        x2_log_error("DINPUT-PAD: releasing button %d, %.3fs past "
                     "its deadline (clear #%lu)\n",
                     i, held, g_vbtn_clears);
      SDL_SetJoystickVirtualButton(g_virt_js, i, false);
      changed = 1;
    }
  for (i = 0; i < X2_VIRTUAL_AXIS_COUNT; i++)
    if (g_vaxis_until[i] != 0.0 && now >= g_vaxis_until[i]) {
      short rest = axis_is_trigger(i) ? trigger_raw(0.0) : 0;
      g_vaxis_until[i] = 0.0;
      g_vaxis_release_pending[i] = 0;
      g_vaxis_value[i] = rest;
      SDL_SetJoystickVirtualAxis(g_virt_js, i, rest);
      changed = 1;
    }
  /* Setting a virtual button or axis only records what the device SHOULD
     report; nothing reads differently until SDL latches it. Every other
     release path pumps these two -- this one did not, so a deferred release
     stayed down until some unrelated call happened to latch it. */
  if (changed) {
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
  }
#endif
}
