#include "load_game_menu_policy.h"

#include <limits.h>

static size_t visible_count(size_t total, size_t first) {
  size_t remaining;

  if (first >= total)
    return 0u;
  remaining = total - first;
  return remaining < X2_LOAD_GAME_VISIBLE_ROWS ? remaining
                                               : X2_LOAD_GAME_VISIBLE_ROWS;
}

void x2_load_game_menu_plan(uint16_t manual_present_mask, int has_autosave,
                            X2LoadGameMenuPlan *out) {
  unsigned slot;

  if (!out)
    return;
  out->count = 0u;
  for (slot = 0u; slot < X2_LOAD_GAME_MANUAL_SLOTS; slot++) {
    X2LoadGameEntry *entry;

    if ((manual_present_mask & (uint16_t)(1u << slot)) == 0u)
      continue;
    entry = &out->entries[out->count++];
    entry->kind = X2_LOAD_GAME_MANUAL;
    entry->manual_slot = slot;
  }
  if (has_autosave) {
    X2LoadGameEntry *entry = &out->entries[out->count++];
    entry->kind = X2_LOAD_GAME_AUTOSAVE;
    entry->manual_slot = UINT_MAX;
  }
}

void x2_load_game_menu_window_init(const X2LoadGameMenuPlan *plan,
                                   X2LoadGameMenuWindow *window) {
  if (!window)
    return;
  window->first = 0u;
  window->selected = plan && plan->count ? 0u : SIZE_MAX;
}

int x2_load_game_menu_window_move(const X2LoadGameMenuPlan *plan,
                                  X2LoadGameMenuWindow *window, int delta) {
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
  if (selected >= window->first + X2_LOAD_GAME_VISIBLE_ROWS)
    window->first = selected - X2_LOAD_GAME_VISIBLE_ROWS + 1u;
  return 1;
}

int x2_load_game_menu_window_select(const X2LoadGameMenuPlan *plan,
                                    X2LoadGameMenuWindow *window, size_t row) {
  size_t logical;

  if (!plan || !window || row >= visible_count(plan->count, window->first))
    return 0;
  logical = window->first + row;
  if (logical == window->selected)
    return 0;
  window->selected = logical;
  return 1;
}

size_t x2_load_game_menu_window_count(const X2LoadGameMenuPlan *plan,
                                      const X2LoadGameMenuWindow *window) {
  if (!plan || !window)
    return 0u;
  return visible_count(plan->count, window->first);
}

size_t x2_load_game_menu_window_focus(const X2LoadGameMenuPlan *plan,
                                      const X2LoadGameMenuWindow *window) {
  if (!plan || !window || window->selected >= plan->count ||
      window->selected < window->first ||
      window->selected >= window->first + X2_LOAD_GAME_VISIBLE_ROWS)
    return SIZE_MAX;
  return window->selected - window->first;
}

int x2_load_game_menu_window_entry(const X2LoadGameMenuPlan *plan,
                                   const X2LoadGameMenuWindow *window,
                                   size_t row, X2LoadGameEntry *out) {
  size_t count;
  size_t logical;

  if (!plan || !window || !out)
    return 0;
  count = visible_count(plan->count, window->first);
  if (row >= count)
    return 0;
  logical = window->first + row;
  *out = plan->entries[logical];
  return 1;
}
