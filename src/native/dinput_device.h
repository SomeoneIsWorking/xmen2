/*
 * The devices DirectInput hands out. See dinput_device.c.
 *
 * Kept separate from dinput.c (DirectInput 7) and dinput8.c (DirectInput 8)
 * because BOTH create devices and the device interface is the same one: the
 * engine's controller manager goes through DirectInput 7 and the exe's
 * keyboard/mouse through DirectInput 8, and two copies of the same vtable is
 * how the two would end up behaving differently.
 */
#ifndef DINPUT_DEVICE_H
#define DINPUT_DEVICE_H

#include <stdint.h>

typedef enum {
    DINPUT_DEV_KEYBOARD = 1,
    DINPUT_DEV_MOUSE,
    DINPUT_DEV_JOYSTICK
} DInputDeviceKind;

/* The guest address of the device object, or 0. One object per kind for the
   whole process, because the game caches what it creates -- except joysticks,
   where one object per PAD is the whole point: the game holds four at once. */
uint32_t dinput_device_new(DInputDeviceKind kind);
uint32_t dinput_device_new_pad(int pad);

/*
 * The system-device GUIDs, shared by both DirectInput stacks.
 *
 * dinput_guid_kind returns DINPUT_DEV_* for the system keyboard or mouse and 0
 * for anything else; dinput_guid_of hands back the sixteen bytes so an
 * enumeration can report the same GUID a CreateDevice will accept -- a device
 * enumerated under one GUID and creatable only under another is a device the
 * game can see and never open.
 */
int dinput_guid_kind(uint32_t guid);
const unsigned char *dinput_guid_of(int kind);

void dinput_device_report(void);

/*
 * Start the X2_INPUT_SCRIPT clock (see dinput_device.c). Called once when the
 * guest is about to run, so a script's times are seconds from the start of the
 * run rather than from whenever the game first polled the keyboard.
 */
void dinput_script_start(void);

#endif /* DINPUT_DEVICE_H */
