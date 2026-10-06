#include "autosave_runtime.h"
#include "boot_mode_runtime.h"
#include "boot_player_selection.h"
#include "continue_policy.h"
#include "exact_save_load.h"
#include "guest_heap.h"
#include "guest_memory.h"
#include "lan_session.h"
#include "save_catalog.h"
#include "save_directory.h"
#include "save_trace_runtime.h"
#include "x2_log.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "guest_body.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  EXE_PREFERRED = 0x00400000u,
  FN_SUCCESS_CALLBACK = 0x0009f140u,
  FN_MAIN_MENU_HIDE = 0x001bb920u,
  FN_CONTINUE_CALLBACK = 0x001f2b70u,
  FN_INTERN_POOL = 0x00202200u,
  FN_INTERN = 0x0001a460u,
  FN_FIND_ITEM = 0x001adc10u,
  FN_SET_VISIBLE = 0x001ade70u,
  FN_SET_TEXT = 0x001adcf0u,
  FN_LOCALIZE = 0x00229bf0u,
  MANAGER_MODE = 0xd4u,
  MANAGER_STATE = 0xd8u,
  ITEM_COMMAND = 0x24u,
  DANGER_COMPARATOR = 0x002e6628u,
  ONLINE_COMPARATOR = 0x002e662cu,
  EMPTY_STRING = 0x00281968u,
  LABEL_OPTION09 = 0x002a1280u,
  LOADGAME_COMMAND = 0x002a385cu,
  MENU_MODE = 0x003298a8u,
  LAST_ROW = X2_MAIN_MENU_ROWS - 1u
};

/* The command the LAN Join row runs; registered by options_menu.c. */
static const char JOIN_LAN_COMMAND[] = "port_lan_join";

#define PRIMARY_LOCAL_PLAYER 0u

static const uint32_t LABEL_RVA[X2_MAIN_MENU_ROWS] = {0x002a135cu, 0x002a134cu,
                                                      0x002a1290u, 0x002a12d8u,
                                                      0x002a133cu, 0x002a1280u};
static const char *const TEXT[X2_MENU_TEXT_PLAY_ONLINE + 1u] = {
    "Continue", "new game", "load game",  "danger room",
    "review",   "options",  "Play Online"};

static uint32_t g_exe;
static uint32_t g_text[X2_MENU_TEXT_PLAY_ONLINE + 1u];
static uint32_t g_continue_command;
static uint32_t g_join_command;
static uint32_t g_join_text;
static uint32_t g_original_command[X2_MAIN_MENU_ROWS];
static int g_original_commands_ready;
static char g_latest_leaf[X2_SAVE_LEAF_CAPACITY];
static int g_strings_ready;
static int g_latest_ready;
static int g_continue_command_armed;
static int g_boot_load_pending;
static X2ContinueTransaction g_transaction;

static void continue_load_completed(int succeeded);

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

static uint32_t copy_guest_string(const char *text) {
  uint32_t address;
  size_t size = strlen(text) + 1u;

  address = guest_malloc((uint32_t)size);
  if (address)
    memcpy(guest_memory_pointer(address), text, size);
  return address;
}

static int prepare_strings(void) {
  unsigned i;
  if (g_strings_ready)
    return 1;
  for (i = 0; i <= X2_MENU_TEXT_PLAY_ONLINE; i++)
    if (!g_text[i])
      g_text[i] = copy_guest_string(TEXT[i]);
  for (i = 0; i <= X2_MENU_TEXT_PLAY_ONLINE; i++)
    if (!g_text[i])
      return 0;
  g_strings_ready = 1;
  return g_strings_ready;
}

static uint32_t guest_call0(const CPU *source, uint32_t target) {
  CPU call = *source;
  x86_guest_call_args(&call, target, 0u);
  return call.reg[kX86pEax];
}

/* The engine's interned copy of a command string, as authored rows hold. */
static uint32_t intern_command(const CPU *source, uint32_t text,
                               uint32_t bytes) {
  CPU call = *source;
  uint32_t pool;

  pool = guest_call0(source, g_exe + FN_INTERN_POOL);
  if (!pool)
    return 0;
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], 2u);
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], bytes);
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], text);
  call.reg[kX86pEcx] = pool;
  x86_guest_call_args(&call, g_exe + FN_INTERN, 12u);
  return call.reg[kX86pEax];
}

static uint32_t intern_join_command(const CPU *source) {
  uint32_t text = copy_guest_string(JOIN_LAN_COMMAND);
  uint32_t command;

  if (!text)
    return 0;
  command = intern_command(source, text, (uint32_t)sizeof JOIN_LAN_COMMAND);
  guest_free(text);
  return command;
}

/* The Join row's text, re-copied whenever the announced host changes. */
static uint32_t join_text(const char *label) {
  if (g_join_text) {
    if (!strcmp(guest_memory_const_pointer(g_join_text), label))
      return g_join_text;
    guest_free(g_join_text);
  }
  g_join_text = copy_guest_string(label);
  return g_join_text;
}

