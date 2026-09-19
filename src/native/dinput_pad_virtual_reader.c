#include "dinput_pad_virtual.h"

#include "x2_log.h"

#ifdef X2_WITH_SDL
#include "dinput_pad.h"
#include "dinput_pad_virtual_internal.h"

/*
 * What the READING thread sees while a press is held.
 *
 * The setter's own read-back says the gamepad is DOWN at the moment it
 * publishes, and the guest then reads that same pad tens of thousands of
 * times without once seeing it. Those two observations are made on different
 * threads and through different handles, so this reports the reader's side of
 * exactly the same question, once, from the thread that does the reading.
 *
 * The negative is the point: if no synthetic button is ever held at a refresh,
 * that is a different finding from one that is held and reads up, and it says
 * so rather than staying quiet.
 */
static unsigned long g_refreshes;
static int g_said_held;
static int g_said_never;

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
  if (held < 0) {
    if (!g_said_never && g_refreshes == 20000) {
      g_said_never = 1;
      x2_log_error(
          "DINPUT-PAD: reader view -- %lu refresh(es) on thread %llu and NOT "
          "ONCE was a synthetic button down or awaiting a reader at one. The "
          "press is gone before the reading thread ever looks.\n",
          g_refreshes, (unsigned long long)SDL_GetCurrentThreadID());
    }
    return -1;
  }
  if (g_said_held) {
    return held;
  }
  g_said_held = 1;
  x2_log_error("DINPUT-PAD: reader view -- on thread %llu, handle %p, button "
               "%d (\"%s\") reads %d as a joystick button and %d through the "
               "game's own gamepad handle; release pending %d.\n",
               (unsigned long long)SDL_GetCurrentThreadID(), (void *)g_virt_js,
               held, g_vbtn_name[held],
               (int)SDL_GetJoystickButton(g_virt_js, held),
               dinput_pad_open_gamepad_button(dinput_pad_virtual_slot(), held),
               g_vbtn_release_pending[held]);
  return held;
}
#else
int dinput_pad_virtual_report_reader_view(void) { return -1; }
#endif
