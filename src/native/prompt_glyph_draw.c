/* Prompt glyphs at the text renderer -- the port draws its own art.
 *
 * Direction (2026-08-26): the game's fonts stay untouched. No derived font
 * pack, no in-memory atlas compositing, nothing written into a font record.
 * Instead the port overrides the exe's own glyph pipeline, established in
 * docs/RE/text.md and claim C266:
 *
 *   FUN_005ef2e0  per-element orchestrator: markup -> wide line buffers
 *   FUN_005ee620  binds the font atlas, caches scales into the draw object
 *   FUN_005ee780  THE GLYPH LOOP: wchar < 256 -> record lookup -> quad via
 *                 FUN_005ee400; __fastcall(ECX=owner, EDX=&wide buf), seven
 *                 stack args, callee pops 0x1c
 *   FUN_00597c90  measurement, reached through font-table vtable +0x38
 *
 * This file is being built in stages, and stage one is DETECTION: sit on
 * FUN_005ee780, classify every string that reaches the glyph loop, and prove
 * on a real run that prompt labels carrying the port's private codepoints
 * (X2_PROMPT_GLYPH_FIRST..X2_PROMPT_GLYPH_LAST, delivered by pad_glyphs.c and
 * prompt_labels.c) actually arrive HERE. Everything downstream -- segmentation,
 * the port atlas upload, custom quads through FUN_005ee400, and the matching
 * measurer override -- depends on that arrival, so it is established and
 * counted before any pixel is drawn. A super-call happens on EVERY string;
 * without the feature gate nothing else about a string is touched.
 *
 * STAGE ONE RAN AND THE LABELS DO ARRIVE -- claim C268, docs/RE/text.md.
 * On a boot-direct tutorial run, 1,142 of 4,581 strings at this loop carried
 * 13,704 prompt codepoints, in exactly the composed shape
 * (0090 0091x5 0092x5 "Enter" 0093). So the cheap test below is sound and
 * stage two -- segmentation, the port atlas, custom quads through
 * FUN_005ee400, and the matching FUN_00597c90 measurer override -- is
 * unblocked.
 *
 * It first reported the opposite, and that was this file's fault: it read
 * the wide string from C->edx, which FUN_005ee780 overwrites from [EDI+0x8]
 * at 0x005ee797 before ever reading. See glyph_loop_string() below and
 * instrument I069 for how a wrong pointer still decoded as real text and
 * read like a working detector for a whole investigation.
 */
#include "prompt_glyph_draw.h"

#include "pad_glyph_codes.h"
#include "prompt_glyph_pack.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>

void fn_XMen2_005ee780(CPU *C);

#define MAX_WALK 512u          /* widest line buffer the game builds */

static int prompt_codepoint(uint16_t c)
{
    /* The WHOLE published run, keycap parts included -- the pad-only bound
     * would classify a cap-composed keyboard label as ordinary text. */
    return c >= X2_PROMPT_GLYPH_FIRST && c <= X2_PROMPT_GLYPH_LAST;
}

static unsigned long g_strings, g_with_prompts, g_prompt_codepoints;
static unsigned long g_super_called;
static unsigned g_examples;
/* The NEAR MISS denominator. A prompt codepoint is 0x80..0x93 -- inside the
   byte range, not up in a private plane -- so "no prompt codepoint arrived"
   and "no non-ASCII arrived at all" are different answers and the report has
   to be able to tell them apart. Without this, a label whose codepoints were
   remapped, narrowed or shifted on the way here reads exactly like a label
   that was never drawn. */
static unsigned long g_with_non_ascii;

/* Sample by DISTINCT CONTENT, not by arrival order. A flat "first N" cap
   spends its whole budget on the legal screen and the engine's repeated
   control words -- the run that mattered dumped `9d28 01f2` eight times and
   never showed the one string shape worth seeing. Keyed on a hash of the
   wchars, so a string repeated 2,000 times costs one slot. */
#define DISTINCT_SLOTS 96u
static uint32_t g_seen_hash[DISTINCT_SLOTS];
static unsigned g_distinct;
static unsigned long g_distinct_dropped;

static uint32_t wide_hash(uint32_t s_guest, unsigned *length_out)
{
    uint32_t h = 2166136261u;
    unsigned i;
    for (i = 0; i < MAX_WALK; i++) {
        uint16_t c = RD16(s_guest + (uint32_t)i * 2u);
        if (!c) break;
        h = (h ^ c) * 16777619u;
    }
    if (length_out) *length_out = i;
    return h ? h : 1u;
}

/* First sighting of this exact content? Records it if there is room. */
static int first_sighting(uint32_t hash)
{
    unsigned i;
    for (i = 0; i < g_distinct; i++)
        if (g_seen_hash[i] == hash) return 0;
    if (g_distinct == DISTINCT_SLOTS) { g_distinct_dropped++; return 0; }
    g_seen_hash[g_distinct++] = hash;
    return 1;
}

int x2_string_has_prompt_glyph(uint32_t s_guest, unsigned max)
{
    unsigned i;
    if (!s_guest) return 0;
    for (i = 0; i < max; i++) {
        uint16_t c = RD16(s_guest + (uint32_t)i * 2u);
        if (!c) return 0;
        if (prompt_codepoint(c)) return 1;
    }
    return 0;
}

