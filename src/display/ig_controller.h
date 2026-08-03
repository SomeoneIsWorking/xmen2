#ifndef X2_IG_CONTROLLER_H
#define X2_IG_CONTROLLER_H

#include <stdint.h>

#define X2_MAX_CONTROLLERS 4
#define X2_BUTTON_COUNT 32

typedef enum {
    X2_BUTTON_SELECT = 0,
    X2_BUTTON_LEFT_JOYSTICK_BUTTON = 1,
    X2_BUTTON_RIGHT_JOYSTICK_BUTTON = 2,
    X2_BUTTON_START = 3,
    X2_BUTTON_LEFT_PAD_UP = 4,
    X2_BUTTON_LEFT_PAD_RIGHT = 5,
    X2_BUTTON_LEFT_PAD_DOWN = 6,
    X2_BUTTON_LEFT_PAD_LEFT = 7,
    X2_BUTTON_LOWER_LEFT_TRIGGER = 8,
    X2_BUTTON_LOWER_RIGHT_TRIGGER = 9,
    X2_BUTTON_UPPER_LEFT_TRIGGER = 10,
    X2_BUTTON_UPPER_RIGHT_TRIGGER = 11,
    X2_BUTTON_RIGHT_PAD_UP = 12,
    X2_BUTTON_RIGHT_PAD_RIGHT = 13,
    X2_BUTTON_RIGHT_PAD_DOWN = 14,
    X2_BUTTON_RIGHT_PAD_LEFT = 15,
    X2_BUTTON_16 = 16,
    X2_BUTTON_17 = 17,
    X2_BUTTON_18 = 18,
    X2_BUTTON_19 = 19,
    X2_BUTTON_20 = 20,
    X2_BUTTON_21 = 21,
    X2_BUTTON_22 = 22,
    X2_BUTTON_23 = 23,
    X2_BUTTON_24 = 24,
    X2_BUTTON_25 = 25,
    X2_BUTTON_26 = 26,
    X2_BUTTON_27 = 27,
    X2_BUTTON_28 = 28,
    X2_BUTTON_29 = 29,
    X2_BUTTON_30 = 30,
    X2_BUTTON_31 = 31,
    X2_BUTTON_MAX = 32,
    X2_BUTTON_UNMAPPED = 0xffff
} x2_button;

typedef enum {
    X2_CONTROLLER_UNKNOWN = 0,
    X2_CONTROLLER_PSX2_PELICAN_16BUTTONS,
    X2_CONTROLLER_PSX2_SMARTJOY_12BUTTONS,
    X2_CONTROLLER_PSX2_XSERIES_12BUTTONS,
    X2_CONTROLLER_PSX2_ELECOM_12BUTTONSPOV,
    X2_CONTROLLER_PSX2_ELECOM_16BUTTONS,
    X2_CONTROLLER_PSX2_SANWA_16BUTTONS,
    X2_CONTROLLER_XBOX360_MICROSOFT_10BUTTONSPOV,
    X2_CONTROLLER_TYPE_COUNT
} x2_controller_type;

typedef struct x2_controller {
    uint16_t id;
    uint32_t button_state;
    float pressure[X2_BUTTON_COUNT];
    float joystick[2][2];
    uint8_t connected;
    x2_controller_type type;
    uint8_t is_console;
    void *impl;
    int impl_id;
} x2_controller;

typedef struct x2_controller_manager x2_controller_manager;

typedef void (*x2_controller_connection_cb)(x2_controller_manager *man, x2_controller *controller);
typedef void (*x2_controller_disconnection_cb)(x2_controller_manager *man, x2_controller *controller);

struct x2_controller_manager {
    x2_controller controllers[X2_MAX_CONTROLLERS];
    int count;
    x2_controller_connection_cb on_connect;
    x2_controller_disconnection_cb on_disconnect;
    void *userdata;
};

void x2_controller_manager_init(x2_controller_manager *man);
void x2_controller_manager_shutdown(x2_controller_manager *man);
void x2_controller_manager_set_callbacks(x2_controller_manager *man,
                                         x2_controller_connection_cb on_connect,
                                         x2_controller_disconnection_cb on_disconnect,
                                         void *userdata);
int x2_controller_manager_get_count(const x2_controller_manager *man);
x2_controller *x2_controller_manager_get(const x2_controller_manager *man, int index);
x2_controller *x2_controller_manager_add(x2_controller_manager *man);
void x2_controller_manager_remove(x2_controller_manager *man, int index);
x2_controller *x2_controller_manager_find_by_impl(const x2_controller_manager *man, int impl_id);

int x2_controller_is_connected(const x2_controller *controller);
uint32_t x2_controller_get_buttons_state(const x2_controller *controller);
int x2_controller_get_button_state(const x2_controller *controller, x2_button button);
float x2_controller_get_button_pressure(const x2_controller *controller, x2_button button);
void x2_controller_set_button_state(x2_controller *controller, x2_button button, int pressed);
void x2_controller_set_button_pressure(x2_controller *controller, x2_button button, float pressure);
void x2_controller_get_joystick(const x2_controller *controller, unsigned int stick,
                                float *x, float *y);
void x2_controller_set_joystick(x2_controller *controller, unsigned int stick, float x, float y);
void x2_controller_set_rumble(x2_controller *controller, int motor, float speed);

#endif
