#include "ig_controller.h"

#include <string.h>

void x2_controller_manager_init(x2_controller_manager *man)
{
    memset(man, 0, sizeof(*man));
}

void x2_controller_manager_shutdown(x2_controller_manager *man)
{
    for (int i = 0; i < man->count; ++i) {
        x2_controller *c = &man->controllers[i];
        if (c->impl && c->connected) {
            if (man->on_disconnect) {
                man->on_disconnect(man, c);
            }
        }
        memset(c, 0, sizeof(*c));
    }
    man->count = 0;
}

void x2_controller_manager_set_callbacks(x2_controller_manager *man,
                                         x2_controller_connection_cb on_connect,
                                         x2_controller_disconnection_cb on_disconnect,
                                         void *userdata)
{
    man->on_connect = on_connect;
    man->on_disconnect = on_disconnect;
    man->userdata = userdata;
}

int x2_controller_manager_get_count(const x2_controller_manager *man)
{
    return man->count;
}

x2_controller *x2_controller_manager_get(const x2_controller_manager *man, int index)
{
    if (index < 0 || index >= man->count) {
        return NULL;
    }
    return (x2_controller *)&man->controllers[index];
}

x2_controller *x2_controller_manager_add(x2_controller_manager *man)
{
    if (man->count >= X2_MAX_CONTROLLERS) {
        return NULL;
    }
    x2_controller *c = &man->controllers[man->count];
    memset(c, 0, sizeof(*c));
    ++man->count;
    if (man->on_connect) {
        man->on_connect(man, c);
    }
    return c;
}

void x2_controller_manager_remove(x2_controller_manager *man, int index)
{
    if (index < 0 || index >= man->count) {
        return;
    }
    x2_controller *c = &man->controllers[index];
    if (c->impl && c->connected) {
        if (man->on_disconnect) {
            man->on_disconnect(man, c);
        }
    }
    int n = man->count - index - 1;
    if (n > 0) {
        memmove(&man->controllers[index], &man->controllers[index + 1],
                (size_t)n * sizeof(x2_controller));
    }
    memset(&man->controllers[man->count - 1], 0, sizeof(x2_controller));
    --man->count;
}

x2_controller *x2_controller_manager_find_by_impl(const x2_controller_manager *man, int impl_id)
{
    for (int i = 0; i < man->count; ++i) {
        if (man->controllers[i].impl_id == impl_id) {
            return (x2_controller *)&man->controllers[i];
        }
    }
    return NULL;
}

int x2_controller_is_connected(const x2_controller *controller)
{
    return controller->connected;
}

uint32_t x2_controller_get_buttons_state(const x2_controller *controller)
{
    return controller->button_state;
}

int x2_controller_get_button_state(const x2_controller *controller, x2_button button)
{
    if (button >= X2_BUTTON_MAX) {
        return 0;
    }
    return ((controller->button_state >> button) & 1u) == 1u;
}

float x2_controller_get_button_pressure(const x2_controller *controller, x2_button button)
{
    if (button >= X2_BUTTON_MAX) {
        return 0.0f;
    }
    return controller->pressure[button];
}

void x2_controller_set_button_state(x2_controller *controller, x2_button button, int pressed)
{
    if (button >= X2_BUTTON_MAX) {
        return;
    }
    if (pressed) {
        controller->button_state |= (1u << button);
    } else {
        controller->button_state &= ~(1u << button);
    }
}

void x2_controller_set_button_pressure(x2_controller *controller, x2_button button, float pressure)
{
    if (button >= X2_BUTTON_MAX) {
        return;
    }
    controller->pressure[button] = pressure;
}

void x2_controller_get_joystick(const x2_controller *controller, unsigned int stick,
                                float *x, float *y)
{
    if (stick > 1) {
        *x = 0.0f;
        *y = 0.0f;
        return;
    }
    *x = controller->joystick[stick][0];
    *y = controller->joystick[stick][1];
}

void x2_controller_set_joystick(x2_controller *controller, unsigned int stick, float x, float y)
{
    if (stick > 1) {
        return;
    }
    controller->joystick[stick][0] = x;
    controller->joystick[stick][1] = y;
}
