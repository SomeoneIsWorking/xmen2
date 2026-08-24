/* Xbox prompt delivery at the game's own physical-input naming boundary.
 *
 * XMen2.exe FUN_00619e30 selects an action binding and asks FUN_006281f0 to
 * name its (device kind, physical code), then formats that name as "[%s]".
 * The original name function's measured pad vocabulary is:
 *
 *   kinds 3..0xc       gamepad slots 0..9
 *   codes 0x15..0x31  "Btn N" (the active DirectInput order is in
 *                       dinput_pad.c)
 *   codes 1..0x10     signed axes; Z+ (5) / Z- (6) are LT / RT
 *   codes 0x11..0x14  POV directions -- X+ X- Y+ Y-, i.e. right, left,
 *                       down, up, and each gets its OWN glyph
 *
 * This port replaces only those names for a live Xbox-family SDL device and
 * only when the generated font pack is active. Every other case super-calls
 * the original recompiled body, keeping keyboard, PlayStation, generic-pad,
 * stick-click and unknown-code names faithful.
 *
 * WHICH BINDING THE LABEL DESCRIBES is the other half, and without it the
 * naming override above can be perfectly correct and never fire on screen.
 * FUN_00619e30 asks FUN_006294b0 for slots 2, 0, 1, 1 in that order and takes
 * the first with a non-zero device kind. Slot 2 of the set it reads is where
 * FUN_0061b030 puts its own hardcoded menu keys -- row 4 slot 2 is DIK Return
 * -- so the keyboard always wins and a dialog reads "[ENTER]" with a pad in
 * hand. So a second override sits on FUN_006294b0: while an Xbox-family pad is
 * the assigned pad is the player's active source, a row that HAS its binding
 * is named by it whatever slot it sits in; keyboard-active hotswap super-calls.
 *
 * That is a deliberate change of behaviour rather than a faithful one, and it
 * is the behaviour the feature is for: a prompt names the last device used by
 * that player. It is confined to the label -- FUN_006294b0 has four call sites and
 * all four are inside FUN_00619e30 -- so no input path is affected, and with
 * no active assigned pad the original order is untouched.
 */
#include "pad_glyphs.h"

#include "dinput_pad.h"
#include "dinput8_controller_slots.h"
#include "pad_glyph_codes.h"
#include "prompt_glyph_pack.h"
#include "prompt_labels.h"
#include "player_input.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>

#define EXE_PREFERRED       0x00400000u
#define NAME_BUFFER_RVA     0x0066aec8u /* original static at 0x00a6aec8 */

#include "input_bindings.h"

static unsigned long g_mapped, g_deferred;
static unsigned long g_rows_asked, g_rows_padded, g_rows_no_pad;

/*
 * X2_PAD_GLYPH_PROBE=<char> -- draw this ASCII character instead of the glyph
 * byte, everywhere the glyph would have gone.
 *
 * The delivery path and the RENDERING of the byte it delivers are two
 * different questions, and the on-screen symptom of a failure is the same for
 * both: "[]", an empty pair of brackets. With this set to a character the
 * stock font certainly has, a prompt that reads "[#] Back" proves the label is
 * built, routed and drawn, and narrows the fault to the codepoint; a prompt
 * that still reads "[]" proves it never gets that far. Off unless asked for.
 */
static int probe_char(void)
{
    static int c = -1;
    if (c < 0) {
        const char *e = getenv("X2_PAD_GLYPH_PROBE");
        /* "0xNN" forces a raw byte -- use it to put an ASYMMETRIC glyph on
           every prompt (LB and RB carry an L and an R), which is the only way
           to tell a mirrored atlas cell from an upright one. A bare character
           forces that character. */
        if (e && e[0] == '0' && (e[1] == 'x' || e[1] == 'X'))
            c = (int)strtol(e + 2, NULL, 16) & 0xff;
        else
            c = (e && *e) ? (unsigned char)*e : 0;
        if (c)
            fprintf(stderr, "PAD-GLYPHS: X2_PAD_GLYPH_PROBE -- every pad "
                            "prompt will draw byte 0x%02x ('%c') instead of "
                            "its glyph. This is a diagnostic; unset it to see "
                            "the real art.\n",
                    c, c >= 0x20 && c < 0x7f ? c : '.');
    }
    return c;
}

uint8_t pad_glyph_code(uint32_t code)
{
    static const uint8_t buttons[] = {
        X2_PAD_GLYPH_FACE_A, X2_PAD_GLYPH_FACE_B, X2_PAD_GLYPH_FACE_X, X2_PAD_GLYPH_FACE_Y,
        X2_PAD_GLYPH_LB, X2_PAD_GLYPH_RB,
        X2_PAD_GLYPH_BACK, X2_PAD_GLYPH_START
    };
    /* The POV hat, ONE GLYPH PER DIRECTION. A single d-pad picture for all
       four is what the game asks about four different bindings, and a prompt
       offering four d-pad choices then draws the same icon four times -- less
       than the keyboard text it replaced, which at least said UP or LEFT
       (#88). The code order is DirectInput's POV pairs: X+ X- Y+ Y-, and Y+
       is DOWN here exactly as it is on the sticks. */
    static const uint8_t pov[] = {
        X2_PAD_GLYPH_DPAD_RIGHT, X2_PAD_GLYPH_DPAD_LEFT,
        X2_PAD_GLYPH_DPAD_DOWN,  X2_PAD_GLYPH_DPAD_UP
    };
    if (code >= 0x15u && code < 0x15u + sizeof buttons)
        return buttons[code - 0x15u];
    if (code == 5u) return X2_PAD_GLYPH_LT;
    if (code == 6u) return X2_PAD_GLYPH_RT;
    if (code >= 0x11u && code < 0x11u + sizeof pov)
        return pov[code - 0x11u];
    if (code == 0x1du) return X2_PAD_GLYPH_LS;
    if (code == 0x1eu) return X2_PAD_GLYPH_RS;
    return 0;
}

