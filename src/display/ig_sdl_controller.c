#include "ig_controller.h"

#include <SDL.h>

#define X2_STICK_DEADZONE 0.1f
#define X2_TRIGGER_BUTTON_THRESHOLD 0.5f

static x2_button s_button_map[SDL_CONTROLLER_BUTTON_MAX];

static int s_button_map_ready;

static void build_button_map(void)
{
    for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; ++i) {
        s_button_map[i] = X2_BUTTON_UNMAPPED;
    }
    s_button_map[SDL_CONTROLLER_BUTTON_A] = X2_BUTTON_RIGHT_PAD_DOWN;
    s_button_map[SDL_CONTROLLER_BUTTON_B] = X2_BUTTON_RIGHT_PAD_RIGHT;
    s_button_map[SDL_CONTROLLER_BUTTON_X] = X2_BUTTON_RIGHT_PAD_LEFT;
    s_button_map[SDL_CONTROLLER_BUTTON_Y] = X2_BUTTON_RIGHT_PAD_UP;
    s_button_map[SDL_CONTROLLER_BUTTON_BACK] = X2_BUTTON_SELECT;
    s_button_map[SDL_CONTROLLER_BUTTON_START] = X2_BUTTON_START;
    s_button_map[SDL_CONTROLLER_BUTTON_LEFTSTICK] = X2_BUTTON_LEFT_JOYSTICK_BUTTON;
    s_button_map[SDL_CONTROLLER_BUTTON_RIGHTSTICK] = X2_BUTTON_RIGHT_JOYSTICK_BUTTON;
    s_button_map[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = X2_BUTTON_UPPER_LEFT_TRIGGER;
    s_button_map[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = X2_BUTTON_UPPER_RIGHT_TRIGGER;
    s_button_map[SDL_CONTROLLER_BUTTON_DPAD_UP] = X2_BUTTON_LEFT_PAD_UP;
    s_button_map[SDL_CONTROLLER_BUTTON_DPAD_DOWN] = X2_BUTTON_LEFT_PAD_DOWN;
    s_button_map[SDL_CONTROLLER_BUTTON_DPAD_LEFT] = X2_BUTTON_LEFT_PAD_LEFT;
    s_button_map[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = X2_BUTTON_LEFT_PAD_RIGHT;
    s_button_map[SDL_CONTROLLER_BUTTON_GUIDE] = X2_BUTTON_16;
    s_button_map_ready = 1;
}

int x2_sdl_controller_button_to_ig(SDL_GameControllerButton button)
{
    if (!s_button_map_ready) {
        build_button_map();
    }
    if (button < 0 || button >= SDL_CONTROLLER_BUTTON_MAX) {
        return X2_BUTTON_UNMAPPED;
    }
    return s_button_map[button];
}

static float normalize_axis(Sint16 value)
{
    float v = value >= 0 ? (float)value / 32767.0f : (float)value / 32768.0f;
    if (v > -X2_STICK_DEADZONE && v < X2_STICK_DEADZONE) {
        return 0.0f;
    }
    return v;
}

static x2_controller_type detect_type(SDL_GameController *gc)
{
    SDL_GameControllerType type = SDL_GameControllerGetType(gc);
    switch (type) {
    case SDL_CONTROLLER_TYPE_XBOX360:
    case SDL_CONTROLLER_TYPE_XBOXONE:
        return X2_CONTROLLER_XBOX360_MICROSOFT_10BUTTONSPOV;
    default:
        return X2_CONTROLLER_UNKNOWN;
    }
}

static void handle_added(x2_controller_manager *man, int device_index)
{
    SDL_GameController *gc = SDL_GameControllerOpen(device_index);
    if (!gc) {
        return;
    }
    if (x2_controller_manager_find_by_impl(man, SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc)))) {
        SDL_GameControllerClose(gc);
        return;
    }
    x2_controller *c = x2_controller_manager_add(man);
    if (!c) {
        SDL_GameControllerClose(gc);
        return;
    }
    c->impl = gc;
    c->impl_id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
    c->connected = 1;
    c->type = detect_type(gc);
    c->is_console = 1;
    c->id = (uint16_t)(man->count - 1);
    if (c->type == X2_CONTROLLER_XBOX360_MICROSOFT_10BUTTONSPOV) {
        x2_controller_set_button_pressure(c, X2_BUTTON_LOWER_LEFT_TRIGGER, 0.0f);
        x2_controller_set_button_pressure(c, X2_BUTTON_LOWER_RIGHT_TRIGGER, 0.0f);
    }
}

