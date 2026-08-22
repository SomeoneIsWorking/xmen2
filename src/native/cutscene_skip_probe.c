/* The live input boundary for the two retail cutscene-skip paths.
 *
 * XMen2.exe owns the behavior: movie-menu update 0x005ca110 consumes action
 * bit 19, while scripted-cinematic update 0x0059f1a0 consumes action bit 20.
 * FUN_00619c40 maps both to row 17 (Pause). This probe does not decide to
 * skip; it shows whether Escape/Start were published and whether the exact
 * bits the retail code reads are down. A failure downstream of a down bit is
 * therefore movie/cinematic state policy, not host input publication.
 */
#include "cutscene_skip_probe.h"

#include "cutscene_skip_publication.h"
#include "input_bindings.h"
#include "rmlui_ui.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdarg.h>
#include <stdio.h>

#define PAUSE_ROW              17u
#define FMV_SKIP_ACTION        19u
#define CINEMATIC_SKIP_ACTION  20u
#define VT_FMV_ACTION_MASK     0x12cu
#define VT_CINEMATIC_MASK      0x140u
#define DIK_ESCAPE             0x01u
#define PAD_START              0x1cu

static void append(char *out, size_t size, size_t *at, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

static void append(char *out, size_t size, size_t *at, const char *fmt, ...)
{
    va_list args;
    int wrote;

    if (*at >= size) return;
    va_start(args, fmt);
    wrote = vsnprintf(out + *at, size - *at, fmt, args);
    va_end(args);
    if (wrote > 0)
        *at += (size_t)wrote > size - *at ? size - *at : (size_t)wrote;
}

static uint32_t call0(CPU *cpu, uint32_t object, uint32_t slot, int *readable)
{
    CPU call;
    uint32_t vtable = 0, function = 0;

    *readable = 0;
    if (!cpu || !object || !x86_peek32(object, &vtable) ||
        !x86_peek32(vtable + slot, &function) || !function)
        return 0;
    call = *cpu;
    call.ecx = object;
    x86_guest_call_args(&call, function, 0u);
    *readable = 1;
    return call.eax;
}

static CutsceneSkipPublicationBank publication_at(uint32_t controller)
{
    CutsceneSkipPublicationBank result = {0};
    uint32_t object;
    unsigned slot;
    char why[160];

    object = input_bindings_object_at(controller, why, (int)sizeof why);
    if (!object) return result;
    result.readable = 1;
    for (slot = 0; slot < INPUT_BINDING_SLOTS; slot++) {
        uint32_t kind = 0, code = 0;
        if (!input_bindings_read(object, PAUSE_ROW, slot, &kind, &code)) {
            result.readable = 0;
            continue;
        }
        if (kind == 1u && code == DIK_ESCAPE) result.escape = 1;
        if (kind >= 3u && kind <= 0xcu && code == PAD_START) result.start = 1;
    }
    return result;
}

static void report_publication(char *out, size_t size, size_t *at,
                               unsigned player)
{
    static const char *const NAME[INPUT_BINDING_SETS] = {
        "master", "working", "menu"
    };
    const uint32_t controller[INPUT_BINDING_SETS] = {
        INPUT_SET_MASTER + player,
        INPUT_SET_WORKING + player,
        INPUT_SET_MENU + player
    };
    CutsceneSkipPublicationBank found[INPUT_BINDING_SETS];
    CutsceneSkipPublicationSummary summary;
    unsigned bank;

    for (bank = 0; bank < INPUT_BINDING_SETS; bank++) {
        found[bank] = publication_at(controller[bank]);
        append(out, size, at,
               "  %-7s controller %u row 17: Escape %s, Start %s%s\n",
               NAME[bank], controller[bank],
               found[bank].escape ? "yes" : "no",
               found[bank].start ? "yes" : "no",
               found[bank].readable ? "" : "  (UNREADABLE)");
    }
    summary = cutscene_skip_publication_classify(found);
    append(out, size, at,
           "  publication: Escape %u/%u banks%s; Start %u/%u banks%s; "
           "%u/%u readable\n",
           summary.escape, INPUT_BINDING_SETS,
           summary.escape == INPUT_BINDING_SETS ? " (complete)" : " (MISSING)",
           summary.start, INPUT_BINDING_SETS,
           summary.start == INPUT_BINDING_SETS ? " (complete)" : " (MISSING)",
           summary.readable, INPUT_BINDING_SETS);
}

size_t cutscene_skip_probe_report(CPU *cpu, unsigned controller,
                                  uint32_t input_manager,
                                  char *out, size_t size)
{
    size_t at = 0;
    unsigned player = controller % INPUT_PLAYERS;
    int fmv_row, cinematic_row, fmv_readable, cinematic_readable;
    int ui_capture;
    uint32_t fmv_mask, cinematic_mask;

    if (!out || !size) return 0;
    fmv_row = input_binding_row_of_action(cpu, FMV_SKIP_ACTION);
    cinematic_row = input_binding_row_of_action(cpu, CINEMATIC_SKIP_ACTION);
    fmv_mask = call0(cpu, input_manager, VT_FMV_ACTION_MASK, &fmv_readable);
    cinematic_mask = call0(cpu, input_manager, VT_CINEMATIC_MASK,
                           &cinematic_readable);
    ui_capture = x2_ui_captures_input();

    append(out, size, &at, "cutscene skip input boundary -- player %u\n",
           player + 1u);
    append(out, size, &at, "  host modal UI capture: %s%s\n",
           ui_capture ? "yes" : "no",
           ui_capture
               ? " (game DirectInput is intentionally zeroed; Escape closes "
                 "the settings UI, not the cutscene)" : "");
    append(out, size, &at,
           "  retail map: FMV action 19 -> row %d; scripted cinematic "
           "action 20 -> row %d%s\n",
           fmv_row, cinematic_row,
           fmv_row == (int)PAUSE_ROW && cinematic_row == (int)PAUSE_ROW
               ? "  (both Pause)" : "  (UNEXPECTED RETAIL MAP)");
    report_publication(out, size, &at, player);
    if (fmv_readable)
        append(out, size, &at, "  FMV mask       0x%08x: action 19 %s\n",
               fmv_mask, fmv_mask & (1u << FMV_SKIP_ACTION) ? "DOWN" : "up");
    else
        append(out, size, &at,
               "  FMV mask: UNREADABLE at input vtable +0x12c\n");
    if (cinematic_readable)
        append(out, size, &at,
               "  cinematic mask 0x%08x: action 20 %s\n", cinematic_mask,
               cinematic_mask & (1u << CINEMATIC_SKIP_ACTION) ? "DOWN" : "up");
    else
        append(out, size, &at,
               "  cinematic mask: UNREADABLE at input vtable +0x140\n");
    append(out, size, &at,
           "  boundary rule: DOWN here proves host input and retail action "
           "publication; any refusal after it belongs to the active retail "
           "movie/cinematic state and its authored cleanup policy.\n\n");
    return at;
}
