#ifndef X2_KEYBOARD_BINDING_LABELS_HPP
#define X2_KEYBOARD_BINDING_LABELS_HPP

#include <string>

#include "settings.h"

namespace x2::input {

/* The key a profile row resolves to: its override, else the game's own. */
std::string keyboard_binding_label(const x2::config::KeyboardProfile &profile,
                                   unsigned row);
/* Whether the game's own bindings have been read, so labels can name them. */
bool keyboard_bindings_game_known();

} // namespace x2::input

#endif
