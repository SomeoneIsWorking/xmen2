#include "load_game_menu_runtime.h"

#include "exact_save_load.h"
#include "guest_heap.h"
#include "guest_memory.h"
#include "load_game_menu_policy.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "guest_body.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  EXE_PREFERRED = 0x00400000u,
  FN_INPUT_MANAGER = 0x001d8920u,
  FN_UPDATE_DIALOG_ORIGIN = 0x001e9c90u,
  FN_SCRIPT_ARGUMENT = 0x000d5830u,
  UI_SINGLETON = 0x004b13ecu,
  SAVE_MODE_LOAD = 3u,
  MANAGER_MODE = 0xd4u,
  MANAGER_SELECTION = 0xddu,
  MANAGER_METADATA = 0xe4u,
  METADATA_STRIDE = 0xa8u,
  METADATA_EMPTY = 0xa4u,
  UI_ACTIVE_PAGE = 0x403cu,
  PAGE_FIRST = 0x18u,
  PAGE_STRIDE = 0x1560u,
  PAGE_TEXT = 0x400u,
  PAGE_PRIMARY = 0x901u,
  PAGE_SECONDARY = 0xe02u,
  PAGE_PRIMARY_BITS = 0x1304u,
  PAGE_SECONDARY_BITS = 0x1308u,
  PAGE_SELECTABLE_BITS = 0x130cu,
  PAGE_COUNT = 0x1558u,
  PAGE_CAPACITY = 0x1559u,
  PAGE_ORIGIN = 0x155au,
  PAGE_FOCUS = 0x155bu,
  ROW_BYTES = 0x80u,
  AUTOSAVE_SCRIPT_SLOT = 10u
};

typedef struct {
  char text[ROW_BYTES];
  char primary[ROW_BYTES];
  char secondary[ROW_BYTES];
  unsigned primary_bit;
  unsigned secondary_bit;
  unsigned selectable_bit;
} ResidentRow;

static const char AUTOSAVE_LEAF[] = "autosave.save";
static const char AUTOSAVE_COMMAND[] = "saveloadChooseFile(10)";

static uint32_t g_exe;
static uint32_t g_ui;
static uint32_t g_page;
static uint32_t g_manager;
static uint32_t g_autosave_metadata;
static ResidentRow g_rows[X2_LOAD_GAME_MAX_ENTRIES];
static X2LoadGameMenuPlan g_plan;
static X2LoadGameMenuWindow g_window;
static unsigned g_refreshes;
static unsigned g_manual_choices;
static unsigned g_autosave_choices;
static int g_active;
static int g_last_manager_selection = -1;
static char g_selected_leaf[32];

static uint32_t exe_base(void) {
  const X86Module *module;

  if (g_exe)
    return g_exe;
  for (module = x86_modules(); module; module = module->next)
    if (module->preferred == EXE_PREFERRED && *module->base) {
      g_exe = *module->base;
      break;
    }
  return g_exe;
}

static uint32_t dialog_page(uint32_t ui) {
  uint32_t index;

  if (!ui)
    return 0u;
  index = RD32(ui + UI_ACTIVE_PAGE);
  if (index >= 3u)
    index = 0u;
  return ui + PAGE_FIRST + index * PAGE_STRIDE;
}

static int row_bit(uint32_t page, uint32_t offset, unsigned row) {
  return (RD32(page + offset) & (1u << row)) != 0u;
}

static void capture_row(uint32_t page, unsigned resident, ResidentRow *row) {
  memcpy(row->text,
         guest_memory_pointer(page + PAGE_TEXT + resident * ROW_BYTES),
         ROW_BYTES);
  memcpy(row->primary,
         guest_memory_pointer(page + PAGE_PRIMARY + resident * ROW_BYTES),
         ROW_BYTES);
  memcpy(row->secondary,
         guest_memory_pointer(page + PAGE_SECONDARY + resident * ROW_BYTES),
         ROW_BYTES);
  row->primary_bit = row_bit(page, PAGE_PRIMARY_BITS, resident);
  row->secondary_bit = row_bit(page, PAGE_SECONDARY_BITS, resident);
  row->selectable_bit = row_bit(page, PAGE_SELECTABLE_BITS, resident);
}

