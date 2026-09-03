/*
 * Does SDL's virtual joystick round-trip a button press at all?
 *
 * The port's synthetic pad enumerates perfectly, the game polls it 71,700
 * times a run, and every read comes back released. Inside the game that has
 * many possible causes; here it has one. This attaches a virtual pad exactly
 * as dinput_pad.c does, presses a button, and requires BOTH layers to see it:
 * the joystick itself, and the gamepad the mapping produces.
 *
 * It also requires the button to come back UP after release, so a test cannot
 * pass on a stuck-down device that never changes.
 *
 * IT CREATES A HIDDEN WINDOW FIRST, and that is the whole point of the test
 * rather than a detail. The first version of this file passed while the game
 * failed, and it passed because it had NO window: SDL drops joystick button
 * state when no window holds keyboard focus, but a process with no windows at
 * all is not subject to that. The game has a hidden window, which can never
 * take focus, so it lost every button while its axes kept working. A test that
 * cannot reproduce the defect it is meant to guard is worse than no test -- it
 * says the code is fine.
 */
#include <SDL3/SDL.h>
#include <stdio.h>

#define VBTN 10
static const char *const NAME[VBTN] = {"a",
                                       "b",
                                       "x",
                                       "y",
                                       "back",
                                       "start",
                                       "leftstick",
                                       "rightstick",
                                       "leftshoulder",
                                       "rightshoulder"};

int main(void) {
  SDL_VirtualJoystickDesc desc;
  SDL_JoystickID jid;
  SDL_Joystick *js;
  SDL_Gamepad *gp;
  SDL_GUID g;
  SDL_Window *win;
  char gs[64], map[600];
  size_t n = 0;
  int i, fails = 0;

  /* The port sets this because SDL's focus policy silently eats buttons;
     the test must run under the SAME policy or it tests a different
     program. Removing this line must make this test FAIL. */
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    fprintf(stderr, "SKIP: no SDL gamepad subsystem here (%s)\n",
            SDL_GetError());
    return 77; /* ctest SKIP, not a false pass */
  }
  /* The condition the game runs under: a window exists and never has focus. */
  win = SDL_CreateWindow("x2 virtual pad test", 64, 64, SDL_WINDOW_HIDDEN);
  if (!win)
    fprintf(stderr,
            "note: no window (%s); this test is WEAKER without one "
            "-- the defect it guards only appears when a window "
            "exists and lacks focus.\n",
            SDL_GetError());

  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.vendor_id = 0x045e;
  desc.product_id = 0x028e;
  desc.name = "X2 Virtual Xbox 360 Pad";
  desc.naxes = 6;
  desc.nbuttons = VBTN;
  desc.nhats = 1;
  if ((jid = SDL_AttachVirtualJoystick(&desc)) == 0) {
    fprintf(stderr, "FAIL: SDL_AttachVirtualJoystick: %s\n", SDL_GetError());
    return 1;
  }
  g = SDL_GetJoystickGUIDForID(jid);
  SDL_GUIDToString(g, gs, sizeof gs);
  n += (size_t)snprintf(map + n, sizeof map - n, "%s,X2 Virtual Pad,", gs);
  for (i = 0; i < VBTN; i++)
    n += (size_t)snprintf(map + n, sizeof map - n, "%s:b%d,", NAME[i], i);
  snprintf(map + n, sizeof map - n,
           "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,"
           "leftx:a0,lefty:a1,rightx:a2,righty:a3,"
           "lefttrigger:a4,righttrigger:a5,");
  SDL_AddGamepadMapping(map);

  js = SDL_OpenJoystick(jid);
  gp = SDL_OpenGamepad(jid);
  if (!js || !gp) {
    fprintf(stderr, "FAIL: open joystick=%p gamepad=%p: %s\n", (void *)js,
            (void *)gp, SDL_GetError());
    return 1;
  }

  for (i = 0; i < VBTN; i++) {
    int jdown, gdown;
    SDL_GamepadButton gb = SDL_GetGamepadButtonFromString(NAME[i]);

    if (!SDL_SetJoystickVirtualButton(js, i, true)) {
      fprintf(stderr, "FAIL: set %s (b%d): %s\n", NAME[i], i, SDL_GetError());
      fails++;
      continue;
    }
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    jdown = SDL_GetJoystickButton(js, i) ? 1 : 0;
    gdown = SDL_GetGamepadButton(gp, gb) ? 1 : 0;

    if (!jdown || !gdown) {
      fprintf(stderr, "FAIL: %-14s b%d -> joystick %s, gamepad(enum %d) %s\n",
              NAME[i], i, jdown ? "DOWN" : "UP", (int)gb,
              gdown ? "DOWN" : "UP");
      fails++;
    } else {
      printf("  ok  %-14s b%d -> joystick DOWN, gamepad DOWN\n", NAME[i], i);
    }

    /* And it must come back UP: a device stuck down would pass above. */
    SDL_SetJoystickVirtualButton(js, i, false);
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    if (SDL_GetJoystickButton(js, i)) {
      fprintf(stderr, "FAIL: %s stayed DOWN after release\n", NAME[i]);
      fails++;
    }
  }

  SDL_CloseGamepad(gp);
  SDL_CloseJoystick(js);
  SDL_DetachVirtualJoystick(jid);
  if (win)
    SDL_DestroyWindow(win);
  SDL_Quit();
  printf("virtual pad round-trip: %s (%d failure(s))\n",
         fails ? "FAILED" : "PASSED", fails);
  return fails ? 1 : 0;
}
