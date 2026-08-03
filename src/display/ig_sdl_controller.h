#ifndef X2_IG_SDL_CONTROLLER_H
#define X2_IG_SDL_CONTROLLER_H

#include <SDL_gamecontroller.h>

#include "ig_controller.h"

int x2_sdl_controller_init(x2_controller_manager *man);
void x2_sdl_controller_poll(x2_controller_manager *man);
int x2_sdl_controller_button_to_ig(SDL_GameControllerButton button);

#endif
