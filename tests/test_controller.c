#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>

#include "ig_controller.h"
#include "ig_sdl_controller.h"

static int s_connect_count;
static int s_disconnect_count;
static int s_connected_ids[8];

static void on_connect(x2_controller_manager *man, x2_controller *c)
{
    (void)man;
    s_connected_ids[s_connect_count++] = c->id;
}

static void on_disconnect(x2_controller_manager *man, x2_controller *c)
{
    (void)man;
    (void)c;
    ++s_disconnect_count;
}

static void test_mapping(void)
{
    assert(x2_sdl_controller_button_to_ig(SDL_GAMEPAD_BUTTON_SOUTH) == X2_BUTTON_RIGHT_PAD_DOWN);
    assert(x2_sdl_controller_button_to_ig(SDL_GAMEPAD_BUTTON_EAST) == X2_BUTTON_RIGHT_PAD_RIGHT);
    assert(x2_sdl_controller_button_to_ig(SDL_GAMEPAD_BUTTON_WEST) == X2_BUTTON_RIGHT_PAD_LEFT);
    assert(x2_sdl_controller_button_to_ig(SDL_GAMEPAD_BUTTON_NORTH) == X2_BUTTON_RIGHT_PAD_UP);
    assert(x2_sdl_controller_button_to_ig(SDL_GAMEPAD_BUTTON_BACK) == X2_BUTTON_SELECT);
    assert(x2_sdl_controller_button_to_ig(SDL_GAMEPAD_BUTTON_START) == X2_BUTTON_START);
    assert(x2_sdl_controller_button_to_ig(SDL_GAMEPAD_BUTTON_LEFT_STICK) == X2_BUTTON_LEFT_JOYSTICK_BUTTON);
    assert(x2_sdl_controller_button_to_ig(SDL_GAMEPAD_BUTTON_RIGHT_STICK) == X2_BUTTON_RIGHT_JOYSTICK_BUTTON);
    assert(x2_sdl_controller_button_to_ig(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) == X2_BUTTON_UPPER_LEFT_TRIGGER);
    assert(x2_sdl_controller_button_to_ig(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) == X2_BUTTON_UPPER_RIGHT_TRIGGER);
    assert(x2_sdl_controller_button_to_ig(SDL_GAMEPAD_BUTTON_DPAD_UP) == X2_BUTTON_LEFT_PAD_UP);
    assert(x2_sdl_controller_button_to_ig(SDL_GAMEPAD_BUTTON_DPAD_DOWN) == X2_BUTTON_LEFT_PAD_DOWN);
    assert(x2_sdl_controller_button_to_ig(SDL_GAMEPAD_BUTTON_DPAD_LEFT) == X2_BUTTON_LEFT_PAD_LEFT);
    assert(x2_sdl_controller_button_to_ig(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) == X2_BUTTON_LEFT_PAD_RIGHT);
    for (int i = X2_BUTTON_SELECT; i < X2_BUTTON_16; ++i) {
        if (i == X2_BUTTON_LOWER_LEFT_TRIGGER || i == X2_BUTTON_LOWER_RIGHT_TRIGGER) {
            continue;
        }
        int found = 0;
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
            if (x2_sdl_controller_button_to_ig((SDL_GamepadButton)b) == i) {
                found = 1;
                break;
            }
        }
        assert(found);
    }
    printf("mapping: ok\n");
}

static void test_button_state(void)
{
    x2_controller_manager man;
    x2_controller_manager_init(&man);
    x2_controller_manager_set_callbacks(&man, on_connect, on_disconnect, NULL);
    x2_controller *c = x2_controller_manager_add(&man);
    assert(c);
    assert(x2_controller_manager_get_count(&man) == 1);
    assert(s_connect_count == 1);

    x2_controller_set_button_state(c, X2_BUTTON_START, 1);
    x2_controller_set_button_state(c, X2_BUTTON_RIGHT_PAD_DOWN, 1);
    assert(x2_controller_get_button_state(c, X2_BUTTON_START));
    assert(x2_controller_get_button_state(c, X2_BUTTON_RIGHT_PAD_DOWN));
    assert(!x2_controller_get_button_state(c, X2_BUTTON_SELECT));
    assert(x2_controller_get_buttons_state(c) ==
           (1u << X2_BUTTON_START) | (1u << X2_BUTTON_RIGHT_PAD_DOWN));

    x2_controller_set_button_state(c, X2_BUTTON_START, 0);
    assert(!x2_controller_get_button_state(c, X2_BUTTON_START));
    assert(x2_controller_get_button_state(c, X2_BUTTON_RIGHT_PAD_DOWN));
    printf("button state: ok\n");

    x2_controller_manager_shutdown(&man);
}

static void test_pressure(void)
{
    x2_controller_manager man;
    x2_controller_manager_init(&man);
    x2_controller *c = x2_controller_manager_add(&man);
    x2_controller_set_button_pressure(c, X2_BUTTON_LOWER_LEFT_TRIGGER, 0.75f);
    x2_controller_set_button_state(c, X2_BUTTON_LOWER_LEFT_TRIGGER, 1);
    assert(x2_controller_get_button_pressure(c, X2_BUTTON_LOWER_LEFT_TRIGGER) > 0.74f);
    assert(x2_controller_get_button_pressure(c, X2_BUTTON_LOWER_LEFT_TRIGGER) < 0.76f);
    assert(x2_controller_get_button_pressure(c, X2_BUTTON_SELECT) == 0.0f);
    printf("pressure: ok\n");
    x2_controller_manager_shutdown(&man);
}

