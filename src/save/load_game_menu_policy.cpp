#include "load_game_menu_policy.h"

#include <climits>

namespace x2::save {

namespace {

size_t visible_count(size_t total, const LoadGameMenuWindow *window) {
  const size_t first = window->first;
  size_t remaining;

  if (first >= total)
    return 0u;
  remaining = total - first;
  return remaining < window->rows ? remaining : window->rows;
}

} // namespace

void load_game_menu_plan(uint16_t manual_present_mask, unsigned autosaves,
                         LoadGameMenuPlan *out) {
  unsigned slot;
  unsigned autosave;

  if (!out)
    return;
  out->count = 0u;
  for (slot = 0u; slot < kLoadGameManualSlots; slot++) {
    LoadGameEntry *entry;

    if ((manual_present_mask & (uint16_t)(1u << slot)) == 0u)
      continue;
    entry = &out->entries[out->count++];
    entry->kind = LoadGameEntryKind::Manual;
    entry->manual_slot = slot;
    entry->autosave = UINT_MAX;
  }
  for (autosave = 0u; autosave < autosaves && autosave < kAutosaveSlots;
       autosave++) {
    LoadGameEntry *entry = &out->entries[out->count++];
    entry->kind = LoadGameEntryKind::Autosave;
    entry->manual_slot = UINT_MAX;
    entry->autosave = autosave;
  }
}

void load_game_menu_window_init(const LoadGameMenuPlan *plan,
                                LoadGameMenuWindow *window, size_t rows) {
  if (!window)
    return;
  window->rows = rows == 0u                     ? 1u
                 : rows > kLoadGameResidentRows ? kLoadGameResidentRows
                                                : rows;
  window->first = 0u;
  window->selected = plan && plan->count ? 0u : SIZE_MAX;
}

int load_game_menu_window_move(const LoadGameMenuPlan *plan,
                               LoadGameMenuWindow *window, int delta) {
  size_t selected;

  if (!plan || !window || window->selected >= plan->count || delta == 0)
    return 0;
  if (delta > 0) {
    size_t amount = (size_t)delta;
    selected = amount >= plan->count - window->selected
                   ? plan->count - 1u
                   : window->selected + amount;
  } else {
    size_t amount = (size_t)(-(delta + 1)) + 1u;
    selected = amount > window->selected ? 0u : window->selected - amount;
  }
  if (selected == window->selected)
    return 0;
  window->selected = selected;
  if (selected < window->first)
    window->first = selected;
  if (selected >= window->first + window->rows)
    window->first = selected - window->rows + 1u;
  return 1;
}

int load_game_menu_window_select(const LoadGameMenuPlan *plan,
                                 LoadGameMenuWindow *window, size_t row) {
  size_t logical;

  if (!plan || !window || row >= visible_count(plan->count, window))
    return 0;
  logical = window->first + row;
  if (logical == window->selected)
    return 0;
  window->selected = logical;
  return 1;
}

int load_game_menu_window_show(const LoadGameMenuPlan *plan,
                               LoadGameMenuWindow *window, size_t first,
                               size_t rows) {
  size_t last_first;
  size_t selected;
  LoadGameMenuWindow before;

  if (!plan || !window || plan->count == 0u)
    return 0;
  before = *window;
  window->rows = rows == 0u                     ? 1u
                 : rows > kLoadGameResidentRows ? kLoadGameResidentRows
                                                : rows;
  last_first = plan->count > window->rows ? plan->count - window->rows : 0u;
  window->first = first > last_first ? last_first : first;
  selected = window->selected < plan->count ? window->selected : 0u;
  if (selected < window->first)
    selected = window->first;
  if (selected >= window->first + window->rows)
    selected = window->first + window->rows - 1u;
  window->selected = selected;
  return before.first != window->first || before.rows != window->rows ||
         before.selected != window->selected;
}

size_t load_game_menu_window_count(const LoadGameMenuPlan *plan,
                                   const LoadGameMenuWindow *window) {
  if (!plan || !window)
    return 0u;
  return visible_count(plan->count, window);
}

size_t load_game_menu_window_focus(const LoadGameMenuPlan *plan,
                                   const LoadGameMenuWindow *window) {
  if (!plan || !window || window->selected >= plan->count ||
      window->selected < window->first ||
      window->selected >= window->first + window->rows)
    return SIZE_MAX;
  return window->selected - window->first;
}

int load_game_menu_window_entry(const LoadGameMenuPlan *plan,
                                const LoadGameMenuWindow *window, size_t row,
                                LoadGameEntry *out) {
  size_t count;
  size_t logical;

  if (!plan || !window || !out)
    return 0;
  count = visible_count(plan->count, window);
  if (row >= count)
    return 0;
  logical = window->first + row;
  *out = plan->entries[logical];
  return 1;
}

} // namespace x2::save