static void copy_row_to_guest(uint32_t page, unsigned resident,
                              const ResidentRow *row) {
  memcpy(guest_memory_pointer(page + PAGE_TEXT + resident * ROW_BYTES),
         row->text, ROW_BYTES);
  memcpy(guest_memory_pointer(page + PAGE_PRIMARY + resident * ROW_BYTES),
         row->primary, ROW_BYTES);
  memcpy(guest_memory_pointer(page + PAGE_SECONDARY + resident * ROW_BYTES),
         row->secondary, ROW_BYTES);
}

static void clear_row(uint32_t page, unsigned resident) {
  memset(guest_memory_pointer(page + PAGE_TEXT + resident * ROW_BYTES), 0,
         ROW_BYTES);
  memset(guest_memory_pointer(page + PAGE_PRIMARY + resident * ROW_BYTES), 0,
         ROW_BYTES);
  memset(guest_memory_pointer(page + PAGE_SECONDARY + resident * ROW_BYTES), 0,
         ROW_BYTES);
}

static void update_selected_leaf(void) {
  const X2LoadGameEntry *entry;

  g_selected_leaf[0] = 0;
  if (!g_active || g_window.selected >= g_plan.count)
    return;
  entry = &g_plan.entries[g_window.selected];
  if (entry->kind == X2_LOAD_GAME_AUTOSAVE)
    snprintf(g_selected_leaf, sizeof g_selected_leaf, "%s", AUTOSAVE_LEAF);
  else
    snprintf(g_selected_leaf, sizeof g_selected_leaf, "saveslot%u.save",
             entry->manual_slot);
}

static void refresh_projection(void) {
  uint32_t primary_bits = 0u;
  uint32_t secondary_bits = 0u;
  uint32_t selectable_bits = 0u;
  size_t count;
  size_t focus;
  unsigned resident;

  if (!g_active || !g_page)
    return;
  count = x2_load_game_menu_window_count(&g_plan, &g_window);
  focus = x2_load_game_menu_window_focus(&g_plan, &g_window);
  if (count > X2_LOAD_GAME_VISIBLE_ROWS || focus >= count) {
    g_active = 0;
    return;
  }
  for (resident = 0u; resident < X2_LOAD_GAME_VISIBLE_ROWS; resident++) {
    X2LoadGameEntry entry;
    const ResidentRow *row;

    if (resident >= count) {
      clear_row(g_page, resident);
      continue;
    }
    if (!x2_load_game_menu_window_entry(&g_plan, &g_window, resident, &entry)) {
      g_active = 0;
      return;
    }
    row = &g_rows[g_window.first + resident];
    copy_row_to_guest(g_page, resident, row);
    if (row->primary_bit)
      primary_bits |= 1u << resident;
    if (row->secondary_bit)
      secondary_bits |= 1u << resident;
    if (row->selectable_bit)
      selectable_bits |= 1u << resident;
  }
  WR32(g_page + PAGE_PRIMARY_BITS, primary_bits);
  WR32(g_page + PAGE_SECONDARY_BITS, secondary_bits);
  WR32(g_page + PAGE_SELECTABLE_BITS, selectable_bits);
  WR8(g_page + PAGE_COUNT, (uint8_t)count);
  WR8(g_page + PAGE_CAPACITY, X2_LOAD_GAME_VISIBLE_ROWS);
  WR8(g_page + PAGE_ORIGIN, 0u);
  WR8(g_page + PAGE_FOCUS, (uint8_t)focus);
  g_refreshes++;
  update_selected_leaf();
}

static uint16_t manual_present_mask(uint32_t manager) {
  uint16_t mask = 0u;
  unsigned slot;

  for (slot = 0u; slot < X2_LOAD_GAME_MANUAL_SLOTS; slot++) {
    uint32_t metadata = manager + MANAGER_METADATA + slot * METADATA_STRIDE;
    /* EMPTY alone controls residency. Header failure at +0xa5 still
       produces retail's saveloadChooseCorrupt() row, which is captured
       and projected unchanged with the other manual-slot rows. */
    if (RD8(metadata + METADATA_EMPTY) == 0u)
      mask |= (uint16_t)(1u << slot);
  }
  return mask;
}

