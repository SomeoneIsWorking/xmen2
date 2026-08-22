/*
 * "Did the game see it?" -- the live view of XMen2.exe's own input state.
 *
 * This exists because a whole class of input defect is invisible from either
 * end alone. The host layer can prove SDL delivered a button and that
 * DirectInput handed it to the guest; the screen can show that nothing
 * happened. Neither says whether the game's binding table resolved the press
 * into an action, and that is the layer where a working device still does
 * nothing.
 *
 * Everything here is READ-ONLY and is taken on the guest's own thread, at the
 * one pump point per frame where the guest is between operations (see
 * dinput_device.c's keyboard branch). Nothing here decides anything: it is an
 * instrument, and it is wired to the control channel so a run can be asked
 * where it is instead of being guessed at from a log afterwards.
 */
#include "input_probe.h"
#include "binding_rows.h"
#include "cutscene_skip_probe.h"
#include "input_bindings.h"
#include "dinput_pad.h"
#include "gpu_device.h"
#include "guest_clock.h"
#include "input_probe_lifecycle.h"
#include "player_participation_probe.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define EXE_PREFERRED   0x00400000u

/*
 * Guest addresses are written here EXACTLY as the disassembly writes them --
 * absolute, at the module's preferred base -- and the offset from the mapped
 * base is computed. Hand-subtracting 0x00400000 at each constant is an error
 * that produces a plausible pointer into unrelated data and a report full of
 * confident nonsense; it happened three times while this file was being
 * written, so the subtraction is done in one place instead.
 */
#define RVA(va)         ((uint32_t)(va) - EXE_PREFERRED)
#define INPUT_MGR_RVA   RVA(0x005d8920u)  /* FUN_005d8920, the input singleton    */
#define VT_ACTION_DOWN  0x138u       /* vtable slot the conversation gates on */

/*
 * The other half of the chain, and the one that actually holds live state.
 *
 * FUN_005d4970 -- what vtable +0x138 forwards to -- does not read the binding
 * table at all. It asks the PAD manager (FUN_00551ed0, the singleton at
 * 0x0079ebc0) for each player object and tests `1 << action` against a 32-bit
 * LOGICAL mask that object returns from its own vtable +0x18. So the accept
 * the conversation gates on is action BIT 4, not binding row 4 by coincidence
 * of numbering, and the binding table is what fills that mask rather than what
 * is read at the gate.
 *
 * Player objects are inline in the manager: `manager + 0x3c + player * 0x390`
 * (FUN_005510d0). Live PC measurements (C216) establish 30 physical floats at
 * +0x2fc; 0x2fc + 30*4 lands inside 0x390. This dumps the measured structure.
 */
#define PAD_MGR_RVA     RVA(0x00551ed0u)  /* FUN_00551ed0, the pad manager        */
#define PADMGR_CURPLAYER 0x34u       /* vt+0x60 is just `return [this+0x34]`  */

/*
 * Who the game thinks the player's character IS.
 *
 * FUN_00422210 asks the pad manager for the current player index, indexes the
 * handle table at 0x0070b814 with it (or falls back to 0x0072988c when the
 * index is out of 0..3 -- the manager constructs it as -1), and resolves that
 * handle through FUN_004654b0. Whatever comes back is the actor the rest of
 * the game treats as "the player".
 *
 * It is reported here because a null answer is load-bearing far away from
 * input: igConversationManager::start uses it to pick which of two code paths
 * names a conversation's speaker, and with no speaker it takes a fallback that
 * indexes the seen-line bitmap from 0 -- which is what makes the tutorial's
 * second conversation collide with its first (issue #83).
 */
#define HERO_HANDLES_RVA RVA(0x0070b814u)  /* four handles             */
#define HERO_FALLBACK_RVA RVA(0x0072988cu)  /* used when the index is -1 */
#define RESOLVE_HANDLE_RVA RVA(0x004654b0u) /* FUN_004654b0(this=&handle) -> actor */

/* Where FUN_00619e30 sprintf()s "[%s]" -- the on-screen prompt label, after
   FUN_006281f0 has named the binding. Printed with its bytes, because the
   whole question for the Xbox prompts is whether a byte the port put there
   survives to the draw, and a glyph that renders as nothing looks exactly like
   a byte that was never written. */