static uint32_t find_item(const CPU *source, uint32_t menu, unsigned row) {
  CPU call = *source;
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], g_exe + LABEL_RVA[row]);
  call.reg[kX86pEcx] = menu;
  x86_guest_call_args(&call, g_exe + FN_FIND_ITEM, 4u);
  return call.reg[kX86pEax];
}

static void set_visible(const CPU *source, uint32_t menu, unsigned row,
                        int visible) {
  CPU call = *source;
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], visible != 0);
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], g_exe + LABEL_RVA[row]);
  call.reg[kX86pEcx] = menu;
  x86_guest_call_args(&call, g_exe + FN_SET_VISIBLE, 8u);
}

static uint32_t localized(const CPU *source, uint32_t text) {
  CPU call = *source;
  uint32_t result;
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], text);
  x86_guest_call_args(&call, g_exe + FN_LOCALIZE, 0u);
  result = call.reg[kX86pEax];
  call.reg[kX86pEsp] += 4u;
  return result ? result : text;
}

static void set_text(const CPU *source, uint32_t menu, unsigned row,
                     uint32_t text) {
  CPU call = *source;
  uint32_t display = localized(source, text);
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], display);
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], g_exe + LABEL_RVA[row]);
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], menu);
  x86_guest_call_args(&call, g_exe + FN_SET_TEXT, 0u);
  call.reg[kX86pEsp] += 12u;
}

static int catalog_for_show(void) {
  const char *boot_leaf = x2_boot_mode_runtime_continue_leaf();
  const char *directory;
  X2SaveCandidate latest;
  int result;

  if (boot_leaf) {
    snprintf(g_latest_leaf, sizeof g_latest_leaf, "%s", boot_leaf);
    g_latest_ready = 1;
    return 1;
  }
  directory = x2_retail_save_directory();
  result = directory ? x2_save_catalog_latest(directory, &latest) : -1;
  if (result == 1) {
    memcpy(g_latest_leaf, latest.leaf, sizeof g_latest_leaf);
    g_latest_ready = 1;
    return 1;
  }
  g_latest_leaf[0] = 0;
  g_latest_ready = 0;
  if (result < 0)
    x2_log_error("continue: retail save directory is unavailable; "
                 "showing the no-save menu\n");
  return 0;
}

static uint32_t row_command(unsigned source) {
  if (source == X2_MENU_COMMAND_CONTINUE)
    return g_continue_command;
  if (source == X2_MENU_COMMAND_JOIN_LAN)
    return g_join_command;
  return g_original_command[source];
}

static void apply_menu_plan(const CPU *source, uint32_t menu, int has_save) {
  X2ContinueMenuPlan plan;
  uint32_t item[X2_MAIN_MENU_ROWS];
  const char *join_label = x2_lan_session_join_label();
  unsigned row;

  g_continue_command_armed = 0;
  if (!prepare_strings()) {
    x2_log_error("continue: guest string allocation failed; preserving "
                 "the shipped main menu\n");
    return;
  }
  if (!g_continue_command)
    g_continue_command = intern_command(source, g_exe + LOADGAME_COMMAND,
                                        (uint32_t)strlen("loadgame") + 1u);
  if (!g_join_command)
    g_join_command = intern_join_command(source);
  if (join_label && (!g_join_command || !join_text(join_label))) {
    x2_log_error("main menu: the LAN Join row could not be prepared; "
                 "leaving it out\n");
    join_label = NULL;
  }
  for (row = 0; row < X2_MAIN_MENU_ROWS; row++)
    item[row] = find_item(source, menu, row);
  for (row = 0; row < X2_MAIN_MENU_ROWS; row++)
    if (!item[row])
      return;
  if (!g_original_commands_ready) {
    for (row = 0; row < X2_MAIN_MENU_ROWS; row++) {
      g_original_command[row] = RD32(item[row] + ITEM_COMMAND);
    }
    g_original_commands_ready = 1;
  }

  x2_continue_menu_plan(has_save && g_continue_command != 0u,
                        join_label != NULL, &plan);
  g_continue_command_armed = plan.show_last_row;
  for (row = 0; row < X2_MAIN_MENU_ROWS; row++) {
    WR32(item[row] + ITEM_COMMAND, row_command(plan.command_source[row]));
    set_text(source, menu, row,
             plan.text[row] == X2_MENU_TEXT_JOIN_LAN ? g_join_text
                                                     : g_text[plan.text[row]]);
  }
  set_visible(source, menu, LAST_ROW,
              plan.show_last_row && RD32(g_exe + MENU_MODE) != 2u);
  WR32(g_exe + DANGER_COMPARATOR, g_exe + LABEL_RVA[plan.danger_row]);
  WR32(g_exe + ONLINE_COMPARATOR,
       g_exe + (plan.disable_online_special ? EMPTY_STRING : LABEL_OPTION09));
}

void x2_main_menu_refresh(CPU *cpu, uint32_t menu) {
  if (!exe_base() || !menu)
    return;
  apply_menu_plan(cpu, menu, catalog_for_show());
}