static void make_autosave_row(ResidentRow *row) {
  char description[96];
  size_t length = 0u;

  memset(row, 0, sizeof *row);
  while (length + 1u < sizeof description &&
         RD8(g_autosave_metadata + (uint32_t)length)) {
    description[length] = (char)RD8(g_autosave_metadata + (uint32_t)length);
    length++;
  }
  description[length] = 0;
  if (length)
    snprintf(row->text, sizeof row->text, "AUTOSAVE - %s", description);
  else
    snprintf(row->text, sizeof row->text, "AUTOSAVE");
  snprintf(row->primary, sizeof row->primary, "%s", AUTOSAVE_COMMAND);
  row->primary_bit = 1u;
  row->selectable_bit = 1u;
}

static void activate_projection(const CPU *source, uint32_t manager) {
  uint16_t mask;
  size_t manual_count;
  size_t logical;
  unsigned retail_count;

  g_active = 0;
  g_selected_leaf[0] = 0;
  if (!exe_base() || RD32(manager + MANAGER_MODE) != SAVE_MODE_LOAD)
    return;
  g_ui = RD32(g_exe + UI_SINGLETON);
  g_page = dialog_page(g_ui);
  if (!g_ui || !g_page)
    return;
  if (!g_autosave_metadata)
    g_autosave_metadata = guest_malloc(METADATA_STRIDE);
  if (!g_autosave_metadata ||
      !x2_exact_save_load_read_header(source, g_exe, AUTOSAVE_LEAF,
                                      g_autosave_metadata))
    return;

  mask = manual_present_mask(manager);
  x2_load_game_menu_plan(mask, 1, &g_plan);
  manual_count = g_plan.count - 1u;
  retail_count = RD8(g_page + PAGE_COUNT);
  if (manual_count && retail_count != manual_count)
    return;
  memset(g_rows, 0, sizeof g_rows);
  for (logical = 0u; logical < manual_count; logical++)
    capture_row(g_page, (unsigned)logical, &g_rows[logical]);
  make_autosave_row(&g_rows[manual_count]);
  x2_load_game_menu_window_init(&g_plan, &g_window);
  g_manager = manager;
  g_active = 1;
  refresh_projection();
}

static int projection_is_current(uint32_t ui) {
  return g_active && ui == g_ui && dialog_page(ui) == g_page && g_manager &&
         RD32(g_manager + MANAGER_MODE) == SAVE_MODE_LOAD;
}

static int poll_navigation_delta(const CPU *source, uint32_t ui, int *delta) {
  CPU call = *source;
  uint32_t input;
  uint32_t sample;
  uint32_t target;

  call.esp -= 4u;
  sample = call.esp;
  WR32(sample, ui & 0xfffc0000u);
  x86_guest_call_args(&call, g_exe + FN_INPUT_MANAGER, 0u);
  input = call.eax;
  if (!input)
    return 0;
  target = RD32(RD32(input) + 0x1f8u);
  if (!target)
    return 0;
  call.esp -= 4u;
  WR32(call.esp, 0u);
  call.esp -= 4u;
  WR32(call.esp, sample);
  call.ecx = input;
  x86_guest_call_args(&call, target, 8u);
  *delta = (int)(int8_t)RD8(sample + 1u);
  return 1;
}

static void update_origin_and_play_focus_sound(const CPU *source, uint32_t ui) {
  CPU call = *source;
  uint32_t input;
  uint32_t target;

  call.ecx = ui;
  x86_guest_call_args(&call, g_exe + FN_UPDATE_DIALOG_ORIGIN, 0u);
  call = *source;
  x86_guest_call_args(&call, g_exe + FN_INPUT_MANAGER, 0u);
  input = call.eax;
  if (!input)
    return;
  target = RD32(RD32(input) + 0xe8u);
  if (!target)
    return;
  call.esp -= 4u;
  WR32(call.esp, 0u);
  call.ecx = input;
  x86_guest_call_args(&call, target, 4u);
}