static void handle_removed(x2_controller_manager *man, SDL_JoystickID instance_id)
{
    for (int i = 0; i < man->count; ++i) {
        x2_controller *c = &man->controllers[i];
        if (c->impl_id != instance_id) {
            continue;
        }
        SDL_GameController *gc = c->impl;
        x2_controller_manager_remove(man, i);
        if (gc) {
            SDL_GameControllerClose(gc);
        }
        return;
    }
}

static void handle_button(x2_controller_manager *man, SDL_JoystickID instance_id,
                          SDL_GameControllerButton button, int pressed)
{
    x2_controller *c = x2_controller_manager_find_by_impl(man, instance_id);
    if (!c) {
        return;
    }
    int ig = x2_sdl_controller_button_to_ig(button);
    if (ig == X2_BUTTON_UNMAPPED) {
        return;
    }
    x2_controller_set_button_state(c, (x2_button)ig, pressed);
    x2_controller_set_button_pressure(c, (x2_button)ig, pressed ? 1.0f : 0.0f);
}

static void handle_axis(x2_controller_manager *man, SDL_JoystickID instance_id,
                        SDL_GameControllerAxis axis, Sint16 value)
{
    x2_controller *c = x2_controller_manager_find_by_impl(man, instance_id);
    if (!c) {
        return;
    }
    switch (axis) {
    case SDL_CONTROLLER_AXIS_LEFTX:
        x2_controller_set_joystick(c, 0, normalize_axis(value), c->joystick[0][1]);
        break;
    case SDL_CONTROLLER_AXIS_LEFTY:
        x2_controller_set_joystick(c, 0, c->joystick[0][0], normalize_axis(value));
        break;
    case SDL_CONTROLLER_AXIS_RIGHTX:
        x2_controller_set_joystick(c, 1, normalize_axis(value), c->joystick[1][1]);
        break;
    case SDL_CONTROLLER_AXIS_RIGHTY:
        x2_controller_set_joystick(c, 1, c->joystick[1][0], normalize_axis(value));
        break;
    case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
        x2_controller_set_button_pressure(c, X2_BUTTON_LOWER_LEFT_TRIGGER, (float)value / 32767.0f);
        x2_controller_set_button_state(c, X2_BUTTON_LOWER_LEFT_TRIGGER,
                                       value / 32767.0f > X2_TRIGGER_BUTTON_THRESHOLD);
        break;
    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
        x2_controller_set_button_pressure(c, X2_BUTTON_LOWER_RIGHT_TRIGGER, (float)value / 32767.0f);
        x2_controller_set_button_state(c, X2_BUTTON_LOWER_RIGHT_TRIGGER,
                                       value / 32767.0f > X2_TRIGGER_BUTTON_THRESHOLD);
        break;
    default:
        break;
    }
}

int x2_sdl_controller_init(x2_controller_manager *man)
{
    if (!s_button_map_ready) {
        build_button_map();
    }
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
            return -1;
        }
    }
    return 0;
}

void x2_sdl_controller_poll(x2_controller_manager *man)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_CONTROLLERDEVICEADDED:
            handle_added(man, ev.cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            handle_removed(man, ev.cdevice.which);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            handle_button(man, ev.cbutton.which, ev.cbutton.button, 1);
            break;
        case SDL_CONTROLLERBUTTONUP:
            handle_button(man, ev.cbutton.which, ev.cbutton.button, 0);
            break;
        case SDL_CONTROLLERAXISMOTION:
            handle_axis(man, ev.caxis.which, ev.caxis.axis, ev.caxis.value);
            break;
        case SDL_CONTROLLERDEVICEREMAPPED:
            break;
        default:
            break;
        }
    }
}

void x2_controller_set_rumble(x2_controller *controller, int motor, float speed)
{
    SDL_GameController *gc = controller->impl;
    if (!gc) {
        return;
    }
    if (speed < 0.0f) {
        speed = 0.0f;
    }
    if (speed > 1.0f) {
        speed = 1.0f;
    }
    uint16_t low = 0;
    uint16_t high = 0;
    if (motor == 0) {
        low = (uint16_t)(speed * 0xffff);
    } else {
        high = (uint16_t)(speed * 0xffff);
    }
    SDL_GameControllerRumble(gc, low, high, 0);
}
