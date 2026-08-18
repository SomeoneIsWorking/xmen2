#ifndef X2_INPUT_BINDINGS_H
#define X2_INPUT_BINDINGS_H

#include <stdint.h>

struct CPU;

/*
 * XMen2.exe's controller binding table.
 *
 * The game keeps one binding object per player at +0x18 of the controller
 * object at 0x00668f40. It holds 42 rows -- the actions FUN_0061b030 names and
 * persists under `Controls\Player%d\<name>1` -- and every row has four slots of
 * three dwords each: +4 is the device kind, +8 the code. FUN_006294b0 reads a
 * row back in the order 2, 0, 1, 1 and takes the first slot with a non-zero
 * device kind, which is why slot 2 is the pad slot and beats the keyboard.
 *
 * Device kinds: 1 keyboard, 2 mouse, 3..0xc gamepad 0..9 (FUN_006281f0).
 */
#define INPUT_BINDING_ROWS      42u
#define INPUT_BINDING_SLOTS      4u

/*
 * The controller objects, and which of them matter.
 *
 * FUN_0061b030 fills sixteen, in four banks of one per player, and they are
 * NOT interchangeable:
 *
 *   0..3    MASTER. The options UI edits these and the registry persists them
 *           as `Controls\Player%d\<row>1` (slot 0) and `<row>2` (slot 1).
 *   4..7    WORKING. FUN_0061b030 COPIES the master into these (FUN_00629490),
 *           and FUN_006285c0's tail evaluates exactly these -- so a binding
 *           that never reaches the working set is never read, however
 *           correctly it sits in the master.
 *   8..11   Set up in place with their own keys; never copied from a master.
 *   12..15  MENU. Also copied from the master, then given the hardcoded menu
 *           keys.
 *
 * After the copy, FUN_0061b030 writes its own keys into SLOTS 2 AND 3 of the
 * working and menu sets -- row 4 slot 2 is Return, which is why a dialog
 * prompt reads [ENTER]. Slot 1's default is 0 for every row, so slot 1 is the
 * free alternate binding and the only one a port may install into without
 * being overwritten by the game's own defaults.
 */
#define INPUT_CONTROLLERS 16u
#define INPUT_SET_MASTER   0u
#define INPUT_SET_WORKING  4u
#define INPUT_SET_MENU    12u
#define INPUT_PLAYERS      4u
#define INPUT_BINDING_ALT_SLOT 1u

/* Controller `index`'s binding object, or 0 -- in which case `why` says which
   step failed, because "the exe is not mapped", "the game has not built this
   controller yet" and "that index does not exist" are different answers and a
   caller acts differently on each. */
uint32_t input_bindings_object_at(uint32_t index, char *why, int whyn);

/* Player 0's, which is what the gameplay bindings mean by "the" table. */
uint32_t input_bindings_object(char *why, int whyn);

int  input_bindings_read(uint32_t object, uint32_t row, uint32_t slot,
                         uint32_t *kind, uint32_t *code);

/*
 * Write one binding into every set that carries `player`'s bindings: the
 * master, the working copy the game evaluates, and the menu copy. The game
 * publishes master to the other two once, at FUN_0061b030 time; anything
 * installed after that has to publish itself or it stays invisible to the
 * running game. Returns how many sets took the write, of INPUT_BINDING_SETS.
 */
#define INPUT_BINDING_SETS 3u
unsigned input_bindings_write_player(struct CPU *cpu, uint32_t player,
                                     uint32_t row, uint32_t slot,
                                     uint32_t kind, uint32_t code);
void input_bindings_write(struct CPU *cpu, uint32_t object, uint32_t row,
                          uint32_t slot, uint32_t kind, uint32_t code);

/* The exe's own name for a binding row, read out of FUN_0061b030's 42-entry
   name array. NULL for a row index outside the table. */
const char *input_binding_row_name(uint32_t row);

/* The action-id -> binding-row map, FUN_00619c40's jump table. -1 for an
   action the game binds to no row. Actions run 0..INPUT_ACTION_MAX-1. */
#define INPUT_ACTION_MAX 0x34u
int input_binding_row_of_action(struct CPU *cpu, uint32_t action);

#endif