static int script_integer_argument(const CPU *source, int *value) {
  CPU call = *source;
  uint32_t argument;
  uint32_t target;

  call.ecx = RD32(source->esp + 4u);
  call.esp -= 4u;
  WR32(call.esp, 0u);
  x86_guest_call_args(&call, g_exe + FN_SCRIPT_ARGUMENT, 4u);
  argument = call.eax;
  if (!argument)
    return 0;
  target = RD32(RD32(argument) + 0x10u);
  if (!target)
    return 0;
  call.ecx = argument;
  x86_guest_call_args(&call, target, 0u);
  *value = (int)call.eax;
  return 1;
}

static void x2_override_004b0d20(CPU *C) {
  uint32_t manager = C->ecx;

  x86_guest_body(C, "XMen2.exe", 0x004b0d20u);
  activate_projection(C, manager);
}

static void x2_override_005e9d30(CPU *C) {
  uint32_t ui = C->ecx;
  size_t resident_focus;
  int delta;

  if (!projection_is_current(ui)) {
    x86_guest_body(C, "XMen2.exe", 0x005e9d30u);
    return;
  }
  resident_focus = RD8(g_page + PAGE_FOCUS);
  if (x2_load_game_menu_window_select(&g_plan, &g_window, resident_focus))
    update_selected_leaf();
  if (poll_navigation_delta(C, ui, &delta) && delta &&
      x2_load_game_menu_window_move(&g_plan, &g_window, delta)) {
    refresh_projection();
    update_origin_and_play_focus_sound(C, ui);
  }
  C->esp += 4u;
}

static void x2_override_0049f010(CPU *C) {
  int selection;

  if (!g_active || !script_integer_argument(C, &selection) ||
      selection != AUTOSAVE_SCRIPT_SLOT) {
    x86_guest_body(C, "XMen2.exe", 0x0049f010u);
    if (g_active && g_manager) {
      g_manual_choices++;
      g_last_manager_selection =
          (int)(int8_t)RD8(g_manager + MANAGER_SELECTION);
    }
    return;
  }
  if (x2_exact_save_load_start(C, g_exe, AUTOSAVE_LEAF, 0u,
                               X2_EXACT_SAVE_LOAD_MENU, NULL)) {
    g_autosave_choices++;
    g_last_manager_selection = (int)(int8_t)RD8(g_manager + MANAGER_SELECTION);
  } else {
    fprintf(stderr, "load-menu: exact autosave transaction refused; "
                    "the retail manager state was not changed\n");
  }
  C->eax = 0u;
  C->esp += 4u;
}

size_t x2_load_game_menu_runtime_report(char *out, size_t capacity) {
  int written;
  long selected = g_active && g_window.selected < g_plan.count
                      ? (long)g_window.selected
                      : -1l;
  size_t resident =
      g_active ? x2_load_game_menu_window_count(&g_plan, &g_window) : 0u;

  if (!out || !capacity)
    return 0u;
  written = snprintf(
      out, capacity,
      "load-menu logical=%zu resident=%zu first=%zu selected=%ld leaf=%s "
      "manager-selected=%d refresh=%u manual=%u autosave=%u\n",
      g_active ? g_plan.count : 0u, resident, g_active ? g_window.first : 0u,
      selected, g_selected_leaf[0] ? g_selected_leaf : "none",
      g_last_manager_selection, g_refreshes, g_manual_choices,
      g_autosave_choices);
  if (written < 0 || (size_t)written >= capacity)
    return 0u;
  return (size_t)written;
}

__attribute__((constructor)) static void x2_load_game_menu_register(void) {
  x86_register_override("XMen2.exe", 0x004b0d20u, x2_override_004b0d20);
  x86_register_override("XMen2.exe", 0x005e9d30u, x2_override_005e9d30);
  x86_register_override("XMen2.exe", 0x0049f010u, x2_override_0049f010);
}
