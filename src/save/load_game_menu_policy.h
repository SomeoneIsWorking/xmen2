#ifndef X2_LOAD_GAME_MENU_POLICY_H
#define X2_LOAD_GAME_MENU_POLICY_H

#include <stddef.h>
#include <stdint.h>

#define X2_LOAD_GAME_MANUAL_SLOTS 10u
#define X2_LOAD_GAME_VISIBLE_ROWS 10u
#define X2_LOAD_GAME_MAX_ENTRIES 11u

typedef enum {
    X2_LOAD_GAME_MANUAL,
    X2_LOAD_GAME_AUTOSAVE
} X2LoadGameEntryKind;

typedef struct {
    X2LoadGameEntryKind kind;
    unsigned manual_slot;
} X2LoadGameEntry;

typedef struct {
    X2LoadGameEntry entries[X2_LOAD_GAME_MAX_ENTRIES];
    size_t count;
} X2LoadGameMenuPlan;

typedef struct {
    size_t first;
    size_t selected;
} X2LoadGameMenuWindow;

/* Manual saves retain numeric slot order. Autosave is the final logical entry,
   so the initial full-profile window remains the ten shipped manual rows. */
void x2_load_game_menu_plan(uint16_t manual_present_mask, int has_autosave,
                            X2LoadGameMenuPlan *out);

/* The retail dialog list owns exactly ten resident command rows. This window
   projects up to eleven logical entries onto those rows without ever assigning
   the retail save manager a synthetic numeric slot. */
void x2_load_game_menu_window_init(const X2LoadGameMenuPlan *plan,
                                   X2LoadGameMenuWindow *window);
int x2_load_game_menu_window_move(const X2LoadGameMenuPlan *plan,
                                  X2LoadGameMenuWindow *window, int delta);
int x2_load_game_menu_window_select(const X2LoadGameMenuPlan *plan,
                                    X2LoadGameMenuWindow *window, size_t row);
size_t x2_load_game_menu_window_count(const X2LoadGameMenuPlan *plan,
                                      const X2LoadGameMenuWindow *window);
size_t x2_load_game_menu_window_focus(const X2LoadGameMenuPlan *plan,
                                      const X2LoadGameMenuWindow *window);
int x2_load_game_menu_window_entry(const X2LoadGameMenuPlan *plan,
                                   const X2LoadGameMenuWindow *window,
                                   size_t row, X2LoadGameEntry *out);

#endif /* X2_LOAD_GAME_MENU_POLICY_H */
