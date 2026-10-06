#include "controller_assignment_rows.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

static const char *pad_id[2];
static const char *pad_name[2];
static int pad_stable[2];
static const char *transient_id[4];
static int transient_pad[4] = {-1, -1, -1, -1};
static int checks;
#define CHECK(c)                                                               \
  do {                                                                         \
    assert(c);                                                                 \
    checks++;                                                                  \
  } while (0)

extern "C" {
const char *dinput_pad_persistent_id(int pad) {
  return pad >= 0 && pad < 2 ? pad_id[pad] : nullptr;
}

const char *dinput_pad_name(int pad) {
  return pad >= 0 && pad < 2 ? pad_name[pad] : nullptr;
}

int dinput_pad_persistent_id_is_stable(int pad) {
  return pad >= 0 && pad < 2 && pad_stable[pad];
}

const char *x2_transient_controller_id(unsigned player) {
  return player < 4 ? transient_id[player] : nullptr;
}

int x2_transient_controller_resolve(unsigned player) {
  return player < 4 && transient_id[player] ? transient_pad[player] : -1;
}

int x2_transient_controller_has_assignment(unsigned player) {
  return player < 4 && transient_id[player];
}

int x2_transient_controller_player_for_pad(int pad) {
  for (unsigned player = 0; player < 4; player++)
    if (transient_id[player] && transient_pad[player] == pad)
      return (int)player;
  return -1;
}
}

int main() {
  X2Settings settings{};
  for (auto &assignment : settings.controller)
    assignment.player = X2_SETTINGS_UNASSIGNED;
  std::strcpy(settings.controller[0].id, "stable-a");
  settings.controller[0].player = 1;
  transient_id[1] = "stable-a";
  transient_pad[1] = 0;

  pad_id[0] = "stable-a";
  pad_name[0] = "Generic USB Pad";
  pad_stable[0] = 1;
  pad_id[1] = "session-b";
  pad_name[1] = "Virtual Pad";
  pad_stable[1] = 0;

  auto rows = x2::ui::controller_assignment_rows(settings);
  CHECK(rows.size() == 2);
  CHECK(rows[0].id == "stable-a");
  CHECK(rows[0].owner == 1);
  CHECK(rows[0].pad == 0);
  CHECK(rows[0].stable_identity);
  CHECK(rows[0].transient_assignment);
  CHECK(rows[0].name == "Generic USB Pad (session assignment; not saved)");
  CHECK(rows[1].id == "session-b");
  CHECK(rows[1].name == "Virtual Pad (session only; not saved)");
  CHECK(!rows[1].stable_identity);
  CHECK(!rows[1].transient_assignment);
  CHECK(rows[1].pad == 1);
  CHECK(rows[1].owner == -1);

  std::printf("test_controller_assignment_rows: %d checks passed\n", checks);
  return 0;
}
