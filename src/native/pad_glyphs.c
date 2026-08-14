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
 *   codes 0x11..0x14  POV directions
 *
 * This port replaces only those names for a live Xbox-family SDL device and
 * only when the generated font pack is active. Every other case super-calls
 * the original recompiled body, keeping keyboard, PlayStation, generic-pad,
 * stick-click and unknown-code names faithful.
 */
#include "pad_glyphs.h"

#include "dinput_pad.h"
#include "pad_glyph_codes.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>

#define EXE_PREFERRED       0x00400000u
#define NAME_BUFFER_RVA     0x0066aec8u /* original static at 0x00a6aec8 */

static unsigned long g_mapped, g_deferred;
static int g_enabled = -1;

static int enabled(void)
{
    if (g_enabled < 0) {
        const char *e = getenv("X2_PAD_GLYPHS");
        g_enabled = e && *e && *e != '0';
    }
    return g_enabled;
}

uint8_t pad_glyph_code(uint32_t code)
{
    static const uint8_t buttons[] = {
        X2_PAD_GLYPH_A, X2_PAD_GLYPH_B, X2_PAD_GLYPH_X, X2_PAD_GLYPH_Y,
        X2_PAD_GLYPH_LB, X2_PAD_GLYPH_RB,
        X2_PAD_GLYPH_BACK, X2_PAD_GLYPH_START
    };
    if (code >= 0x15u && code < 0x15u + sizeof buttons)
        return buttons[code - 0x15u];
    if (code == 5u) return X2_PAD_GLYPH_LT;
    if (code == 6u) return X2_PAD_GLYPH_RT;
    if (code >= 0x11u && code <= 0x14u) return X2_PAD_GLYPH_DPAD;
    return 0;
}

static uint32_t name_buffer(void)
{
    X86Module *m;
    for (m = x86_modules(); m; m = m->next)
        if (m->preferred == EXE_PREFERRED && m->base && *m->base)
            return *m->base + NAME_BUFFER_RVA;
    return 0;
}

void __real_fn_XMen2_006281f0(CPU *C);

void __wrap_fn_XMen2_006281f0(CPU *C)
{
    uint32_t kind = RD32(C->esp + 4u);
    uint32_t code = RD32(C->esp + 8u);
    uint8_t glyph;
    uint32_t out;

    glyph = pad_glyph_code(code);
    if (!enabled() || kind < 3u || kind > 0xcu || !glyph ||
        !dinput_pad_uses_xbox_glyphs((int)kind - 3) || !(out = name_buffer())) {
        g_deferred++;
        __real_fn_XMen2_006281f0(C);
        return;
    }

    /* The original returns one of its own static buffers. Reusing that guest
       buffer preserves the pointer lifetime and keeps the pointer 32-bit; a
       host string literal would truncate on this 64-bit process. */
    WR8(out, glyph);
    WR8(out + 1u, 0);
    C->eax = out;
    C->esp += 12u; /* RET 8: return address + the two stack arguments */
    g_mapped++;
}

void pad_glyphs_report(void)
{
    static int done;
    if (done++) return;
    printf("  Xbox prompt names: %lu glyph(s), %lu original name(s); font "
           "pack %s\n", g_mapped, g_deferred,
           enabled() ? "enabled" : "disabled");
}
