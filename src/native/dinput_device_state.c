/* Polling policy for the DirectInput device snapshots returned to the game. */
#include "dinput_device_internal.h"

#include "dinput_joystick.h"
#include "dinput_pad.h"
#include "dinput_script.h"
#include "dinput_system.h"
#include "gpu_device.h"
#include "guest_clock.h"
#include "input_record.h"
#include "rmlui_ui.h"

#include <stdio.h>
#include <string.h>

#define A(i) RD32(cpu->esp + 4u + (uint32_t)(i) * 4u)

#define S_OK               0x00000000u
#define DIERR_INVALIDPARAM 0x80070057u
#define DIERR_NOTACQUIRED  0x8007000Cu
#define DIERR_INPUTLOST    0x8007001Eu

static void ret_get_state(CPU *cpu, uint32_t result)
{
    dinput_device_return(cpu, result, 2);
}

static const char *kind_name(DInputDeviceKind kind)
{
    return kind == DINPUT_DEV_KEYBOARD ? "keyboard"
         : kind == DINPUT_DEV_MOUSE ? "mouse"
         : kind == DINPUT_DEV_JOYSTICK ? "gamepad" : "(unknown)";
}

static void record_state(const DInputDevice *device, uint32_t out, uint32_t bytes)
{
    const void *state = (const void *)(uintptr_t)out;
    unsigned long frame = gpu_frames_presented();
    double now = guest_clock_elapsed_s();
    if (device->kind == DINPUT_DEV_KEYBOARD)
        input_record_keyboard(state, bytes, frame, now);
    else if (device->kind == DINPUT_DEV_JOYSTICK)
        input_record_gamepad((unsigned)device->pad,
                             dinput_pad_persistent_id(device->pad), state,
                             bytes, frame, now);
    else
        input_record_mouse(state, bytes, frame, now);
}

void dinput_device_get_state(CPU *cpu, DInputDevice *device)
{
    uint32_t bytes = A(1), out = A(2);

    if (!device || !out) { ret_get_state(cpu, DIERR_INVALIDPARAM); return; }
    if (!device->acquired) { ret_get_state(cpu, DIERR_NOTACQUIRED); return; }
    if (bytes != device->data_size) {
        fprintf(stderr, "DINPUT8: GetDeviceState on the %s asked for %u bytes "
                        "but its data format declared %u. Refusing rather than "
                        "writing a state whose fields land somewhere else.\n",
                kind_name(device->kind), bytes, device->data_size);
        ret_get_state(cpu, DIERR_INVALIDPARAM);
        return;
    }
    if (device->kind == DINPUT_DEV_JOYSTICK &&
        dinput_pad_name(device->pad) == NULL) {
        device->acquired = 0;
        ret_get_state(cpu, DIERR_INPUTLOST);
        return;
    }
    device->polls++;
    if (device->kind == DINPUT_DEV_KEYBOARD) {
        extern void dinput8_hotplug_pump(CPU *);
        static unsigned long last_frame = (unsigned long)-1;
        unsigned long frame = gpu_frames_presented();
        if (frame != last_frame) {
            last_frame = frame;
            dinput_pad_virtual_tick(frame);
            dinput8_hotplug_pump(cpu);
        }
    }
    if (x2_ui_captures_input()) {
        memset((void *)(uintptr_t)out, 0, bytes);
        record_state(device, out, bytes);
        ret_get_state(cpu, S_OK);
        return;
    }
    if (device->kind == DINPUT_DEV_KEYBOARD) {
        dinput_system_keyboard_state(out, bytes);
        dinput_script_apply(cpu, out, bytes);
    } else if (device->kind == DINPUT_DEV_JOYSTICK) {
        dinput_joystick_state(device->pad, device->axis_lo, device->axis_hi,
                              out, bytes);
    } else {
        dinput_system_mouse_state(out, bytes);
    }
    record_state(device, out, bytes);
    ret_get_state(cpu, S_OK);
}
