#include "x2_log.h"
/* Building the synthetic SDL device itself: its descriptor, its gamepad
 * mapping, opening it and putting its axes at rest. Separate from the
 * lifecycle policy that decides WHEN a pad should exist and from the action
 * owner that presses it, because this is the only part that talks to SDL's
 * virtual-joystick API and the only part that has to be right about the
 * joystick-versus-gamepad button orders. */
#include "dinput_pad.h"
#include "dinput_pad_virtual.h"
#include "dinput_pad_virtual_internal.h"

#include <stdio.h>
#include <string.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>

void virtual_attach(void) {
  SDL_VirtualJoystickDesc desc;
  SDL_JoystickID jid;
  SDL_GUID g;
  char gs[64], map[600];

  if (!dinput_pad_subsystem_start()) {
    x2_log_error("DINPUT-PAD: X2_VIRTUAL_PAD is set but SDL's gamepad "
                 "subsystem would not start (%s). NO pad is attached.\n",
                 SDL_GetError());
    return;
  }
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  /* A deterministic Xbox 360 identity makes the synthetic device exercise
     the same prompt-family path as the hardware it models. */
  desc.vendor_id = 0x045e;
  desc.product_id = 0x028e;
  desc.name = "X2 Virtual Xbox 360 Pad";
  desc.naxes = 6;
  desc.nbuttons = 11;
  desc.nhats = 1;
  if ((jid = SDL_AttachVirtualJoystick(&desc)) == 0) {
    x2_log_error("DINPUT-PAD: X2_VIRTUAL_PAD is set but "
                 "SDL_AttachVirtualJoystick failed (%s). NO pad is "
                 "attached -- the run continues WITHOUT one rather than "
                 "pretending.\n",
                 SDL_GetError());
    return;
  }
  g = SDL_GetJoystickGUIDForID(jid);
  SDL_GUIDToString(g, gs, sizeof gs);
  {
    /*
     * The mapping string is BUILT FROM g_vbtn_name, not written beside it.
     *
     * A virtual joystick is driven by JOYSTICK button index, while the
     * game-facing names are GAMEPAD buttons, and the two orders differ:
     * this mapping puts start at b5, where SDL's own enum has GUIDE at 5
     * and START at 6. Written out twice, the pair drifts and every press
     * lands one button off -- which looks exactly like "the controller
     * does nothing in this menu". One table, two readers.
     */
    size_t n = 0;
    int i;
    n += (size_t)snprintf(map + n, sizeof map - n, "%s,X2 Virtual Pad,", gs);
    for (i = 0; i < X2_VIRTUAL_BUTTON_COUNT; i++)
      n += (size_t)snprintf(map + n, sizeof map - n, "%s:b%d,", g_vbtn_name[i],
                            i);
    snprintf(map + n, sizeof map - n,
             "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,"
             "leftx:a0,lefty:a1,rightx:a2,righty:a3,"
             "lefttrigger:a4,righttrigger:a5,");
  }
  {
    /*
     * What SDL actually did with it. A mapping that is silently rejected
     * leaves the pad enumerating perfectly and reading UP forever, which
     * is indistinguishable from a game that ignores the pad -- so the
     * return code and the mapping SDL ENDS UP USING are both reported,
     * not just the fact that a mapping was offered.
     */
    int rc = SDL_AddGamepadMapping(map);
    x2_log_error("DINPUT-PAD: mapping offered (%zu bytes) -> %s\n", strlen(map),
                 rc < 0 ? SDL_GetError() : (rc ? "added" : "updated existing"));
  }
  g_virt_id = jid;
  g_virt_js = SDL_OpenJoystick(jid);
  if (!g_virt_js)
    x2_log_error("DINPUT-PAD: the synthetic pad attached but could NOT be "
                 "opened (%s), so nothing can press its buttons. It will "
                 "enumerate and read as all-zero forever.\n",
                 SDL_GetError());
  if (g_virt_js) {
    /* PUT THE TRIGGERS AT REST. A fresh virtual axis is 0, and SDL maps a
       trigger's whole signed travel onto 0..32767 -- so a pad that has
       never been touched presents BOTH triggers half squeezed. On the
       shared Z axis they then cancel, which is why it looked fine: the
       axis read centred while every individual trigger binding, including
       the one this port's preset puts `Power` on, resolved to nothing. */
    int i;
    for (i = 0; i < 6; i++)
      if (axis_is_trigger(i)) {
        g_vaxis_value[i] = trigger_raw(0.0);
        SDL_SetJoystickVirtualAxis(g_virt_js, i, trigger_raw(0.0));
      }
    SDL_UpdateJoysticks();
    x2_log_error("DINPUT-PAD: synthetic triggers set to their REST "
                 "value %d (a fresh virtual axis reads 0, which SDL "
                 "reports as a trigger held half down)\n",
                 (int)trigger_raw(0.0));
  }
  dinput_pad_refresh();
  {
    char *m = SDL_GetGamepadMappingForID(jid);
    x2_log_error("DINPUT-PAD: SDL_IsGamepad=%d; the mapping IN FORCE is: "
                 "%s\n",
                 (int)SDL_IsGamepad(jid), m ? m : "(none)");
    if (m)
      SDL_free(m);
  }
  x2_log_error("DINPUT-PAD: attached on thread %llu\n",
               (unsigned long long)SDL_GetCurrentThreadID());
  x2_log_error("DINPUT-PAD: X2_VIRTUAL_PAD -- a SYNTHETIC gamepad is "
               "attached. Nothing in this run's controller behaviour came "
               "from real hardware, and this line is here so that cannot "
               "be mistaken.\n");
}
#endif /* X2_WITH_SDL */
