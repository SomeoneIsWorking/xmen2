#ifndef DINPUT_SCRIPT_H
#define DINPUT_SCRIPT_H

#include <stdint.h>

struct CPU;
void dinput_script_apply(struct CPU *cpu, uint32_t out, uint32_t size);

#endif /* DINPUT_SCRIPT_H */
