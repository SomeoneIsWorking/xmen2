#pragma once

#include <cstddef>
#include <cstdint>

namespace x2::save {

inline constexpr unsigned kLoadGameManualSlots = 10u;
inline constexpr unsigned kLoadGameVisibleRows = 10u;
inline constexpr unsigned kLoadGameMaxEntries = 11u;

enum class LoadGameEntryKind { Manual, Autosave };

struct LoadGameEntry {
  LoadGameEntryKind kind;
  unsigned manual_slot;
};

struct LoadGameMenuPlan {
  LoadGameEntry entries[kLoadGameMaxEntries];
  std::size_t count;
};

struct LoadGameMenuWindow {
  std::size_t first;
  std::size_t selected;
};

/* Manual saves retain numeric slot order. Autosave is the final logical entry,
   so the initial full-profile window remains the ten shipped manual rows. */
void load_game_menu_plan(std::uint16_t manual_present_mask, int has_autosave,
                         LoadGameMenuPlan *out);

/* The retail dialog list owns exactly ten resident command rows. This window
   projects up to eleven logical entries onto those rows without ever assigning
   the retail save manager a synthetic numeric slot. */
void load_game_menu_window_init(const LoadGameMenuPlan *plan,
                                LoadGameMenuWindow *window);
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
