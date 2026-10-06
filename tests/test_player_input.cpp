#include "player_input.h"

#include "dinput_pad.h"
#include "input_binding_sets.h"
#include "input_bindings.h"
#include "settings_store.h"
#include "x86rt.h"
#include "xbox_defaults.h"

#include <stdio.h>
#include <string.h>

typedef struct {
  uint32_t kind, code;
} Slot;
static Slot slots[INPUT_CONTROLLERS][INPUT_BINDING_ROWS][INPUT_BINDING_SLOTS];
static X2Settings settings;
static const char *pads[DINPUT_PAD_MAX];
static int controller_slots[DINPUT_PAD_MAX] = {-1, -1, -1, -1, -1, -1, -1, -1};
static unsigned participation_join;
static unsigned participation_leave;
static unsigned participation_active;
static unsigned participation_eligible;
static int transient_pad[INPUT_PLAYERS] = {-2, -2, -2, -2};
static int checks;
#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      fprintf(stderr, "test_player_input:%d: %s failed\n", __LINE__, #c);      \
      return 1;                                                                \
    }                                                                          \
    checks++;                                                                  \
  } while (0)

X2Settings *x2_settings_store(void) { return &settings; }
void dinput_pad_refresh(void) {}
const char *dinput_pad_name(int pad) { return pads[pad]; }
int dinput_pad_for_persistent_id(const char *id) {
  int i;
  for (i = 0; i < DINPUT_PAD_MAX; i++)
    if (pads[i] && strcmp(pads[i], id) == 0)
      return i;
  return -1;
}
int dinput8_controller_slot_for_host_pad(int pad) {
  return pad >= 0 && pad < DINPUT_PAD_MAX ? controller_slots[pad] : -1;
}
int x2_transient_controller_has_assignment(unsigned player) {
  return player < INPUT_PLAYERS && transient_pad[player] != -2;
}
int x2_transient_controller_resolve(unsigned player) {
  return x2_transient_controller_has_assignment(player) ? transient_pad[player]
                                                        : -1;
}
uint32_t input_bindings_object_at(uint32_t index, char *why, int whyn) {
  (void)why;
  (void)whyn;
  return index + 1u;
}
int input_bindings_read(uint32_t object, uint32_t row, uint32_t slot,
                        uint32_t *kind, uint32_t *code) {
  Slot *s = &slots[object - 1u][row][slot];
  *kind = s->kind;
  *code = s->code;
  return 1;
}
typedef struct {
  uint32_t row, slot, kind, code;
} TestBindingWrite;

static void write_test_set(uint32_t controller, void *context) {
  TestBindingWrite *write = context;
  slots[controller][write->row][write->slot] = (Slot){write->kind, write->code};
}

unsigned input_bindings_write_player(CPU *cpu, uint32_t player, uint32_t row,
                                     uint32_t slot, uint32_t kind,
                                     uint32_t code) {
  TestBindingWrite write = {row, slot, kind, code};
  (void)cpu;
  return input_binding_sets_for_player(player, write_test_set, &write);
}
void x2_player_participation_apply(CPU *cpu, uint8_t join, uint8_t leave) {
  (void)cpu;
  participation_join |= join;
  participation_leave |= leave;
  participation_active |= join;
  participation_active &= (unsigned)~leave;
}