void x2_override_005c9260(CPU *C) {
  uint32_t menu = C->reg[kX86pEcx];
  int has_save;
  int boot_continue = x2_boot_mode_runtime_continue_leaf() != NULL;

  x2_continue_transaction_reader_result(&g_transaction, 0);
  x2_autosave_runtime_menu_show();
  x2_save_trace_menu_open();
  /* Retail reaches CMenuMain::Show with the player who dismissed the title
     screen selected. CMenu::Show copies that selection into CMenuMgr and
     clears CPadManager while the menu is active; CMenu::Hide restores it
     before the load transition. A presentation-bypassing boot has no title
     input, so supply the primary player at this exact ownership boundary. */
  if (boot_continue && !x2_boot_player_select_primary(C, PRIMARY_LOCAL_PLAYER))
    boot_continue = 0;
  x86_guest_body(C, "XMen2.exe", 0x005c9260u);
  if (!exe_base())
    return;
  has_save = catalog_for_show();
  apply_menu_plan(C, menu, has_save);
  if (has_save && g_continue_command && boot_continue) {
    CPU call = *C;
    /* A real menu selection leaves through CMenuMain::Hide before its
       command runs. That derived owner calls CMenu::Hide, which restores
       the player saved by Show, then performs the main-menu cleanup. */
    call.reg[kX86pEcx] = menu;
    x86_guest_call_args(&call, g_exe + FN_MAIN_MENU_HIDE, 0u);
    g_boot_load_pending = 1;
    call = *C;
    call.reg[kX86pEsp] -= 4u;
    WR32(call.reg[kX86pEsp], 0u);
    x86_guest_call_args(&call, g_exe + FN_CONTINUE_CALLBACK, 0u);
    call.reg[kX86pEsp] += 4u;
  }
}

static int start_latest_load(const CPU *source) {
  unsigned slot;

  if (!g_latest_ready || !prepare_strings() ||
      !x2_continue_leaf_slot(g_latest_leaf, &slot))
    return 0;
  if (!x2_exact_save_load_start(source, g_exe, g_latest_leaf, slot,
                                X2_EXACT_SAVE_LOAD_CONTINUE,
                                continue_load_completed))
    return 0;
  x2_continue_transaction_begin(&g_transaction);
  return 1;
}

/* Direct boot dispatch: run the one authoritative retail mode-3 chain right
   at the intercepted intro command instead of routing through the menu-map
   lifecycle. The boot's own intro phase has already executed `resetgame` and
   initialized the save manager by the time the command fires, so the state
   is the pristine one the chain expects; the LOAD SUCCESSFUL ack re-selects
   the primary player (the payload keys its party writes off CPadManager's
   current player), which is the piece the first direct attempt lacked.
   Returns 0 unchanged when anything refuses -- the caller falls back to the
   retail menu path. */
int x2_continue_boot_dispatch(struct X86pCpu *C) {
  if (!exe_base())
    return 0;
  if (!catalog_for_show())
    return 0;
  if (!start_latest_load(C))
    return 0;
  g_boot_load_pending = 1;
  x2_boot_mode_runtime_continue_started();
  return 1;
}

void x2_override_005f2b70(CPU *C) {
  if (!g_continue_command_armed) {
    x86_guest_body(C, "XMen2.exe", 0x005f2b70u);
    return;
  }
  if (start_latest_load(C))
    x2_boot_mode_runtime_continue_started();
  C->reg[kX86pEax] = 0u;
  C->reg[kX86pEsp] += 4u;
}

static void continue_load_completed(int succeeded) {
  x2_continue_transaction_reader_result(&g_transaction, succeeded);
}

void x2_override_004b1280(CPU *C) {
  uint32_t manager = C->reg[kX86pEcx];
  CPU call;

  x86_guest_body(C, "XMen2.exe", 0x004b1280u);
  if (!x2_continue_transaction_take_success_ack(&g_transaction,
                                                RD32(manager + MANAGER_MODE),
                                                RD32(manager + MANAGER_STATE)))
    return;
  if (g_boot_load_pending) {
    /* The menu lifecycle between our Show intercept and this ack clears
       CPadManager's current player (write-watch: select 0, Show clears,
       Hide restores, two later menu Shows clear and never restore), and
       the save payload's party writes key off that player -- boot ended
       with index -1 and every hero handle unresolved, which is exactly
       issue #83's speaker-collision precondition. Manual Continue ends
       at player 0 because the input-driven menu re-selects; a bypassing
       boot re-selects here, after the last menu Show and before the
       payload deserializes. */
    g_boot_load_pending = 0;
    x2_boot_player_select_primary(C, PRIMARY_LOCAL_PLAYER);
  }
  call = *C;
  x86_guest_call_args(&call, g_exe + FN_SUCCESS_CALLBACK, 0u);
}

__attribute__((constructor)) static void x2_continue_register(void) {
  x86_register_override("XMen2.exe", 0x005c9260u, x2_override_005c9260);
  x86_register_override("XMen2.exe", 0x005f2b70u, x2_override_005f2b70);
  x86_register_override("XMen2.exe", 0x004b1280u, x2_override_004b1280);
}
