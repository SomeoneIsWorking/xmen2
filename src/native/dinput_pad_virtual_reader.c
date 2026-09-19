#include "dinput_pad_virtual.h"

#include "x2_log.h"

#ifdef X2_WITH_SDL
#include "dinput_pad.h"
#include "dinput_pad_virtual_internal.h"

/*
 * What the READING thread sees at the first refresh after a press.
 *
 * The setter's own read-back says the gamepad is DOWN at the moment it
 * publishes, and the guest then reads that same pad tens of thousands of
 * times without once seeing it. Those two observations are made on different
 * threads and through different handles, so this reports the reader's side of
 * the same question from the thread that does the reading.
 *
 * It is triggered by the PRESS, not by finding one held, and it prints
 * whatever it finds. An instrument that only speaks when it sees the button
 * down cannot tell "the press was gone before I looked" from "I never
 * looked", and the first version of this one was silent for exactly that
 * reason.
 */
static unsigned long g_refreshes;
static unsigned long g_reported_press;
static int g_reports;

int dinput_pad_virtual_report_reader_view(void) {
  int i;
  int held = -1;

  g_refreshes++;
  if (!g_virt_js) {
    return -1;
  }
  for (i = 0; i < X2_VIRTUAL_BUTTON_COUNT; i++) {
    if (SDL_GetJoystickButton(g_virt_js, i) || g_vbtn_release_pending[i]) {
      held = i;
      break;
    }
  }
  if (g_vpad_presses == g_reported_press || g_reports >= 4) {
    return held;
  }
  g_reported_press = g_vpad_presses;
  g_reports++;
  if (held < 0) {
    x2_log_error(
        "DINPUT-PAD: reader view -- refresh %lu on thread %llu, the first "
        "after press %lu, and NO synthetic button is down or awaiting a "
        "reader. The press was gone before this thread looked.\n",
        g_refreshes, (unsigned long long)SDL_GetCurrentThreadID(),
        g_vpad_presses);
    return held;
  }
  x2_log_error("DINPUT-PAD: reader view -- refresh %lu on thread %llu, the "
               "first after press %lu: button %d (\"%s\") reads %d as a "
               "joystick button and %d through the game's own gamepad handle; "
               "release pending %d.\n",
               g_refreshes, (unsigned long long)SDL_GetCurrentThreadID(),
               g_vpad_presses, held, g_vbtn_name[held],
               (int)SDL_GetJoystickButton(g_virt_js, held),
               dinput_pad_open_gamepad_button(dinput_pad_virtual_slot(), held),
               g_vbtn_release_pending[held]);
  return held;
}
#else
int dinput_pad_virtual_report_reader_view(void) { return -1; }
#endif