void x2_player_participation_enforce_eligibility(CPU *cpu, uint8_t eligible) {
  (void)cpu;
  participation_eligible = eligible;
  participation_active &= eligible;
}
int main(void) {
  CPU cpu = {0};
  unsigned char keyboard_state[256] = {0};
  unsigned char gamepad_state[80] = {0};
  unsigned player, row;
  for (player = 0; player < INPUT_PLAYERS; player++)
    for (row = 0; row < INPUT_BINDING_ROWS; row++)
      slots[player][row][0] = (Slot){1, 20u + row};
  for (player = 0; player < INPUT_PLAYERS; player++)
    slots[player][17][0] = (Slot){1, 0x01};
  x2_settings_defaults(&settings);
  pads[0] = "pad-a";
  pads[1] = "pad-b";
  controller_slots[0] = 0;
  controller_slots[1] = 1;
  CHECK(x2_settings_assign_controller(&settings, "pad-b", 1));
  CHECK(x2_settings_assign_controller(&settings, "pad-a", 2));
  CHECK(x2_settings_assign_keyboard(&settings, 2, 3));
  settings.keyboard_profile[2].keyboard_set[4] = 1;
  settings.keyboard_profile[2].keyboard[4] = 77;
  x2_player_input_sync(&cpu);

  /* Assignment establishes eligibility, not participation. Only P1 is
     default-active; P2-P4 remain inactive until their Start edge. */
  CHECK(participation_join == 0x01u);
  CHECK(participation_leave == 0u);
  participation_join = participation_leave = 0;

  /* Each persistent controller identity resolves only for its owner. */
  CHECK(slots[0][4][INPUT_BINDING_ALT_SLOT].kind == 0);
  CHECK(slots[1][4][INPUT_BINDING_ALT_SLOT].kind == 4);
  CHECK(slots[2][4][INPUT_BINDING_ALT_SLOT].kind == 3);
  CHECK(slots[1][4][INPUT_BINDING_ALT_SLOT].code == 0x15);
  CHECK(slots[2][8][INPUT_BINDING_ALT_SLOT].code == 0x06);
  CHECK(slots[0][4][0].kind == 1);
  CHECK(slots[1][4][0].kind == 0);
  CHECK(slots[3][4][0].kind == 1);
  CHECK(slots[3][4][0].code == 77);
  CHECK(!x2_player_input_uses_gamepad(0));
  CHECK(x2_player_input_resolved_pad(1) == 1);
  CHECK(x2_player_input_resolved_pad(2) == 0);

  /* Host inventory positions and retail controller-table slots are
     independent. An unresolved guest slot removes the binding; admission
     and later reordering must republish unchanged assignments. */
  controller_slots[1] = -1;
  x2_player_input_sync(&cpu);
  CHECK(x2_player_input_resolved_pad(1) == 1);
  CHECK(slots[1][4][INPUT_BINDING_ALT_SLOT].kind == 0);
  CHECK(!x2_player_input_uses_gamepad(1));
  CHECK(!x2_player_input_pad_is_active_source(1));
  controller_slots[1] = 0;
  controller_slots[0] = 1;
  x2_player_input_sync(&cpu);
  CHECK(slots[1][4][INPUT_BINDING_ALT_SLOT].kind == 3);
  CHECK(slots[2][4][INPUT_BINDING_ALT_SLOT].kind == 4);
  CHECK(x2_player_input_pad_is_active_source(1));
  CHECK(x2_player_input_pad_is_active_source(0));
  controller_slots[0] = 0;
  controller_slots[1] = 1;
  x2_player_input_sync(&cpu);

  gamepad_state[48 + 7] = 0x80;
  x2_player_input_note_gamepad_state(1, gamepad_state, sizeof gamepad_state);
  x2_player_input_sync(&cpu);
  CHECK(participation_join == 0x02u);
  CHECK(participation_leave == 0u);
  participation_join = participation_leave = 0;
  x2_player_input_note_gamepad_state(1, gamepad_state, sizeof gamepad_state);
  x2_player_input_sync(&cpu);
  CHECK(participation_join == 0u);
  memset(gamepad_state, 0, sizeof gamepad_state);
  x2_player_input_note_gamepad_state(1, gamepad_state, sizeof gamepad_state);

  /* The exact production publisher must carry each retail Pause input to
     every bank the game evaluates: master, working and menu. */
  for (row = 0; row < 3; row++) {
    static const unsigned bank[] = {0, 4, 12};
    CHECK(slots[bank[row] + 0][17][0].kind == 1);
    CHECK(slots[bank[row] + 0][17][0].code == 0x01);
    CHECK(slots[bank[row] + 1][17][INPUT_BINDING_ALT_SLOT].kind == 4);
    CHECK(slots[bank[row] + 1][17][INPUT_BINDING_ALT_SLOT].code == 0x1c);
    CHECK(slots[bank[row] + 2][17][INPUT_BINDING_ALT_SLOT].kind == 3);
    CHECK(slots[bank[row] + 2][17][INPUT_BINDING_ALT_SLOT].code == 0x1c);
  }

  /* Moving a keyboard row evicts the prior keyboard row for that player. */
  CHECK(x2_settings_assign_keyboard(&settings, 2, 0));
  x2_player_input_sync(&cpu);
  CHECK(slots[0][4][0].code == 77);
  CHECK(participation_leave == 0x08u);
  participation_leave = 0;

  /* Disconnecting an assigned pad leaves its association reserved. */
  pads[1] = NULL;
  x2_player_input_sync(&cpu);
  CHECK(slots[1][4][INPUT_BINDING_ALT_SLOT].kind == 0);
  CHECK(slots[2][4][INPUT_BINDING_ALT_SLOT].kind == 3);
  CHECK(slots[0][4][0].kind == 1);
  CHECK(!x2_player_input_uses_gamepad(1));
  CHECK(participation_leave == 0u);

  pads[1] = "pad-b";
  x2_player_input_sync(&cpu);
  CHECK(x2_player_input_resolved_pad(1) == 1);

  /* A shared physical Pause key is ambiguous: P1 pressing Escape must not
     accidentally join an eligible P4 profile. Giving P4 a distinct mapped
     Pause action then joins only P4, and a held key does not repeat it. */
  CHECK(x2_settings_assign_keyboard(&settings, 0, 3));
  x2_player_input_sync(&cpu);
  CHECK(participation_join == 0u);
  keyboard_state[0x01] = 0x80;
  x2_player_input_note_keyboard_state(keyboard_state, sizeof keyboard_state);
  x2_player_input_sync(&cpu);
  CHECK(participation_join == 0u);
  keyboard_state[0x01] = 0;
  x2_player_input_note_keyboard_state(keyboard_state, sizeof keyboard_state);
  settings.keyboard_profile[0].keyboard_set[17] = 1;
  settings.keyboard_profile[0].keyboard[17] = 0x1c;
  x2_player_input_sync(&cpu);
  keyboard_state[0x1c] = 0x80;
  x2_player_input_note_keyboard_state(keyboard_state, sizeof keyboard_state);
  x2_player_input_sync(&cpu);
  CHECK(participation_join == 0x08u);
  participation_join = 0;
  x2_player_input_note_keyboard_state(keyboard_state, sizeof keyboard_state);
  x2_player_input_sync(&cpu);
  CHECK(participation_join == 0u);
  keyboard_state[0x1c] = 0;
  x2_player_input_note_keyboard_state(keyboard_state, sizeof keyboard_state);

  /* Keyboard plus a specific controller is implicit hotswap. Both binding
     sources are published simultaneously; prompt mode follows activity. */
  CHECK(x2_settings_assign_controller(&settings, "hot-pad", 0));
  pads[0] = "hot-pad";
  pads[1] = "pad-b";
  x2_player_input_sync(&cpu);
  CHECK(x2_player_input_resolved_pad(0) == 0);
  CHECK(slots[0][4][0].kind == 1);
  CHECK(slots[0][4][INPUT_BINDING_ALT_SLOT].kind == 3);
  CHECK(!x2_player_input_uses_gamepad(0));
  x2_player_input_note_gamepad_activity(0);
  CHECK(x2_player_input_uses_gamepad(0));
  keyboard_state[77] = 0x80;
  x2_player_input_note_keyboard_state(keyboard_state, sizeof keyboard_state);
  CHECK(!x2_player_input_uses_gamepad(0));

  /* Disconnect waits for the same persistent identity. A different pad in
     the reused slot is not adopted; the exact identity resumes hotswap. */
  pads[0] = NULL;
  x2_player_input_sync(&cpu);
  CHECK(!x2_player_input_uses_gamepad(0));
  pads[0] = "other-pad";
  x2_player_input_sync(&cpu);
  CHECK(x2_player_input_resolved_pad(0) == -1);
  pads[0] = "hot-pad";
  x2_player_input_sync(&cpu);
  CHECK(!x2_player_input_uses_gamepad(0));
  CHECK(x2_player_input_resolved_pad(0) == 0);

  /* Removing P2's assignment forces leave. The disconnected stable ID was
     reserved above and never roamed; None is a distinct policy state. */
  CHECK(x2_settings_assign_controller(&settings, "pad-b",
                                      X2_SETTINGS_UNASSIGNED));
  x2_player_input_sync(&cpu);
  CHECK(participation_leave == 0x02u);
  participation_leave = 0;

  /* A session-only assignment is immediately usable but is a runtime
     overlay, not persisted settings. It suppresses P2's persisted keyboard
     without destroying it and does not activate P2 by assignment alone. */
  CHECK(x2_settings_assign_keyboard(&settings, 1, 1));
  x2_player_input_sync(&cpu);
  CHECK(slots[1][4][0].kind == 1);
  transient_pad[1] = 0;
  x2_player_input_sync(&cpu);
  CHECK(x2_player_input_resolved_pad(1) == 0);
  CHECK(x2_player_input_resolved_pad(0) == -1);
  CHECK(x2_settings_player_keyboard(&settings, 1) == 1);
  CHECK(slots[1][4][0].kind == 0);
  CHECK(participation_join == 0u);
  gamepad_state[48 + 7] = 0x80;
  x2_player_input_note_gamepad_state(0, gamepad_state, sizeof gamepad_state);
  x2_player_input_sync(&cpu);
  CHECK(participation_join == 0x02u);
  participation_join = 0;
  transient_pad[1] = -1;
  x2_player_input_sync(&cpu);
  CHECK(x2_player_input_resolved_pad(1) == -1);
  CHECK(slots[1][4][0].kind == 0);
  CHECK(slots[1][4][INPUT_BINDING_ALT_SLOT].kind == 0);
  CHECK(participation_leave == 0u);
  transient_pad[1] = -2;
  x2_player_input_sync(&cpu);
  CHECK(slots[1][4][0].kind == 1);
  CHECK(participation_leave == 0u);

  /* The unresolved transient also suppresses a live persisted pad rather
     than falling back to it. Clearing restores the persisted owner. */
  CHECK(x2_settings_assign_controller(&settings, "pad-b", 1));
  x2_player_input_sync(&cpu);
  CHECK(x2_player_input_resolved_pad(1) == 1);
  transient_pad[1] = -1;
  x2_player_input_sync(&cpu);
  CHECK(x2_player_input_resolved_pad(1) == -1);
  transient_pad[1] = -2;
  x2_player_input_sync(&cpu);
  CHECK(x2_player_input_resolved_pad(1) == 1);

  /* P1 alone keeps keyboard plus a transient controller simultaneously. */
  transient_pad[0] = 0;
  x2_player_input_sync(&cpu);
  CHECK(slots[0][4][0].kind == 1);
  CHECK(slots[0][4][INPUT_BINDING_ALT_SLOT].kind == 3);

  /* The retail Players page may change participation behind the host's
     transition policy. An unchanged None assignment is still enforced on
     every safe pump, without blindly requeueing a policy leave bit. */
  CHECK(x2_settings_assign_controller(&settings, "pad-a",
                                      X2_SETTINGS_UNASSIGNED));
  x2_player_input_sync(&cpu);
  participation_join = participation_leave = 0;
  participation_active |= 0x04u;
  x2_player_input_sync(&cpu);
  CHECK((participation_eligible & 0x04u) == 0u);
  CHECK((participation_active & 0x04u) == 0u);
  CHECK(participation_leave == 0u);

  printf("test_player_input: %d checks passed\n", checks);
  return 0;
}