#define LABEL_BUFFER_RVA RVA(0x00a68c18u)

/*
 * And the link before that: the game's OWN copy of the device state.
 *
 * FUN_006285c0 polls up to ten joystick devices (pointers at wrapper+0x0c)
 * into DIJOYSTATE2 blocks at wrapper+0x4f0 + pad*0x110, and records which of
 * them answered in the bitmask at wrapper+0x129cc. FUN_00627650 then reads a
 * button as `[wrapper + pad*0x110 + code + 0x50b]`, i.e. rgbButtons[code-0x15]
 * of that block. So this is the exact byte a pad binding resolves against, and
 * reading it here separates "DirectInput never delivered it" from "it was
 * delivered and the binding did not use it".
 */
#define DI_WRAPPER_RVA  RVA(0x00a6abfcu)  /* [0x00a6abfc], the DirectInput wrapper */
#define DI_JOY_BLOCK    0x4f0u
#define DI_JOY_STRIDE   0x110u
#define DI_JOY_BUTTONS  0x30u        /* DIJOYSTATE2 rgbButtons                */
#define DI_JOY_NBUTTON  32u
#define DI_JOY_MAX      10u
#define DI_JOY_LIVE     0x129ccu     /* bit per device that answered          */
#define DI_KEYBOARD     0x25e4u      /* 256-byte DIK state, read by 006276d0  */
#define DI_BOUND_COUNT  0x129c8u     /* controllers registered for evaluation */
#define DI_BOUND_LIST   0x129d0u
#define DI_PAD_VALUE_RVA RVA(0x00627650u) /* FUN_00627650(pad, code) -> float      */

static uint32_t exe_base(void)
{
    X86Module *m;
    for (m = x86_modules(); m; m = m->next)
        if (m->preferred == EXE_PREFERRED && m->base && *m->base)
            return *m->base;
    return 0;
}

static uint32_t thiscall(CPU *cpu, uint32_t fn, uint32_t ecx,
                         int argc, const uint32_t *argv)
{
    CPU call = *cpu;
    int i;
    call.esp -= (uint32_t)argc * 4u;
    for (i = 0; i < argc; i++) WR32(call.esp + (uint32_t)i * 4u, argv[i]);
    call.ecx = ecx;
    x86_guest_call_args(&call, fn, (uint32_t)argc * 4u);
    return call.eax;
}

/*
 * The physical-code vocabulary FUN_00627650 answers for, named.
 *
 * Codes 1..0x10 are SIGNED AXIS HALVES -- the positive half then the negative
 * half of each DirectInput axis, so one stick costs four codes -- 0x11..0x14
 * are the POV hat's four directions, and 0x15 upward are the 32 buttons of the
 * DIJOYSTATE block in the order DirectInput enumerates them. The Xbox meaning
 * beside each one is this pad's layout (dinput_pad.c), not DirectInput's, and
 * the two triggers SHARE the Z axis: left drives it positive, right negative,
 * which is what a 2005 game written against a 360 pad reads.
 */
#define PAD_CODE_MAX 0x34u           /* 0x15 + 32 buttons - 1 */

static const char *pad_code_name(uint32_t code)
{
    static const char *const AXES[] = {
        "X+",   "X- ",                       /* 0x01 0x02  left stick right/left */
        "Y+",   "Y- ",                       /* 0x03 0x04  left stick down/up    */
        "Z+ LT", "Z- RT",                    /* 0x05 0x06  the shared trigger axis */
        "Rx+",  "Rx-",                       /* 0x07 0x08  right stick right/left */
        "Ry+",  "Ry-",                       /* 0x09 0x0a  right stick down/up   */
        "Rz+",  "Rz-", "S0+", "S0-", "S1+", "S1-",
    };
    static const char *const POV[] = { "POV X+", "POV X-", "POV Y+", "POV Y-" };
    static const char *const BUTTONS[] = {
        "A", "B", "X", "Y", "LB", "RB", "Back", "Start", "LS click", "RS click"
    };
    static char other[16];

    if (code >= 1u && code <= 0x10u) return AXES[code - 1u];
    if (code >= 0x11u && code <= 0x14u) return POV[code - 0x11u];
    if (code >= 0x15u && code < 0x15u + sizeof BUTTONS / sizeof BUTTONS[0])
        return BUTTONS[code - 0x15u];
    snprintf(other, sizeof other, "button %u", code - 0x15u);
    return other;
}

