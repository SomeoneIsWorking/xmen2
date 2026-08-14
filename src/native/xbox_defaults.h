#ifndef X2_XBOX_DEFAULTS_H
#define X2_XBOX_DEFAULTS_H

#include <stddef.h>
#include <stdint.h>

struct CPU;

typedef struct {
    uint8_t binding;
    uint8_t code;
} XboxDefaultBinding;

/* The bindable part of the original Xbox control layout, recovered from the
   XBE action IDs, the console controller screen, and XMen2.exe's binding ABI. */
const XboxDefaultBinding *xbox_default_bindings(size_t *count);

/* Keep the port-owned preset in slot 2 while a pad is present. Existing pad
   bindings are user state and are never overwritten. */
void xbox_defaults_sync(struct CPU *cpu);

/* Explicit menu action: replace pad slot 2 with the verified Xbox layout.
   Unlike automatic sync, this is user-selected state and remains installed
   when the controller disconnects. Returns nonzero when it was applied. */
int xbox_defaults_apply(struct CPU *cpu);
void xbox_defaults_report(void);

#endif