/* Is this byte one of the codepoints this port publishes? The bounds are
   generated with the glyphs, because a hand-written range ending at whichever
   glyph is currently last stops covering the set the moment one is added. */
static uint32_t name_buffer(void)
{
    X86Module *m;
    for (m = x86_modules(); m; m = m->next)
        if (m->preferred == EXE_PREFERRED && m->base && *m->base)
            return *m->base + NAME_BUFFER_RVA;
    return 0;
}

void fn_XMen2_006281f0(CPU *C);

static int host_pad_for_kind(uint32_t kind)
{
    if (kind < 3u || kind > 0xcu) return -1;
    return dinput8_controller_host_pad_for_slot((int)kind - 3);
}

void x2_override_006281f0(CPU *C)
{
    uint32_t kind = RD32(C->esp + 4u);
    uint32_t code = RD32(C->esp + 8u);
    uint8_t glyph;
    uint32_t out;
    int host_pad;

    glyph = pad_glyph_code(code);
    host_pad = host_pad_for_kind(kind);
    if (!prompt_glyph_pack_enabled() || host_pad < 0 || !glyph ||
        !dinput_pad_uses_xbox_glyphs(host_pad) || !(out = name_buffer())) {
        g_deferred++;
        fn_XMen2_006281f0(C);
        return;
    }

    /* The original returns one of its own static buffers. Reusing that guest
       buffer preserves the pointer lifetime and keeps the pointer 32-bit; a
       host string literal would truncate on this 64-bit process. */
    if (probe_char()) glyph = (uint8_t)probe_char();
    WR8(out, glyph);
    WR8(out + 1u, 0);
    C->eax = out;
    C->esp += 12u; /* RET 8: return address + the two stack arguments */
    g_mapped++;
}

/*
 * FUN_006294b0(row, slot, *kind, *code) -- the label's binding reader.
 *
 * __thiscall, RET 0x10, and it writes nothing when the caller passes a null
 * out-pointer, which the original checks for and so does this.
 */
void fn_XMen2_006294b0(CPU *C);

/* The row's pad binding, in whichever slot holds it. 0 if it has none. */
static int row_pad_binding(uint32_t object, uint32_t row, uint32_t *kind,
                           uint32_t *code)
{
    uint32_t slot, k, c;
    int host_pad;
    for (slot = 0; slot < INPUT_BINDING_SLOTS; slot++) {
        if (!input_bindings_read(object, row, slot, &k, &c)) continue;
        host_pad = host_pad_for_kind(k);
        if (host_pad < 0) continue;
        if (!x2_player_input_pad_is_active_source(host_pad)) continue;
        *kind = k;
        *code = c;
        return 1;
    }
    return 0;
}

void x2_override_006294b0(CPU *C)
{
    uint32_t object = C->ecx;
    uint32_t row = RD32(C->esp + 4u);
    uint32_t out_kind = RD32(C->esp + 0xcu);
    uint32_t out_code = RD32(C->esp + 0x10u);
    uint32_t kind, code;

    g_rows_asked++;
    if (!prompt_glyph_pack_enabled() || row >= INPUT_BINDING_ROWS ||
        !row_pad_binding(object, row, &kind, &code)) {
        g_rows_no_pad++;
        fn_XMen2_006294b0(C);
        return;
    }
    if (out_kind) WR32(out_kind, kind);
    if (out_code) WR32(out_code, code);
    C->esp += 4u + 0x10u;          /* RET 0x10 */
    g_rows_padded++;
}

/* Register the Xbox-prompt name and label-selection boundaries. */
__attribute__((constructor))
static void x2_pad_glyphs_register_overrides(void)
{
    x86_register_override("XMen2.exe", 0x006281f0, x2_override_006281f0);
    x86_register_override("XMen2.exe", 0x006294b0, x2_override_006294b0);
}

void pad_glyphs_report(void)
{
    static int done;
    if (done++) return;
    printf("  Xbox prompt names: %lu glyph(s), %lu original name(s); font "
           "pack %s\n", g_mapped, g_deferred,
           prompt_glyph_pack_enabled() ? "enabled" : "disabled");
    /* With the denominator, because "0 glyphs" means one thing when the label
       was never built at all and another when it was built 900 times and every
       row named the keyboard. */
    printf("  Xbox prompt rows: %lu label read(s) -- %lu answered with the "
           "row's pad binding, %lu had none and used the game's own slot "
           "order\n", g_rows_asked, g_rows_padded, g_rows_no_pad);
    prompt_labels_report();
}
