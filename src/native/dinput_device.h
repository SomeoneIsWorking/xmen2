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
    DINPUT_DEV_MOUSE
} DInputDeviceKind;

/* The guest address of the device object, or 0. One object per kind for the
   whole process, because the game caches what it creates. */
uint32_t dinput_device_new(DInputDeviceKind kind);

void dinput_device_report(void);

#endif /* DINPUT_DEVICE_H */
