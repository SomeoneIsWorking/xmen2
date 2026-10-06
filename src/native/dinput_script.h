#ifndef DINPUT_SCRIPT_H
#define DINPUT_SCRIPT_H

#include <stdint.h>

struct X86pCpu;
void dinput_script_apply(struct X86pCpu *cpu, uint32_t out, uint32_t size);

#endif /* DINPUT_SCRIPT_H */
