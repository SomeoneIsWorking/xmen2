#pragma once

#include <cstddef>
#include <cstdint>

#include "save_catalog.h"

namespace x2::save {

inline constexpr unsigned kLoadGameManualSlots = 10u;
inline constexpr unsigned kLoadGameResidentRows = 10u;
inline constexpr unsigned kLoadGameMaxEntries =
    kLoadGameManualSlots + kAutosaveSlots;

enum class LoadGameEntryKind { Manual, Autosave };

struct LoadGameEntry {
  LoadGameEntryKind kind;
  unsigned manual_slot;
  unsigned autosave;
};

struct LoadGameMenuPlan {
  LoadGameEntry entries[kLoadGameMaxEntries];
  std::size_t count;
};

struct LoadGameMenuWindow {
  std::size_t first;
  std::size_t selected;
  std::size_t rows;
};

/* Manual saves retain numeric slot order. The autosaves follow, newest first
   (`autosave` indexes save_catalog_autosaves), so the initial full-profile
   window remains the ten shipped manual rows. */
void load_game_menu_plan(std::uint16_t manual_present_mask, unsigned autosaves,
                         LoadGameMenuPlan *out);

/* The retail dialog list owns ten resident command rows and shows `rows` of
   them, the page's visible count (fewer on touch). This window projects up to
   kLoadGameMaxEntries logical entries onto the shown rows without ever
   assigning the retail save manager a synthetic numeric slot. */
void load_game_menu_window_init(const LoadGameMenuPlan *plan,
                                LoadGameMenuWindow *window, std::size_t rows);
int load_game_menu_window_move(const LoadGameMenuPlan *plan,
                               LoadGameMenuWindow *window, int delta);
int load_game_menu_window_select(const LoadGameMenuPlan *plan,
                                 LoadGameMenuWindow *window, std::size_t row);
std::size_t load_game_menu_window_count(const LoadGameMenuPlan *plan,
                                        const LoadGameMenuWindow *window);
std::size_t load_game_menu_window_focus(const LoadGameMenuPlan *plan,
                                        const LoadGameMenuWindow *window);
int load_game_menu_window_entry(const LoadGameMenuPlan *plan,
                                const LoadGameMenuWindow *window,
                                std::size_t row, LoadGameEntry *out);

} // namespace x2::save
