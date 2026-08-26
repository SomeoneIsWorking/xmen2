#include "boot_mode_runtime.h"
#include "boot_player_selection.h"
#include "autosave_runtime.h"
#include "continue_policy.h"
#include "guest_heap.h"
#include "save_catalog.h"
#include "save_directory.h"
#include "save_trace_runtime.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    EXE_PREFERRED = 0x00400000u,
    FN_SAVE_MANAGER = 0x000b2880u,
    FN_STORAGE = 0x0015e9a0u,
    FN_SET_MODE = 0x000aeb80u,
    FN_SET_DEVICE = 0x000aece0u,
    FN_CHOOSE_FILE = 0x000ae850u,
    FN_SUCCESS_CALLBACK = 0x0009f140u,
    FN_MAIN_MENU_HIDE = 0x001bb920u,
    FN_READ_HEADER = 0x0015f580u,
    FN_READ_LEAF = 0x0015fcd0u,
    FN_CONTINUE_CALLBACK = 0x001f2b70u,
    FN_INTERN_POOL = 0x00202200u,
    FN_INTERN = 0x0001a460u,
    FN_FIND_ITEM = 0x001adc10u,
    FN_SET_VISIBLE = 0x001ade70u,
    FN_SET_TEXT = 0x001adcf0u,
    FN_LOCALIZE = 0x00229bf0u,
    SAVE_MODE_LOAD = 3u,
    SAVE_DEVICE_PC = 0u,
    MANAGER_MODE = 0xd4u,
    MANAGER_STATE = 0xd8u,
    MANAGER_METADATA = 0xe4u,
    METADATA_STRIDE = 0xa8u,
    ITEM_COMMAND = 0x24u,
    DANGER_COMPARATOR = 0x002e6628u,
    ONLINE_COMPARATOR = 0x002e662cu,
    EMPTY_STRING = 0x00281968u,
    LABEL_OPTION06 = 0x002a1290u,
    LABEL_OPTION07 = 0x002a12d8u,
    LABEL_OPTION09 = 0x002a1280u,
    LOADGAME_COMMAND = 0x002a385cu,
    MENU_MODE = 0x003298a8u,
    CONTINUE_COMMAND_SOURCE = X2_MAIN_MENU_ROWS
};

#define PRIMARY_LOCAL_PLAYER 0u

static const uint32_t LABEL_RVA[X2_MAIN_MENU_ROWS] = {
    0x002a135cu, 0x002a134cu, 0x002a1290u,
    0x002a12d8u, 0x002a133cu, 0x002a1280u
};
static const char *const TEXT[X2_MENU_TEXT_PLAY_ONLINE + 1u] = {
    "Continue", "new game", "load game", "danger room",
    "review", "options", "Play Online"
};

static uint32_t g_exe;
static uint32_t g_text[X2_MENU_TEXT_PLAY_ONLINE + 1u];
static uint32_t g_continue_command;
static uint32_t g_original_command[X2_MAIN_MENU_ROWS];
static int g_original_commands_ready;
static char g_latest_leaf[X2_SAVE_LEAF_CAPACITY];
static uint32_t g_latest_leaf_guest;
static int g_strings_ready;
static int g_latest_ready;
static int g_leaf_redirect_pending;
static int g_continue_command_armed;
static int g_boot_load_pending;
static X2ContinueTransaction g_transaction;

void fn_XMen2_005c9260(CPU *C);
void fn_XMen2_005f2b70(CPU *C);
void fn_XMen2_0055ff00(CPU *C);
void fn_XMen2_004b1280(CPU *C);

static uint32_t exe_base(void)
{
    const X86Module *module;
    if (g_exe) return g_exe;
    for (module = x86_modules(); module; module = module->next)
        if (module->preferred == EXE_PREFERRED && *module->base) {
            g_exe = *module->base;
            break;
        }
    return g_exe;
}

static uint32_t copy_guest_string(const char *text)
{
    uint32_t address;
    size_t size = strlen(text) + 1u;

    address = guest_malloc((uint32_t)size);
    if (address) memcpy((void *)(uintptr_t)address, text, size);
    return address;
}

static int prepare_strings(void)
{
    unsigned i;
    if (g_strings_ready) return 1;
    if (!g_latest_leaf_guest)
        g_latest_leaf_guest = guest_malloc(X2_SAVE_LEAF_CAPACITY);
    for (i = 0; i <= X2_MENU_TEXT_PLAY_ONLINE; i++)
        if (!g_text[i]) g_text[i] = copy_guest_string(TEXT[i]);
    if (!g_latest_leaf_guest) return 0;
    for (i = 0; i <= X2_MENU_TEXT_PLAY_ONLINE; i++)
        if (!g_text[i]) return 0;
    g_strings_ready = 1;
    return g_strings_ready;
}

static uint32_t guest_call0(const CPU *source, uint32_t target)
{
    CPU call = *source;
    x86_guest_call_args(&call, target, 0u);
    return call.eax;
}

