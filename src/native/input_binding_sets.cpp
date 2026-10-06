#include "input_binding_sets.h"

#include "input_bindings.h"

unsigned input_binding_sets_for_player(uint32_t player,
                                       InputBindingSetVisitor visit,
                                       void *context) {
  static const uint32_t BANK[INPUT_BINDING_SETS] = {
      INPUT_SET_MASTER, INPUT_SET_WORKING, INPUT_SET_MENU};
  unsigned bank;

  if (player >= INPUT_PLAYERS || !visit)
    return 0;
  for (bank = 0; bank < INPUT_BINDING_SETS; bank++)
    visit(BANK[bank] + player, context);
  return INPUT_BINDING_SETS;
}
