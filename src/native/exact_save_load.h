#ifndef X2_EXACT_SAVE_LOAD_H
#define X2_EXACT_SAVE_LOAD_H

#include <stdint.h>

struct X86pCpu;

typedef enum {
  X2_EXACT_SAVE_LOAD_NONE,
  X2_EXACT_SAVE_LOAD_CONTINUE,
  X2_EXACT_SAVE_LOAD_MENU
} X2ExactSaveLoadOwner;

typedef void (*X2ExactSaveLoadCompletion)(int succeeded);

int x2_exact_save_load_read_header(const struct X86pCpu *source, uint32_t exe,
                                   const char *leaf, uint32_t metadata);
int x2_exact_save_load_start(const struct X86pCpu *source, uint32_t exe,
                             const char *leaf, unsigned staging_slot,
                             X2ExactSaveLoadOwner owner,
                             X2ExactSaveLoadCompletion completion);

#endif /* X2_EXACT_SAVE_LOAD_H */
