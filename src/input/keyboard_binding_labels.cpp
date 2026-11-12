#include "keyboard_binding_labels.hpp"

#include <cstdio>

#include "dinput_system.h"
#include "player_input.h"

namespace x2::input {
namespace {

constexpr uint32_t kKeyboardKind = 1;
constexpr uint32_t kMouseKind = 2;

std::string device_code_label(uint32_t kind, uint32_t code) {
  if (kind == 0u || code == 0u) {
    return "Unbound";
  }
  if (kind == kKeyboardKind && code <= 0xffu) {
    const char *name = dinput_system_dik_name(static_cast<unsigned char>(code));
    if (name != nullptr && name[0] != '\0') {
      return name;
    }
  }
  char out[48];
  if (kind == kMouseKind) {
    std::snprintf(out, sizeof out, "Mouse %u", code);
  } else {
    std::snprintf(out, sizeof out, "Device %u code 0x%02x", kind, code);
  }
  return out;
}

} // namespace

std::string keyboard_binding_label(const x2::config::KeyboardProfile &profile,
                                   unsigned row) {
  uint32_t kind = 0;
  uint32_t code = 0;
  if (profile.keyboard_set[row] != 0u) {
    return device_code_label(kKeyboardKind, profile.keyboard[row]);
  }
  if (!player_input_game_keyboard_binding(row, &kind, &code)) {
    return "Game default";
  }
  return device_code_label(kind, code);
}

bool keyboard_bindings_game_known() {
  uint32_t kind = 0;
  uint32_t code = 0;
  return player_input_game_keyboard_binding(0, &kind, &code) != 0;
}

} // namespace x2::input
