#ifndef X2_ATTR_STACK_VERIFY_H
#define X2_ATTR_STACK_VERIFY_H

#include <stdint.h>

struct X86pCpu;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int active;
  uint32_t self;
  int count;
  uint32_t stacks[128];
  uint32_t orig_f08[128];
  uint32_t orig_f18[128];
  uint32_t orig_f24[128];
  uint8_t orig_f20[128];
  uint8_t orig_f28[128];
  uint32_t orig_f30[128];
  uint32_t p18;
  uint32_t orig_p18_f8;
  uint32_t p1c;
  uint32_t orig_p1c_f8;
  uint32_t p2c;
  uint32_t orig_p2c_f8;
  uint32_t p40;
  uint32_t orig_p40_f14;
  uint32_t p24;
  uint32_t orig_p24_f8;
  uint32_t p28;
  uint32_t orig_p28_f8;
} AttrStackVerify;

void attr_stack_verify_begin(AttrStackVerify *v, uint32_t self);
void attr_stack_verify_end(const struct X86pCpu *C, AttrStackVerify *v,
                           uint32_t self);

#ifdef __cplusplus
}
#endif

#endif /* X2_ATTR_STACK_VERIFY_H */
