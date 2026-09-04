/* Native adapter for MSVC's _CI* math ABI; operands/results use x87 state. */
#include "x87crt.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static double stack_double(CPU *C) {
  uint64_t bits = RD32(C->reg[kX86pEsp] + 4u) |
                  ((uint64_t)RD32(C->reg[kX86pEsp] + 8u) << 32);
  double value;
  memcpy(&value, &bits, sizeof value);
  return value;
}

static void return_double(CPU *C, double value) {
  x87_crt_push(C, (long double)value);
  C->reg[kX86pEsp] += 4u;
}

void x87_crt_push(CPU *C, long double value) {
  if (!x86p_x87_push(&C->x87, value))
    x87_fault("x87 stack overflow returning from the CRT");
}

long double x87_crt_pop(CPU *C) {
  long double value = 0.0L;
  if (!x86p_x87_pop(&C->x87, &value))
    x87_fault("x87 stack underflow entering a _CI* routine");
  return value;
}

void x87_crt_cipow(CPU *C) {
  long double y = x87_crt_pop(C), x = x87_crt_pop(C);
  x87_crt_push(C, powl(x, y));
  C->reg[kX86pEsp] += 4u;
}

void x87_crt_cifmod(CPU *C) {
  long double y = x87_crt_pop(C), x = x87_crt_pop(C);
  x87_crt_push(C, fmodl(x, y));
  C->reg[kX86pEsp] += 4u;
}

void x87_crt_ciacos(CPU *C) {
  long double x = x87_crt_pop(C);
  x87_crt_push(C, acosl(x));
  C->reg[kX86pEsp] += 4u;
}

void x87_crt_ciasin(CPU *C) {
  long double x = x87_crt_pop(C);
  x87_crt_push(C, asinl(x));
  C->reg[kX86pEsp] += 4u;
}

void x87_crt_ftol(CPU *C) {
  int64_t result = (int64_t)x87_crt_pop(C);
  C->reg[kX86pEax] = (uint32_t)(uint64_t)result;
  C->reg[kX86pEdx] = (uint32_t)((uint64_t)result >> 32);
  C->reg[kX86pEsp] += 4u;
}

void x87_crt_atof(CPU *C) {
  return_double(C, atof((const char *)(uintptr_t)RD32(C->reg[kX86pEsp] + 4u)));
}

void x87_crt_ceil(CPU *C) { return_double(C, ceil(stack_double(C))); }
void x87_crt_floor(CPU *C) { return_double(C, floor(stack_double(C))); }

void x87_crt_finite(CPU *C) {
  C->reg[kX86pEax] = isfinite(stack_double(C)) ? 1u : 0u;
  C->reg[kX86pEsp] += 4u;
}
