#include "keyboard_bindings_document.hpp"

#include "dinput_system.h"
#include "player_input.h"

#include <cassert>
#include <cstdio>
#include <cstring>

static bool game_known;
static uint32_t game_kind[x2::config::kSettingsRows];
static uint32_t game_code[x2::config::kSettingsRows];
static int checks;
#define CHECK(c)                                                               \
  do {                                                                         \
    assert(c);                                                                 \
    checks++;                                                                  \
  } while (0)

const char *dinput_system_dik_name(unsigned char dik) {
  switch (dik) {
  case 0x11:
    return "W";
  case 0x1e:
    return "A";
  default:
    return nullptr;
  }
}

namespace x2::input {
int player_input_game_keyboard_binding(unsigned row, uint32_t *kind,
                                       uint32_t *code) {
  if (!game_known || row >= x2::config::kSettingsRows)
    return 0;
  *kind = game_kind[row];
  *code = game_code[row];
  return 1;
}
} // namespace x2::input

int main() {
  x2::config::KeyboardProfile profile{};

  CHECK(!x2::ui::keyboard_bindings_game_known());
  CHECK(x2::ui::keyboard_binding_label(profile, 0) == "Game default");

  game_known = true;
  game_kind[0] = 1;
  game_code[0] = 0x11;
  game_kind[2] = 2;
  game_code[2] = 1;
  game_kind[3] = 1;
  game_code[3] = 0x7f;
  CHECK(x2::ui::keyboard_bindings_game_known());
  CHECK(x2::ui::keyboard_binding_label(profile, 0) == "W");
  CHECK(x2::ui::keyboard_binding_label(profile, 1) == "Unbound");
  CHECK(x2::ui::keyboard_binding_label(profile, 2) == "Mouse 1");
  CHECK(x2::ui::keyboard_binding_label(profile, 3) == "Device 1 code 0x7f");

  profile.keyboard_set[0] = 1;
  profile.keyboard[0] = 0x1e;
  profile.keyboard_set[1] = 1;
  profile.keyboard[1] = 0;
  CHECK(x2::ui::keyboard_binding_label(profile, 0) == "A");
  CHECK(x2::ui::keyboard_binding_label(profile, 1) == "Unbound");

  std::string rml = x2::ui::keyboard_bindings_document_rml(profile);
  CHECK(rml.find("id='binding-reset-0'") != std::string::npos);
  CHECK(rml.find("id='binding-reset-2'") == std::string::npos);
  CHECK(rml.find("id='binding-reset-all'") != std::string::npos);

  CHECK(!x2::ui::keyboard_bindings_document_restore(profile, "kb-0"));
  CHECK(
      !x2::ui::keyboard_bindings_document_restore(profile, "binding-reset-42"));
  CHECK(
      !x2::ui::keyboard_bindings_document_restore(profile, "binding-reset-0x"));
  CHECK(x2::ui::keyboard_bindings_document_restore(profile, "binding-reset-0"));
  CHECK(!profile.keyboard_set[0]);
  CHECK(profile.keyboard_set[1]);
  CHECK(x2::ui::keyboard_binding_label(profile, 0) == "W");
  CHECK(
      x2::ui::keyboard_bindings_document_restore(profile, "binding-reset-all"));
  CHECK(!profile.keyboard_set[1]);

  std::printf("test_keyboard_bindings_document: %d checks passed\n", checks);
  return 0;
}
