#ifndef X2_KEYBOARD_BINDINGS_DOCUMENT_HPP
#define X2_KEYBOARD_BINDINGS_DOCUMENT_HPP

#include <string>

#include "settings.h"

namespace Rml {
class ElementDocument;
class EventListener;
} // namespace Rml

namespace x2::ui {

/* The key a profile row resolves to: its override, else the game's own. */
std::string keyboard_binding_label(const x2::config::KeyboardProfile &profile,
                                   unsigned row);
std::string
keyboard_bindings_document_rml(const x2::config::KeyboardProfile &profile);
void keyboard_bindings_document_wire(Rml::ElementDocument &document,
                                     Rml::EventListener &listener);
/* Applies a restore control to `profile`; false when `id` names none. */
bool keyboard_bindings_document_restore(x2::config::KeyboardProfile &profile,
                                        const std::string &id);
/* Whether the game's own bindings have been read, so labels can name them. */
bool keyboard_bindings_game_known();

} // namespace x2::ui

#endif
