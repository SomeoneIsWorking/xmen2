#ifndef DINPUT_SYSTEM_H
#define DINPUT_SYSTEM_H

#include <stdint.h>

int dinput_system_available(void);
unsigned long dinput_system_blind_reads(void);
unsigned char dinput_system_dik(int scancode);
const char *dinput_system_dik_name(unsigned char dik);
void dinput_system_keyboard_state(uint32_t out, uint32_t size);
void dinput_system_mouse_state(uint32_t out, uint32_t size);

#endif /* DINPUT_SYSTEM_H */