static void test_joystick(void)
{
    x2_controller_manager man;
    x2_controller_manager_init(&man);
    x2_controller *c = x2_controller_manager_add(&man);
    x2_controller_set_joystick(c, 0, -0.5f, 0.25f);
    x2_controller_set_joystick(c, 1, 1.0f, -1.0f);
    float x, y;
    x2_controller_get_joystick(c, 0, &x, &y);
    assert(x == -0.5f && y == 0.25f);
    x2_controller_get_joystick(c, 1, &x, &y);
    assert(x == 1.0f && y == -1.0f);
    printf("joystick: ok\n");
    x2_controller_manager_shutdown(&man);
}

static void test_hotswap(void)
{
    x2_controller_manager man;
    x2_controller_manager_init(&man);
    x2_controller_manager_set_callbacks(&man, on_connect, on_disconnect, NULL);

    x2_controller *c0 = x2_controller_manager_add(&man);
    x2_controller *c1 = x2_controller_manager_add(&man);
    assert(c0 && c1);
    assert(x2_controller_manager_get_count(&man) == 2);
    c0->connected = 1;
    c1->connected = 1;
    c0->impl = (void *)1;
    c0->impl_id = 10;
    c1->impl = (void *)2;
    c1->impl_id = 11;

    x2_controller *found = x2_controller_manager_find_by_impl(&man, 11);
    assert(found == c1);

    x2_controller_manager_remove(&man, 0);
    assert(x2_controller_manager_get_count(&man) == 1);
    assert(s_disconnect_count == 1);
    assert(x2_controller_manager_get(&man, 0)->impl_id == 11);

    x2_controller_manager_remove(&man, 0);
    assert(x2_controller_manager_get_count(&man) == 0);
    assert(s_disconnect_count == 2);
    printf("hotswap: ok\n");
}

static void test_poll_headless(void)
{
    x2_controller_manager man;
    x2_controller_manager_init(&man);
    if (x2_sdl_controller_init(&man) != 0) {
        printf("SKIP poll (no SDL)\n");
        x2_controller_manager_shutdown(&man);
        return;
    }
    x2_sdl_controller_poll(&man);
    x2_sdl_controller_poll(&man);
    printf("poll headless: ok (%d controllers)\n", x2_controller_manager_get_count(&man));
    x2_controller_manager_shutdown(&man);
}

static void test_virtual_controller(void)
{
    s_connect_count = 0;
    s_disconnect_count = 0;
    x2_controller_manager man;
    x2_controller_manager_init(&man);
    x2_controller_manager_set_callbacks(&man, on_connect, on_disconnect, NULL);
    if (x2_sdl_controller_init(&man) != 0) {
        printf("SKIP virtual controller (no SDL)\n");
        return;
    }

    /* SDL3 attaches a virtual joystick from a descriptor rather than from a
       (type, naxes, nbuttons, nhats) tuple, and returns 0 -- not a negative
       number -- when it fails. */
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = 4;
    desc.nbuttons = 14;
    desc.nhats = 4;
    SDL_JoystickID jid = SDL_AttachVirtualJoystick(&desc);
    if (jid == 0) {
        printf("SKIP virtual controller: %s\n", SDL_GetError());
        x2_controller_manager_shutdown(&man);
        return;
    }

    SDL_GUID g = SDL_GetJoystickGUIDForID(jid);
    char gs[64];
    SDL_GUIDToString(g, gs, sizeof(gs));
    char map[512];
    snprintf(map, sizeof(map),
             "%s,Virtual Controller,a:b0,b:b1,x:b2,y:b3,back:b4,start:b6,"
             "leftshoulder:b9,rightshoulder:b10,leftstick:b7,rightstick:b8,"
             "dpup:h0.1,dpleft:h0.8,dpdown:h0.4,dpright:h0.2,"
             "leftx:a0,lefty:a1,rightx:a2,righty:a3,",
             gs);
    SDL_AddGamepadMapping(map);

    x2_sdl_controller_poll(&man);
    assert(x2_controller_manager_get_count(&man) == 1);
    assert(s_connect_count == 1);
    x2_controller *c = x2_controller_manager_get(&man, 0);
    assert(c->connected);
    assert(c->is_console);

    SDL_Joystick *joy = SDL_OpenJoystick(jid);
    assert(joy);

    SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_SOUTH, 1);
    x2_sdl_controller_poll(&man);
    assert(x2_controller_get_button_state(c, X2_BUTTON_RIGHT_PAD_DOWN));
    SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_SOUTH, 0);
    x2_sdl_controller_poll(&man);
    assert(!x2_controller_get_button_state(c, X2_BUTTON_RIGHT_PAD_DOWN));

    SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_BACK, 1);
    SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_START, 1);
    x2_sdl_controller_poll(&man);
    assert(x2_controller_get_button_state(c, X2_BUTTON_SELECT));
    assert(x2_controller_get_button_state(c, X2_BUTTON_START));

    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_RIGHTX, 32767);
    x2_sdl_controller_poll(&man);
    float x, y;
    x2_controller_get_joystick(c, 1, &x, &y);
    assert(x > 0.99f);
    assert(y == 0.0f);

    SDL_CloseJoystick(joy);
    SDL_DetachVirtualJoystick(jid);
    x2_sdl_controller_poll(&man);
    assert(x2_controller_manager_get_count(&man) == 0);
    assert(s_disconnect_count == 1);

    printf("virtual controller (SDL event path): ok\n");
    x2_controller_manager_shutdown(&man);
}

int main(void)
{
    test_mapping();
    test_button_state();
    test_pressure();
    test_joystick();
    test_hotswap();
    test_poll_headless();
    test_virtual_controller();
    printf("all controller tests passed\n");
    return 0;
}
