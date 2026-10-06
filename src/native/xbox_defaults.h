#ifndef X2_XBOX_DEFAULTS_H
#define X2_XBOX_DEFAULTS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint8_t binding;
  uint8_t code;
} XboxDefaultBinding;

/* The bindable part of the original Xbox control layout, recovered from the
   XBE action IDs, the console controller screen, and XMen2.exe's binding ABI.
 */
const XboxDefaultBinding *xbox_default_bindings(size_t *count);

#endif
