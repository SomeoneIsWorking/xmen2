#include "load_game_menu_policy.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>

static int checks;
static int failures;
#define CHECK(x)                                                               \
  do {                                                                         \
    checks++;                                                                  \
    if (!(x)) {                                                                \
      failures++;                                                              \
      fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #x);  \
    }                                                                          \
  } while (0)

static void test_empty_and_sparse(void) {
  x2::save::LoadGameMenuPlan plan;
  x2::save::LoadGameMenuWindow window;
  x2::save::LoadGameEntry entry;

  x2::save::load_game_menu_plan(0u, 0, &plan);
  x2::save::load_game_menu_window_init(&plan, &window);
  CHECK(plan.count == 0u);
  CHECK(window.selected == SIZE_MAX);
  CHECK(x2::save::load_game_menu_window_count(&plan, &window) == 0u);
  CHECK(x2::save::load_game_menu_window_focus(&plan, &window) == SIZE_MAX);
  CHECK(!x2::save::load_game_menu_window_entry(&plan, &window, 0u, &entry));
  CHECK(!x2::save::load_game_menu_window_move(&plan, &window, 1));

  x2::save::load_game_menu_plan((uint16_t)((1u << 0) | (1u << 4) | (1u << 9)),
                                1, &plan);
  x2::save::load_game_menu_window_init(&plan, &window);
  CHECK(plan.count == 4u);
  CHECK(plan.entries[0].kind == x2::save::LoadGameEntryKind::Manual);
  CHECK(plan.entries[0].manual_slot == 0u);
  CHECK(plan.entries[1].manual_slot == 4u);
  CHECK(plan.entries[2].manual_slot == 9u);
  CHECK(plan.entries[3].kind == x2::save::LoadGameEntryKind::Autosave);
  CHECK(plan.entries[3].manual_slot == UINT_MAX);
  CHECK(x2::save::load_game_menu_window_count(&plan, &window) == 4u);
}

static void test_full_profile_projection(void) {
  x2::save::LoadGameMenuPlan plan;
  x2::save::LoadGameMenuWindow window;
  x2::save::LoadGameEntry entry;
  unsigned manual_seen = 0u;
  unsigned autosave_seen = 0u;
  size_t row;

  x2::save::load_game_menu_plan(UINT16_C(0x03ff), 1, &plan);
  x2::save::load_game_menu_window_init(&plan, &window);
  CHECK(plan.count == x2::save::kLoadGameMaxEntries);
  CHECK(window.first == 0u);
  CHECK(window.selected == 0u);
  CHECK(x2::save::load_game_menu_window_count(&plan, &window) ==
        x2::save::kLoadGameVisibleRows);
  for (row = 0u; row < x2::save::kLoadGameVisibleRows; row++) {
    CHECK(x2::save::load_game_menu_window_entry(&plan, &window, row, &entry));
    CHECK(entry.kind == x2::save::LoadGameEntryKind::Manual);
    CHECK(entry.manual_slot == row);
  }

  CHECK(x2::save::load_game_menu_window_move(&plan, &window, 9));
  CHECK(window.first == 0u);
  CHECK(x2::save::load_game_menu_window_focus(&plan, &window) == 9u);
  CHECK(x2::save::load_game_menu_window_move(&plan, &window, 1));
  CHECK(window.first == 1u);
  CHECK(window.selected == 10u);
  CHECK(x2::save::load_game_menu_window_focus(&plan, &window) == 9u);
  CHECK(x2::save::load_game_menu_window_count(&plan, &window) == 10u);
  for (row = 0u; row < x2::save::kLoadGameVisibleRows; row++) {
    CHECK(x2::save::load_game_menu_window_entry(&plan, &window, row, &entry));
    if (entry.kind == x2::save::LoadGameEntryKind::Autosave) {
      autosave_seen++;
    } else {
      CHECK(entry.manual_slot >= 1u && entry.manual_slot <= 9u);
      manual_seen |= 1u << entry.manual_slot;
    }
  }
  CHECK(manual_seen == UINT16_C(0x03fe));
  CHECK(autosave_seen == 1u);
  CHECK(!x2::save::load_game_menu_window_move(&plan, &window, 1));
  CHECK(x2::save::load_game_menu_window_move(&plan, &window, -10));
  CHECK(window.first == 0u);
  CHECK(window.selected == 0u);
  CHECK(x2::save::load_game_menu_window_focus(&plan, &window) == 0u);
  CHECK(!x2::save::load_game_menu_window_move(&plan, &window, -1));
  CHECK(x2::save::load_game_menu_window_select(&plan, &window, 5u));
  CHECK(window.selected == 5u);
  CHECK(!x2::save::load_game_menu_window_select(&plan, &window, 5u));
  CHECK(!x2::save::load_game_menu_window_select(&plan, &window, 10u));
  CHECK(x2::save::load_game_menu_window_select(&plan, &window, 0u));
  CHECK(x2::save::load_game_menu_window_move(&plan, &window, 100));
  CHECK(window.selected == 10u);
  CHECK(window.first == 1u);
  CHECK(!x2::save::load_game_menu_window_move(&plan, &window, 100));
  CHECK(x2::save::load_game_menu_window_move(&plan, &window, -100));
  CHECK(window.selected == 0u);
  CHECK(window.first == 0u);
}

static void test_invalid_arguments(void) {
  x2::save::LoadGameMenuPlan plan;
  x2::save::LoadGameMenuWindow window;
  x2::save::LoadGameEntry entry;

  x2::save::load_game_menu_plan(UINT16_C(0xffff), 1, &plan);
  CHECK(plan.count == x2::save::kLoadGameMaxEntries);
  x2::save::load_game_menu_window_init(&plan, &window);
  CHECK(!x2::save::load_game_menu_window_entry(NULL, &window, 0u, &entry));
  CHECK(!x2::save::load_game_menu_window_entry(&plan, NULL, 0u, &entry));
  CHECK(!x2::save::load_game_menu_window_entry(&plan, &window, 0u, NULL));
  CHECK(!x2::save::load_game_menu_window_move(NULL, &window, 1));
  CHECK(!x2::save::load_game_menu_window_move(&plan, NULL, 1));
  CHECK(!x2::save::load_game_menu_window_move(&plan, &window, 0));
  CHECK(!x2::save::load_game_menu_window_select(NULL, &window, 0u));
  CHECK(!x2::save::load_game_menu_window_select(&plan, NULL, 0u));
  x2::save::load_game_menu_plan(0u, 0, NULL);
  x2::save::load_game_menu_window_init(&plan, NULL);
}

int main(void) {
  test_empty_and_sparse();
  test_full_profile_projection();
  test_invalid_arguments();
  printf("load_game_menu_policy: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
