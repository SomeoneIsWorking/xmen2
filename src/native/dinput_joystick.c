/* DirectInput joystick state layout and object enumeration. */
#include "dinput_joystick.h"

#include "dinput_pad.h"
#include "guest_heap.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <string.h>

void dinput_joystick_state(int pad, int32_t lo, int32_t hi,
                           uint32_t out, uint32_t size)
{
    int32_t mid = lo + (hi - lo) / 2;
    int button, count;

    /* Latch SDL's current view ONCE, before reading the sixteen values below
       out of it. Without this every one of them reports what SDL happened to
       hold at startup, which is "nothing pressed" forever. */
    dinput_pad_refresh_state();

    memset((void *)(uintptr_t)out, 0, size);
    if (size < 176u) {
        fprintf(stderr, "DINPUT8: a %u-byte joystick state is smaller than the "
                        "176 bytes DIJOYSTATE2 needs for axes, POVs and "
                        "buttons. Nothing is written.\n", size);
        return;
    }
    WR32(out +  0u, (uint32_t)dinput_pad_axis(pad, DINPUT_PAD_AXIS_X, lo, hi));
    WR32(out +  4u, (uint32_t)dinput_pad_axis(pad, DINPUT_PAD_AXIS_Y, lo, hi));
    WR32(out +  8u, (uint32_t)dinput_pad_axis(pad, DINPUT_PAD_AXIS_Z, lo, hi));
    WR32(out + 12u, (uint32_t)dinput_pad_axis(pad, DINPUT_PAD_AXIS_RX, lo, hi));
    WR32(out + 16u, (uint32_t)dinput_pad_axis(pad, DINPUT_PAD_AXIS_RY, lo, hi));
    WR32(out + 20u, (uint32_t)dinput_pad_axis(pad, DINPUT_PAD_AXIS_RZ, lo, hi));
    WR32(out + 24u, (uint32_t)mid);
    WR32(out + 28u, (uint32_t)mid);
    WR32(out + 32u, dinput_pad_pov(pad));
    WR32(out + 36u, 0xFFFFFFFFu);
    WR32(out + 40u, 0xFFFFFFFFu);
    WR32(out + 44u, 0xFFFFFFFFu);
    count = dinput_pad_button_count(pad);
    for (button = 0; button < count && 48u + (uint32_t)button < size; button++)
        if (dinput_pad_button(pad, button))
            *((unsigned char *)(uintptr_t)out + 48 + button) = 0x80;
}

static void object_guid(unsigned char guid[16], unsigned char low)
{
    static const unsigned char REST[15] = {
        0x02,0x6D,0xA3, 0xF3,0xC9, 0xCF,0x11,
        0xBF,0xC7, 0x44,0x45,0x53,0x54,0x00,0x00
    };
    guid[0] = low;
    memcpy(guid + 1, REST, sizeof REST);
}

#define DIDFT_ABSAXIS   0x00000002u
#define DIDFT_PSHBUTTON 0x00000004u
#define DIDFT_POV       0x00000010u
#define DIOBJ_BYTES     0x13Cu

static void enum_object(CPU *cpu, uint32_t callback, uint32_t context,
                        uint32_t buffer, unsigned char guid_low,
                        uint32_t offset, uint32_t type, const char *name,
                        int *stop)
{
    CPU call;
    unsigned char guid[16];

    if (*stop) return;
    memset((void *)(uintptr_t)buffer, 0, DIOBJ_BYTES);
    WR32(buffer, DIOBJ_BYTES);
    object_guid(guid, guid_low);
    memcpy((void *)(uintptr_t)(buffer + 4u), guid, sizeof guid);
    WR32(buffer + 0x14u, offset);
    WR32(buffer + 0x18u, type);
    WR32(buffer + 0x1cu, 0); /* no DIDOI_FFACTUATOR: force feedback absent */
    snprintf((char *)(uintptr_t)(buffer + 0x20u), 260, "%s", name);
    call = *cpu;
    call.esp -= 8u;
    WR32(call.esp, buffer);
    WR32(call.esp + 4u, context);
    x86_guest_call_args(&call, callback, 8u);
    if (call.eax == 0u) *stop = 1;
}

uint32_t dinput_joystick_enum_objects(CPU *cpu, int pad, uint32_t callback,
                                      uint32_t context, uint32_t filter)
{
    static uint32_t buffer;
    static const struct { unsigned char low; const char *name; } AXES[6] = {
        { 0xE0, "X Axis" }, { 0xE1, "Y Axis" }, { 0xE2, "Z Axis" },
        { 0xF4, "X Rotation" }, { 0xF5, "Y Rotation" },
        { 0xE3, "Z Rotation" }
    };
    int stop = 0, i, buttons;

    if (!buffer && !(buffer = guest_malloc(DIOBJ_BYTES))) return 0x8007000Eu;
    for (i = 0; i < 6; i++) {
        uint32_t type = DIDFT_ABSAXIS | ((uint32_t)i << 8);
        if (filter && !(filter & DIDFT_ABSAXIS)) break;
        enum_object(cpu, callback, context, buffer, AXES[i].low,
                    (uint32_t)i * 4u, type, AXES[i].name, &stop);
    }
    if (!filter || (filter & DIDFT_POV))
        enum_object(cpu, callback, context, buffer, 0xF2, 32u, DIDFT_POV,
                    "Hat Switch", &stop);
    buttons = dinput_pad_button_count(pad);
    if (!filter || (filter & DIDFT_PSHBUTTON))
        for (i = 0; i < buttons; i++) {
            char name[32];
            snprintf(name, sizeof name, "Button %d", i);
            enum_object(cpu, callback, context, buffer, 0xF0,
                        48u + (uint32_t)i,
                        DIDFT_PSHBUTTON | ((uint32_t)i << 8), name, &stop);
        }
    return 0;
}
