/* The live input boundary for movies, retail cinematicStart, and gameplay-
 * authored BehavEd cutscenes.
 *
 * XMen2.exe owns the behavior: movie-menu update 0x005ca110 consumes action
 * bit 19, while scripted-cinematic update 0x0059f1a0 consumes action bit 20.
 * FUN_00619c40 maps both to row 17 (Pause). This probe does not decide to
 * skip. The production classifier reported below belongs to the BehavEd
 * player. Conversation records are payloads of that player, never the owner.
 */
#include "cutscene_skip_probe.h"

#include "cutscene_player.h"
#include "cutscene_dialogue.h"
#include "cutscene_script_audio.h"
#include "cutscene_skip_publication.h"
#include "conversation_player.h"
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

static void report_player(CPU *cpu, char *out, size_t size, size_t *at)
{
    CutsceneScriptAudioSnapshot audio;
    CutsceneDialogueSnapshot dialogue;
    static const char *const controls[] = {
        "unreadable", "locked", "released"
    };
    static const char *const conversations[] = {
        "inactive", "waiting", "deterministic", "choice", "unreadable"
    };
    CutscenePlayerSnapshot player;
    ConversationPlayerState conversation = conversation_player_state(cpu);
    unsigned payload = conversation >= CONVERSATION_PLAYER_INACTIVE &&
                               conversation <= CONVERSATION_PLAYER_CHOICE
        ? (unsigned)conversation
        : 4u;

    cutscene_player_snapshot(cpu, &player);
    cutscene_dialogue_snapshot(&dialogue);
    cutscene_script_audio_snapshot(&audio);
    append(out, size, at,
           "Cutscene player: active %u sequence %u; %u owned BehavEd context(s)\n",
           player.active, player.sequence, player.owned_contexts);
    append(out, size, at,
           "  input: %lu poll(s), %lu skip edge(s); policy: %u request(s), "
           "%u invocation(s), %u completion(s)\n",
           player.input_polls, player.input_edges, player.requests,
           player.invocations, player.completions);
    append(out, size, at,
           "  work: %lu authored step(s) (%lu BehavEd, %lu event), "
           "%lu conversation payload(s); "
           "contexts: %lu allocation(s), %lu inherited, %lu freed\n",
           player.authored_steps, player.behaved_steps, player.event_steps,
           player.conversation_payloads,
           player.allocations, player.inherited, player.freed);
    append(out, size, at,
           "  timed events: window %s owner 0x%08x, refused %u, "
           "%lu insertion fault(s)\n",
           player.event_window_active ? "active" : "inactive",
           player.event_owner, player.event_refused,
           player.event_insertion_faults);
    append(out, size, at,
           "                last event target 0x%08x descriptor 0x%08x\n",
           player.last_event_target, player.last_event_descriptor);
    append(out, size, at,
           "  control release: %lu authored, %lu consumed on the private "
           "cutscene timeline\n",
           player.releases, player.private_releases);
    append(out, size, at,
           "  one-step invariant: %lu same-frame, %lu same-guest-time; "
           "results completed/inactive/choice/no-progress/runaway/error "
           "%lu/%lu/%lu/%lu/%lu/%lu\n",
           player.same_frame, player.same_guest_time, player.results[0],
           player.results[1], player.results[2], player.results[3],
           player.results[4], player.results[5]);
    append(out, size, at,
           "  dialogue presentation: %lu ordinary response, %lu ordinary "
           "line start(s); skip stopped %lu active voice(s), suppressed "
           "%lu response and %lu line start(s), leaked %lu; last line "
           "presenter 0x%08x\n",
           dialogue.ordinary_response_starts,
           dialogue.ordinary_line_starts,
           dialogue.active_voice_stops,
           dialogue.suppressed_response_starts,
           dialogue.suppressed_line_starts,
           dialogue.skip_presentation_starts,
           dialogue.last_line_presenter);
    append(out, size, at,
           "                         manager 0x%08x, last stopped handle "
           "0x%08x\n",
           dialogue.last_manager, dialogue.last_stopped_handle);
    append(out, size, at,
           "  script sound commands: %lu ordinary, %lu silent; "
           "last context 0x%08x\n",
           audio.ordinary_commands, audio.silent_commands,
           audio.last_context);
    append(out, size, at,
           "  boundary: controls %s; conversation payload %s\n",
           controls[player.control_state <= 2u ? player.control_state : 0u],
           conversations[payload]);
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
    report_player(cpu, out, size, &at);
    append(out, size, &at,
           "  boundary rule: an action 20 edge asks the ported cutscene "
           "player to finish its control-lock epoch. It drains only causally "
           "owned timed events and BehavEd fibers; deterministic conversation "
           "records are subordinate payloads and a branch refuses completion.\n\n");
    return at;
}
