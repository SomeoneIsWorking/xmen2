#ifndef X2_CONTINUE_POLICY_H
#define X2_CONTINUE_POLICY_H

#include <stddef.h>

#define X2_MAIN_MENU_ROWS 6u

typedef enum {
  X2_MENU_TEXT_CONTINUE,
  X2_MENU_TEXT_NEW_GAME,
  X2_MENU_TEXT_LOAD_GAME,
  X2_MENU_TEXT_DANGER_ROOM,
  X2_MENU_TEXT_REVIEW,
  X2_MENU_TEXT_OPTIONS,
  X2_MENU_TEXT_PLAY_ONLINE,
  /* "Join <host>": the port's LAN row, its text supplied at runtime. */
  X2_MENU_TEXT_JOIN_LAN
} X2MainMenuText;

/* command_source values beyond the six shipped rows. */
#define X2_MENU_COMMAND_CONTINUE 6u
#define X2_MENU_COMMAND_JOIN_LAN 7u

typedef struct {
  X2MainMenuText text[X2_MAIN_MENU_ROWS];
  unsigned command_source[X2_MAIN_MENU_ROWS];
  int show_last_row;
  unsigned danger_row;
  int disable_online_special;
} X2ContinueMenuPlan;

typedef struct {
  int auto_ack_pending;
} X2ContinueTransaction;

/* command_source is an original shipped row index (0..5), or one of the
   X2_MENU_COMMAND_* port commands. The rows are, in order, Join (when a LAN
   game is announced), Continue (with a save), then the shipped rows; while
   more than six remain, Play Online leaves first and Review second. The plan
   never depends on previously-mutated menu state, so repeated Show calls
   cannot progressively shift the rows. */
void x2_continue_menu_plan(int has_save, int has_lan_game,
                           X2ContinueMenuPlan *out);

/* Choose the retail metadata staging record for an exact catalog leaf.
   Manual leaves keep their authored slot; autosave uses record zero only as
   temporary metadata while the one-shot leaf redirect owns the actual read. */
int x2_continue_leaf_slot(const char *leaf, unsigned *slot);

/* Native Continue is the only load source that arms success-dialog
   acknowledgement. A failed payload read or an unexpected completion state
   consumes the one-shot so a later manual Load can never inherit it. */
void x2_continue_transaction_begin(X2ContinueTransaction *transaction);
void x2_continue_transaction_reader_result(X2ContinueTransaction *transaction,
                                           int succeeded);
int x2_continue_transaction_take_success_ack(X2ContinueTransaction *transaction,
                                             unsigned manager_mode,
                                             unsigned manager_state);

#endif
