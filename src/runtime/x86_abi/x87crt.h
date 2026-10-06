#ifndef X2_RUNTIME_X87_CRT_H
#define X2_RUNTIME_X87_CRT_H

#include "x86rt.h"

void x87_crt_push(CPU *C, long double value);
long double x87_crt_pop(CPU *C);

void x87_crt_cipow(CPU *C);
void x87_crt_cifmod(CPU *C);
void x87_crt_ciacos(CPU *C);
void x87_crt_ciasin(CPU *C);
void x87_crt_ftol(CPU *C);
void x87_crt_atof(CPU *C);
void x87_crt_ceil(CPU *C);
void x87_crt_floor(CPU *C);
void x87_crt_finite(CPU *C);

#endif /* X2_RUNTIME_X87_CRT_H */
