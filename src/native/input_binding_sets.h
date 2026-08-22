#ifndef X2_INPUT_BINDING_SETS_H
#define X2_INPUT_BINDING_SETS_H

#include <stdint.h>

typedef void (*InputBindingSetVisitor)(uint32_t controller, void *context);

/* Visit the master, working and menu controller sets that carry one player's
   bindings. This is the single production owner of that publication list. */
unsigned input_binding_sets_for_player(uint32_t player,
                                       InputBindingSetVisitor visit,
                                       void *context);

#endif
