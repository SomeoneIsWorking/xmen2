#include "keyboard_bindings_document.hpp"

#include "rml_text.hpp"

#include <RmlUi/Core.h>

#include <cstdio>
#include <sstream>

#include "binding_rows.h"
#include "dinput_system.h"
#include "player_input.h"

namespace x2::ui {
namespace {

constexpr char kRestorePrefix[] = "binding-reset-";
constexpr char kRestoreAll[] = "binding-reset-all";
constexpr uint32_t kKeyboardKind = 1;
constexpr uint32_t kMouseKind = 2;

std::string device_code_label(uint32_t kind, uint32_t code) {
  char out[48];
  if (!kind || !code)
    return "Unbound";
  if (kind == kKeyboardKind && code <= 0xffu) {
    const char *name = dinput_system_dik_name((unsigned char)code);
    if (name && name[0])
      return name;
  }
  if (kind == kMouseKind)
    std::snprintf(out, sizeof out, "Mouse %u", code);
  else
    std::snprintf(out, sizeof out, "Device %u code 0x%02x", kind, code);
  return out;
}

void wire(Rml::ElementDocument &document, Rml::EventListener &listener,
          const std::string &id) {
  if (Rml::Element *element = document.GetElementById(id))
    element->AddEventListener("click", &listener);
}

} // namespace

std::string keyboard_binding_label(const X2KeyboardProfile &profile,
                                   unsigned row) {
  uint32_t kind = 0;
  uint32_t code = 0;
  if (profile.keyboard_set[row])
    return device_code_label(kKeyboardKind, profile.keyboard[row]);
  if (!x2_player_input_game_keyboard_binding(row, &kind, &code))
    return "Game default";
  return device_code_label(kind, code);
}

bool keyboard_bindings_game_known() {
  uint32_t kind;
  uint32_t code;
  return x2_player_input_game_keyboard_binding(0, &kind, &code) != 0;
}

std::string keyboard_bindings_document_rml(const X2KeyboardProfile &profile) {
  std::ostringstream rml;
  rml << "<button id='" << kRestoreAll << "'>Restore all defaults</button>"
      << "<div class='section-heading'>Actions</div>";
  for (unsigned row = 0; row < INPUT_BINDING_ROWS; row++) {
    const char *name = input_binding_row_display_label(row);
    rml << "<div class='binding'><key>" << escape_rml(name ? name : "Unknown")
        << "</key><button id='kb-" << row << "'>"
        << escape_rml(keyboard_binding_label(profile, row)) << "</button>";
    if (profile.keyboard_set[row])
      rml << "<button class='restore' id='" << kRestorePrefix << row
          << "'>Reset</button>";
    else
      rml << "<restore-slot></restore-slot>";
    rml << "</div>";
  }
  return rml.str();
}

void keyboard_bindings_document_wire(Rml::ElementDocument &document,
                                     Rml::EventListener &listener) {
  wire(document, listener, kRestoreAll);
  for (unsigned row = 0; row < INPUT_BINDING_ROWS; row++) {
    wire(document, listener, "kb-" + std::to_string(row));
    wire(document, listener, kRestorePrefix + std::to_string(row));
  }
}

bool keyboard_bindings_document_restore(X2KeyboardProfile &profile,
                                        const std::string &id) {
  unsigned row;
  char tail;
  if (id == kRestoreAll) {
    x2_keyboard_profile_restore_all(&profile);
    return true;
  }
  if (id.rfind(kRestorePrefix, 0) != 0 ||
      std::sscanf(id.c_str() + sizeof kRestorePrefix - 1, "%u%c", &row,
                  &tail) != 1 ||
      row >= INPUT_BINDING_ROWS)
    return false;
  x2_keyboard_profile_restore_row(&profile, row);
  return true;
}

} // namespace x2::ui