static void log_example(uint32_t where)
{
    char buf[MAX_WALK + 1];
    unsigned i;
    fprintf(stderr, "PROMPT DRAW: string at guest 0x%08x wchars:", where);
    for (i = 0; i < MAX_WALK; i++) {
        uint16_t c = RD16(where + (uint32_t)i * 2u);
        if (!c) break;
        buf[i] = (c >= 0x20 && c < 0x7f) ? (char)c
               : prompt_codepoint(c) ? '#' : '?';
        fprintf(stderr, " %04x", c);
    }
    buf[i] = 0;
    fprintf(stderr, "  = \"%s\"\n", buf);
}

/* The wide string is FUN_005ee780's FIRST STACK ARGUMENT, not EDX.
 *
 * Read out of the retail body: the prologue is `SUB ESP,0x2c` then four
 * pushes (EBX, EBP, ESI, EDI), putting ESP 0x3c below entry; the character
 * walk at 0x005ee7dc then does `MOV EAX,[ESP+0x40]` / `MOVZX EAX,word [EAX]`,
 * and 0x3c - 0x40 lands exactly on entry_esp+4. The entry prologue reads the
 * same slot as `[ESP+0x30]` before the pushes, which agrees.
 *
 * This detector previously read C->edx, on the strength of a "__fastcall
 * (ECX=owner, EDX=&wide buf)" note. EDX is not an input at all: 0x005ee797
 * overwrites it from `[EDI+0x8]` before it is ever read. Whatever the caller
 * happened to leave in EDX often pointed at real wide text -- which is why
 * the wrong pointer still decoded as "Cyclops" and the legal screen and read
 * like a working instrument -- but it was never the string being drawn. Every
 * zero this file reported before that fix was measured against the wrong
 * memory (C267 is retracted on those grounds).
 */
static uint32_t glyph_loop_string(const CPU *C)
{
    return RD32(C->esp + 4u);
}

void x2_override_005ee780(CPU *C)
{
    uint32_t s = glyph_loop_string(C);
    unsigned i;

    g_strings++;
    if (prompt_glyph_pack_enabled() && s &&
        x2_string_has_prompt_glyph(s, MAX_WALK)) {
        g_with_prompts++;
        for (i = 0; i < MAX_WALK; i++) {
            uint16_t c = RD16(s + (uint32_t)i * 2u);
            if (!c) break;
            if (prompt_codepoint(c)) g_prompt_codepoints++;
        }
        if (g_examples < 8) {
            g_examples++;
            log_example(s);
        }
    } else if (s) {
        /* Diagnostic denominator: WHAT is being drawn, if not prompts?
           Every string carrying a non-ASCII wchar is COUNTED for the whole
           run, and every DISTINCT string -- ASCII or not -- is dumped once
           as raw codepoints. The boring case is what gets capped: a control
           word repeated two thousand times costs one slot, so the budget
           survives long enough to reach whatever draws late. */
        unsigned k;
        int non_ascii = 0;
        for (k = 0; k < MAX_WALK; k++) {
            uint16_t ch = RD16(s + (uint32_t)k * 2u);
            if (!ch) break;
            if (ch >= 0x80) { non_ascii = 1; break; }
        }
        if (non_ascii) g_with_non_ascii++;
        if (first_sighting(wide_hash(s, NULL))) log_example(s);
    }
    /* Stage one changes no pixels: the stock body runs on every string. */
    g_super_called++;
    fn_XMen2_005ee780(C);
}

__attribute__((constructor))
static void x2_prompt_draw_register(void)
{
    x86_register_override("XMen2.exe", 0x005ee780, x2_override_005ee780);
}

void x2_prompt_draw_report(void)
{
    fprintf(stderr, "PROMPT DRAW: %lu string(s) reached the glyph loop, %lu "
                    "carried prompt codepoint(s) (0x%02x..0x%02x), %lu "
                    "codepoint(s) total, %lu carried some other non-ASCII "
                    "wchar; %lu super-call(s)\n",
            g_strings, g_with_prompts, X2_PROMPT_GLYPH_FIRST,
            X2_PROMPT_GLYPH_LAST, g_prompt_codepoints, g_with_non_ascii,
            g_super_called);
    fprintf(stderr, "PROMPT DRAW: %u distinct string(s) dumped%s\n",
            g_distinct,
            g_distinct_dropped ? " (SLOTS FULL -- later distinct strings "
                                 "went undumped, so this list is not the "
                                 "whole set)" : "");
    if (!g_strings)
        fprintf(stderr, "PROMPT DRAW: ZERO strings seen -- either nothing "
                        "drew text in this run or the override never armed.\n");
    else if (!prompt_glyph_pack_enabled())
        fprintf(stderr, "PROMPT DRAW: the prompt pack was DISABLED for this "
                        "run (X2_PROMPT_GLYPHS unset), so no string could "
                        "have carried a prompt codepoint. This is not "
                        "evidence about the draw path.\n");
    else if (!g_with_prompts)
        fprintf(stderr, "PROMPT DRAW: the pack was enabled and text drew, yet "
                        "no prompt codepoint reached the glyph loop. Compare "
                        "against the composed-label count in the prompt-label "
                        "report: labels composed but never arriving here means "
                        "they were not drawn in this run OR they reach the "
                        "screen by some other path.\n");
}