static uint32_t intern_command(const CPU *source)
{
    CPU call = *source;
    uint32_t pool;

    pool = guest_call0(source, g_exe + FN_INTERN_POOL);
    if (!pool) return 0;
    call.esp -= 4u; WR32(call.esp, 2u);
    call.esp -= 4u; WR32(call.esp, (uint32_t)strlen("loadgame") + 1u);
    call.esp -= 4u; WR32(call.esp, g_exe + LOADGAME_COMMAND);
    call.ecx = pool;
    x86_guest_call_args(&call, g_exe + FN_INTERN, 12u);
    return call.eax;
}

static uint32_t find_item(const CPU *source, uint32_t menu, unsigned row)
{
    CPU call = *source;
    call.esp -= 4u; WR32(call.esp, g_exe + LABEL_RVA[row]);
    call.ecx = menu;
    x86_guest_call_args(&call, g_exe + FN_FIND_ITEM, 4u);
    return call.eax;
}

static void set_visible(const CPU *source, uint32_t menu, unsigned row,
                        int visible)
{
    CPU call = *source;
    call.esp -= 4u; WR32(call.esp, visible != 0);
    call.esp -= 4u; WR32(call.esp, g_exe + LABEL_RVA[row]);
    call.ecx = menu;
    x86_guest_call_args(&call, g_exe + FN_SET_VISIBLE, 8u);
}

static uint32_t localized(const CPU *source, uint32_t text)
{
    CPU call = *source;
    uint32_t result;
    call.esp -= 4u; WR32(call.esp, text);
    x86_guest_call_args(&call, g_exe + FN_LOCALIZE, 0u);
    result = call.eax;
    call.esp += 4u;
    return result ? result : text;
}

static void set_text(const CPU *source, uint32_t menu, unsigned row,
                     X2MainMenuText text)
{
    CPU call = *source;
    uint32_t display = localized(source, g_text[text]);
    call.esp -= 4u; WR32(call.esp, display);
    call.esp -= 4u; WR32(call.esp, g_exe + LABEL_RVA[row]);
    call.esp -= 4u; WR32(call.esp, menu);
    x86_guest_call_args(&call, g_exe + FN_SET_TEXT, 0u);
    call.esp += 12u;
}

static int catalog_for_show(void)
{
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
        fprintf(stderr, "continue: retail save directory is unavailable; "
                        "showing the no-save menu\n");
    return 0;
}

static void apply_menu_plan(const CPU *source, uint32_t menu, int has_save)
{
    X2ContinueMenuPlan plan;
    uint32_t item[X2_MAIN_MENU_ROWS];
    unsigned row;

    g_continue_command_armed = 0;
    if (!prepare_strings()) {
        fprintf(stderr, "continue: guest string allocation failed; preserving "
                        "the shipped main menu\n");
        return;
    }
    if (!g_continue_command) g_continue_command = intern_command(source);
    for (row = 0; row < X2_MAIN_MENU_ROWS; row++)
        item[row] = find_item(source, menu, row);
    for (row = 0; row < X2_MAIN_MENU_ROWS; row++)
        if (!item[row]) return;
    if (!g_original_commands_ready) {
        for (row = 0; row < X2_MAIN_MENU_ROWS; row++) {
            g_original_command[row] = RD32(item[row] + ITEM_COMMAND);
        }
        g_original_commands_ready = 1;
    }

    x2_continue_menu_plan(has_save && g_continue_command != 0u, &plan);
    g_continue_command_armed = plan.show_last_row;
    for (row = 0; row < X2_MAIN_MENU_ROWS; row++) {
        unsigned source_row = plan.command_source[row];
        uint32_t command = source_row == CONTINUE_COMMAND_SOURCE
                               ? g_continue_command
                               : g_original_command[source_row];
        WR32(item[row] + ITEM_COMMAND, command);
        set_text(source, menu, row, plan.text[row]);
    }
    set_visible(source, menu, 5u,
                plan.show_last_row && RD32(g_exe + MENU_MODE) != 2u);
    WR32(g_exe + DANGER_COMPARATOR,
         g_exe + (plan.danger_row == 3u ? LABEL_OPTION07 : LABEL_OPTION06));
    WR32(g_exe + ONLINE_COMPARATOR,
         g_exe + (plan.disable_online_special ? EMPTY_STRING : LABEL_OPTION09));
}