/* The device kind a slot's value names, in the game's own numbering. */
static const char *kind_name(uint32_t kind)
{
    if (kind == 0u) return "--";
    if (kind == 1u) return "kb";
    if (kind == 2u) return "mouse";
    if (kind >= 3u && kind <= 0xcu) return "pad";
    return "?";
}

/* Append to a bounded buffer, tracking the write position. */
static void put(char *out, size_t n, size_t *at, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

static void put(char *out, size_t n, size_t *at, const char *fmt, ...)
{
    va_list ap;
    int k;
    if (*at >= n) return;
    va_start(ap, fmt);
    k = vsnprintf(out + *at, n - *at, fmt, ap);
    va_end(ap);
    if (k > 0) *at += (size_t)k > n - *at ? n - *at : (size_t)k;
}

size_t input_probe_report(CPU *cpu, unsigned controller,
                          char *out, size_t n)
{
    char why[192];
    size_t at = 0;
    uint32_t object, manager = 0, base = exe_base();
    uint32_t row, slot, action;
    unsigned populated = 0, pad_rows = 0, kb_rows = 0;
    unsigned resolved = 0, unmapped = 0, down = 0;
    int rows_down[INPUT_BINDING_ROWS];

    if (!out || n < 64u) return 0;
    memset(rows_down, 0, sizeof rows_down);

    put(out, n, &at, "input probe -- frame %lu, guest %.2fs\n",
        gpu_frames_presented(), guest_clock_elapsed_s());
    at += x2_input_probe_lifecycle_report(out + at, n - at);

    object = input_bindings_object_at(controller, why, (int)sizeof why);
    if (!object) {
        put(out, n, &at,
            "REFUSED: %s.\n"
            "0 of %u binding rows and 0 of %u actions could be read for "
            "controller %u. This is the probe saying it cannot see, not the "
            "game saying nothing is bound.\n",
            why, INPUT_BINDING_ROWS, INPUT_ACTION_MAX, controller);
        return at;
    }
    if (!cpu) {
        put(out, n, &at,
            "REFUSED: no guest CPU at this call site, so neither the action "
            "map (FUN_00619c40) nor the action state (input vtable +0x%x) can "
            "be asked. The binding table alone is at 0x%08x.\n",
            VT_ACTION_DOWN, object);
        return at;
    }

    manager = base ? thiscall(cpu, base + INPUT_MGR_RVA, 0u, 0, NULL) : 0u;
    at += cutscene_skip_probe_report(cpu, controller, manager, out + at, n - at);

    put(out, n, &at, "controller %u binding table 0x%08x -- %u rows x %u "
                     "slots%s\n\n",
        controller, object, INPUT_BINDING_ROWS, INPUT_BINDING_SLOTS,
        controller < 4u ? "  (a MASTER set: edited and persisted, copied into "
                          "4..7 and 12..15)" : "");
    put(out, n, &at,
        "action  row  name              slot0     slot1     slot2/pad  "
        "slot3     state\n");

    for (row = 0; row < INPUT_BINDING_ROWS; row++) {
        uint32_t kind, code;
        int has_pad = 0, has_kb = 0;
        for (slot = 0; slot < INPUT_BINDING_SLOTS; slot++) {
            if (!input_bindings_read(object, row, slot, &kind, &code)) continue;
            if (kind || code) populated++;
            if (kind >= 3u && kind <= 0xcu) has_pad = 1;
            if (kind == 1u) has_kb = 1;
        }
        pad_rows += (unsigned)has_pad;
        kb_rows += (unsigned)has_kb;
    }

    for (action = 0; action < INPUT_ACTION_MAX; action++) {
        int r = input_binding_row_of_action(cpu, action);
        char cells[4][20];
        const char *name;
        uint32_t state = 0;

        if (r < 0 || (uint32_t)r >= INPUT_BINDING_ROWS) {
            unmapped++;
            continue;
        }
        resolved++;
        row = (uint32_t)r;
        name = input_binding_row_storage_key(row);
        for (slot = 0; slot < INPUT_BINDING_SLOTS; slot++) {
            uint32_t kind = 0, code = 0;
            if (!input_bindings_read(object, row, slot, &kind, &code))
                snprintf(cells[slot], sizeof cells[slot], "unread");
            else if (!kind && !code)
                snprintf(cells[slot], sizeof cells[slot], "--");
            else
                snprintf(cells[slot], sizeof cells[slot], "%s%u:0x%02x",
                         kind_name(kind), kind, code);
        }
        if (manager) {
            uint32_t vt = 0, fn = 0;
            if (x86_peek32(manager, &vt) && x86_peek32(vt + VT_ACTION_DOWN, &fn)
                && fn)
                state = (uint8_t)thiscall(cpu, fn, manager, 1, &action);
        }
        if (state) { down++; rows_down[row] = 1; }
        put(out, n, &at, " 0x%02x   %2u  %-16s  %-9s %-9s %-10s %-9s %s\n",
            action, row, name ? name : "(unnamed)",
            cells[0], cells[1], cells[2], cells[3], state ? "DOWN" : ".");
    }

    put(out, n, &at,
        "\n%u of %u slots populated: %u row(s) carry a pad binding, %u carry a "
        "keyboard one.\n",
        populated, INPUT_BINDING_ROWS * INPUT_BINDING_SLOTS, pad_rows, kb_rows);
    put(out, n, &at,
        "%u of %u actions resolve to a row; %u resolve to none.\n",
        resolved, INPUT_ACTION_MAX, unmapped);
    if (!manager)
        put(out, n, &at,
            "the input singleton (FUN_005d8920) returned 0, so NO action was "
            "asked for its state -- the \"state\" column above is not a "
            "reading, it is an absence.\n");
    else
        put(out, n, &at,
            "%u of %u resolved actions read DOWN through input vtable +0x%x "
            "at this instant.\n", down, resolved, VT_ACTION_DOWN);

    /*
     * The game's own device state, one link before the binding table reads it.
     */
    {
        uint32_t wrapper = 0, live = 0, p;

        put(out, n, &at, "\n");
        if (!base || !x86_peek32(base + DI_WRAPPER_RVA, &wrapper) || !wrapper) {
            put(out, n, &at, "the game's DirectInput wrapper ([0x%08x]) is not "
                             "constructed, so NO device state was read.\n",
                base + DI_WRAPPER_RVA);
        } else {
            unsigned keys = 0, k;
            x86_peek32(wrapper + DI_JOY_LIVE, &live);
            put(out, n, &at, "game DirectInput wrapper 0x%08x: joystick "
                             "answer mask 0x%08x\n", wrapper, live);
            for (p = 0; p < DI_JOY_MAX; p++) {
                uint32_t blk = wrapper + DI_JOY_BLOCK + p * DI_JOY_STRIDE;
                unsigned b, held = 0;
                if (!(live & (1u << p))) continue;
                put(out, n, &at, "  device %u block 0x%08x buttons down:", p,
                    blk);
                for (b = 0; b < DI_JOY_NBUTTON; b++) {
                    unsigned char v = 0;
                    if (!x86_peek(blk + DI_JOY_BUTTONS + b, &v, 1)) continue;
                    if (v & 0x80u) {
                        put(out, n, &at, " %u(code 0x%02x)", b, b + 0x15u);
                        held++;
                    }
                }
                put(out, n, &at, "%s\n", held ? "" : " none of 32");
            }
            if (!live)
                put(out, n, &at, "  NO joystick device answered this "
                                 "frame, so every pad binding reads 0 by "
                                 "construction.\n");
            for (k = 0; k < 256u; k++) {
                unsigned char v = 0;
                if (!x86_peek(wrapper + DI_KEYBOARD + k, &v, 1)) continue;
                if (v & 0x80u) {
                    if (!keys++) put(out, n, &at, "  keyboard DIK down:");
                    put(out, n, &at, " 0x%02x", k);
                }
            }
            put(out, n, &at, "%s\n", keys ? "" :
                "  keyboard: none of 256 DIK bytes down");

            /*
             * Which controllers the wrapper will EVALUATE. FUN_006285c0's tail
             * walks exactly this list and calls FUN_006276d0 on each one's
             * binding object; a controller that is not in it is never asked
             * what its bindings resolve to, however well-populated they are.
             */
            {
                uint32_t cnt = 0, e;
                x86_peek32(wrapper + DI_BOUND_COUNT, &cnt);
                put(out, n, &at, "  registered for binding evaluation: %u\n",
                    cnt);
                for (e = 0; e < cnt && e < 16u; e++) {
                    uint32_t ctl = 0;
                    if (!x86_peek32(wrapper + DI_BOUND_LIST + e * 4u, &ctl))
                        continue;
                    put(out, n, &at, "    [%u] controller 0x%08x -> binding "
                                     "object 0x%08x\n", e, ctl,
                        ctl ? ctl + 0x18u : 0u);
                }
                if (!cnt)
                    put(out, n, &at, "    NONE -- no binding object is "
                                     "evaluated at all this frame.\n");
            }

            /*
             * And the accessor itself, asked directly, over its WHOLE
             * vocabulary. If this reads 1.0 for a button the block above shows
             * down, then everything up to and including the physical read
             * works and the fault is that nothing ASKED -- which is a
             * different repair from a broken read.
             *
             * Every code is printed with its MAGNITUDE, not a down/up bit, and
             * that is the point of sampling the axes here rather than the four
             * face buttons this used to cover. An analog code that arrives at
             * HALF scale is indistinguishable from one that arrives correctly
             * if all you print is "pressed", and the triggers are the codes a
             * pad binding is most likely to be wrong about: DirectInput puts
             * both of them on ONE axis, so their scale is this port's
             * arithmetic rather than SDL's number.
             */
            {
                unsigned c, hot = 0;
                put(out, n, &at,
                    "  FUN_00627650(pad 0, code) over all %u codes -- the "
                    "value a binding on that code resolves to:\n",
                    PAD_CODE_MAX);
                for (c = 1u; c <= PAD_CODE_MAX; c++) {
                    long double v;
                    CPU call = *cpu;
                    call.esp -= 8u;
                    WR32(call.esp + 0u, 0u);
                    WR32(call.esp + 4u, c);
                    call.ecx = wrapper;
                    x86_guest_call_args(&call, base + DI_PAD_VALUE_RVA, 8u);
                    v = call.st[call.top];
                    if (v == 0.0L) continue;
                    put(out, n, &at, "    0x%02x %-14s %+.3f\n",
                        c, pad_code_name(c), (double)v);
                    hot++;
                }
                put(out, n, &at, "    %u of %u codes are non-zero%s\n",
                    hot, PAD_CODE_MAX,
                    hot ? "" : " -- nothing on this pad is deflected or held");
            }
        }
    }

    at += x2_player_participation_probe_report(cpu, out + at, n - at);

    /*
     * The prompt label the game last composed, byte by byte.
     */
    if (base) {
        unsigned char buf[32];
        unsigned i, len = 0;
        put(out, n, &at, "\nprompt label at 0x%08x: \"",
            base + LABEL_BUFFER_RVA);
        for (i = 0; i < sizeof buf; i++) {
            if (!x86_peek(base + LABEL_BUFFER_RVA + i, &buf[i], 1)) break;
            if (!buf[i]) break;
            len++;
            put(out, n, &at, "%c", buf[i] >= 0x20 && buf[i] < 0x7f
                                   ? (char)buf[i] : '.');
        }
        put(out, n, &at, "\"  bytes:");
        for (i = 0; i < len; i++) put(out, n, &at, " %02x", buf[i]);
        if (!len) put(out, n, &at, " (empty -- nothing has composed a label)");
        put(out, n, &at, "\n");
    }

    /*
     * The player's own character, and whether the game can resolve it.
     */
    {
        uint32_t mgr = base ? thiscall(cpu, base + PAD_MGR_RVA, 0u, 0, NULL)
                            : 0u;
        int32_t idx = -1;
        uint32_t i, resolved = 0, live = 0;

        put(out, n, &at, "\n");
        if (!mgr) {
            put(out, n, &at, "current player: the pad manager is not "
                             "constructed, so there is no index to read.\n");
        } else {
            uint32_t v = 0;
            x86_peek32(mgr + PADMGR_CURPLAYER, &v);
            idx = (int32_t)v;
            put(out, n, &at, "current player index %d%s\n", idx,
                (idx < 0 || idx >= 4)
                    ? "  (out of 0..3, so the fallback handle is used)" : "");
            for (i = 0; i < 5u; i++) {
                uint32_t slot = i < 4u
                    ? base + HERO_HANDLES_RVA + i * 4u
                    : base + HERO_FALLBACK_RVA;
                uint32_t handle = 0, actor;
                CPU call = *cpu;
                if (!x86_peek32(slot, &handle)) continue;
                live++;
                call.esp -= 4u;
                WR32(call.esp, handle);
                call.ecx = call.esp;
                x86_guest_call_args(&call, base + RESOLVE_HANDLE_RVA, 0u);
                actor = call.eax;
                if (actor) resolved++;
                put(out, n, &at, "  %-8s handle 0x%08x -> actor 0x%08x%s\n",
                    i < 4u ? "player" : "fallback", handle, actor,
                    actor ? "" : "   UNRESOLVED");
            }
            put(out, n, &at, "%u of %u hero handle(s) resolve to an actor.\n",
                resolved, live);
        }
    }

    /*
     * Every controller, not just player 0's. The dialog prompt that reads
     * [ENTER] is bound on one of the menu controllers, so a report that showed
     * only player 0 would say "Return is bound nowhere" about a game that is
     * plainly responding to Return.
     */
    {
        uint32_t idx, live = 0;
        put(out, n, &at, "\ncontroller  object      slots  pad rows  "
                         "keyboard rows\n");
        for (idx = 0; idx < INPUT_CONTROLLERS; idx++) {
            uint32_t obj = input_bindings_object_at(idx, why, (int)sizeof why);
            unsigned pop = 0, pads = 0, kbs = 0;
            if (!obj) continue;
            live++;
            for (row = 0; row < INPUT_BINDING_ROWS; row++) {
                int has_pad = 0, has_kb = 0;
                for (slot = 0; slot < INPUT_BINDING_SLOTS; slot++) {
                    uint32_t kind = 0, code = 0;
                    if (!input_bindings_read(obj, row, slot, &kind, &code))
                        continue;
                    if (kind || code) pop++;
                    if (kind >= 3u && kind <= 0xcu) has_pad = 1;
                    if (kind == 1u) has_kb = 1;
                }
                pads += (unsigned)has_pad;
                kbs += (unsigned)has_kb;
            }
            put(out, n, &at, "   %2u       0x%08x  %3u    %3u       %3u%s\n",
                idx, obj, pop, pads, kbs,
                idx == controller ? "   <- the table above" : "");
        }
        put(out, n, &at, "%u of %u controller objects are constructed.\n",
            live, INPUT_CONTROLLERS);
    }

    /* Print host beside guest so the binding boundary is attributable. */
    {
        int pad, pads = 0;
        for (pad = 0; pad < DINPUT_PAD_MAX; pad++) {
            const char *nm = dinput_pad_name(pad);
            int b, nb, held = 0;
            if (!nm) continue;
            pads++;
            nb = dinput_pad_button_count(pad);
            put(out, n, &at, "host pad %d \"%s\": %d button(s), POV 0x%08x, "
                             "down:", pad, nm, nb, dinput_pad_pov(pad));
            for (b = 0; b < nb; b++)
                if (dinput_pad_button(pad, b)) {
                    put(out, n, &at, " %d", b);
                    held++;
                }
            put(out, n, &at, "%s\n", held ? "" : " none");
            put(out, n, &at,
                "host pad %d axes (game range -1000..1000): X %d Y %d Z %d "
                "RX %d RY %d RZ %d\n", pad,
                dinput_pad_axis(pad, DINPUT_PAD_AXIS_X, -1000, 1000),
                dinput_pad_axis(pad, DINPUT_PAD_AXIS_Y, -1000, 1000),
                dinput_pad_axis(pad, DINPUT_PAD_AXIS_Z, -1000, 1000),
                dinput_pad_axis(pad, DINPUT_PAD_AXIS_RX, -1000, 1000),
                dinput_pad_axis(pad, DINPUT_PAD_AXIS_RY, -1000, 1000),
                dinput_pad_axis(pad, DINPUT_PAD_AXIS_RZ, -1000, 1000));
        }
        if (!pads)
            put(out, n, &at, "host: no pad is connected, so every pad binding "
                             "above is unreachable by construction.\n");
    }
    return at;
}