void x2_override_005c9260(CPU *C)
{
    uint32_t menu = C->ecx;
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
    if (boot_continue &&
        !x2_boot_player_select_primary(C, PRIMARY_LOCAL_PLAYER))
        boot_continue = 0;
    fn_XMen2_005c9260(C);
    if (!exe_base()) return;
    has_save = catalog_for_show();
    apply_menu_plan(C, menu, has_save);
    if (has_save && g_continue_command && boot_continue) {
        CPU call = *C;
        /* A real menu selection leaves through CMenuMain::Hide before its
           command runs. That derived owner calls CMenu::Hide, which restores
           the player saved by Show, then performs the main-menu cleanup. */
        call.ecx = menu;
        x86_guest_call_args(&call, g_exe + FN_MAIN_MENU_HIDE, 0u);
        g_boot_load_pending = 1;
        call = *C;
        call.esp -= 4u; WR32(call.esp, 0u);
        x86_guest_call_args(&call, g_exe + FN_CONTINUE_CALLBACK, 0u);
        call.esp += 4u;
    }
}

static int start_latest_load(const CPU *source)
{
    CPU call;
    uint32_t manager;
    uint32_t storage;
    uint32_t entry;
    unsigned slot;

    if (!g_latest_ready || !prepare_strings()
        || !x2_continue_leaf_slot(g_latest_leaf, &slot)) return 0;
    manager = guest_call0(source, g_exe + FN_SAVE_MANAGER);
    storage = guest_call0(source, g_exe + FN_STORAGE);
    if (!manager || !storage || RD32(manager + MANAGER_MODE) != 0u) return 0;
    memcpy((void *)(uintptr_t)g_latest_leaf_guest, g_latest_leaf,
           strlen(g_latest_leaf) + 1u);

    call = *source;
    call.esp -= 4u; WR32(call.esp, SAVE_MODE_LOAD);
    call.ecx = manager;
    x86_guest_call_args(&call, g_exe + FN_SET_MODE, 4u);

    entry = manager + MANAGER_METADATA + slot * METADATA_STRIDE;
    memset((void *)(uintptr_t)entry, 0, METADATA_STRIDE);
    call = *source;
    call.esp -= 4u; WR32(call.esp, entry);
    call.esp -= 4u; WR32(call.esp, g_latest_leaf_guest);
    call.esp -= 4u; WR32(call.esp, SAVE_DEVICE_PC);
    call.ecx = storage;
    x86_guest_call_args(&call, g_exe + FN_READ_HEADER, 12u);

    call = *source;
    call.esp -= 4u; WR32(call.esp, SAVE_DEVICE_PC);
    call.ecx = manager;
    x86_guest_call_args(&call, g_exe + FN_SET_DEVICE, 4u);
    call = *source;
    call.esp -= 4u; WR32(call.esp, slot);
    call.ecx = manager;
    x86_guest_call_args(&call, g_exe + FN_CHOOSE_FILE, 4u);
    if (RD32(manager + MANAGER_MODE) != SAVE_MODE_LOAD
        || RD32(manager + MANAGER_STATE) != 0x1cu) {
        fprintf(stderr, "continue: retail manager refused mode3/state1c "
                        "dispatch (mode=%u state=%u); leaf redirect not armed\n",
                RD32(manager + MANAGER_MODE), RD32(manager + MANAGER_STATE));
        return 0;
    }
    x2_continue_transaction_begin(&g_transaction);
    g_leaf_redirect_pending = 1;
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
int x2_continue_boot_dispatch(struct CPU *C)
{
    if (!exe_base()) return 0;
    if (!catalog_for_show()) return 0;
    if (!start_latest_load(C)) return 0;
    g_boot_load_pending = 1;
    x2_boot_mode_runtime_continue_started();
    return 1;
}

void x2_override_005f2b70(CPU *C)
{
    if (!g_continue_command_armed) {
        fn_XMen2_005f2b70(C);
        return;
    }
    if (start_latest_load(C)) x2_boot_mode_runtime_continue_started();
    C->eax = 0u;
    C->esp += 4u;
}

void x2_override_0055ff00(CPU *C)
{
    int succeeded;

    if (!g_leaf_redirect_pending) {
        fn_XMen2_0055ff00(C);
        return;
    }
    g_leaf_redirect_pending = 0;
    WR32(C->esp + 8u, g_latest_leaf_guest);
    x86_dispatch(C, g_exe + FN_READ_LEAF);
    succeeded = (C->eax & 0xffu) != 0u;
    x2_continue_transaction_reader_result(
        &g_transaction, succeeded);
}

void x2_override_004b1280(CPU *C)
{
    uint32_t manager = C->ecx;
    CPU call;

    fn_XMen2_004b1280(C);
    if (!x2_continue_transaction_take_success_ack(
            &g_transaction, RD32(manager + MANAGER_MODE),
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

__attribute__((constructor))
static void x2_continue_register(void)
{
    x86_register_override("XMen2.exe", 0x005c9260u,
                          x2_override_005c9260);
    x86_register_override("XMen2.exe", 0x005f2b70u,
                          x2_override_005f2b70);
    x86_register_override("XMen2.exe", 0x0055ff00u,
                          x2_override_0055ff00);
    x86_register_override("XMen2.exe", 0x004b1280u,
                          x2_override_004b1280);
}
