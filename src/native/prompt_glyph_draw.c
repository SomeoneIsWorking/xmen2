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
 * STAGE ONE RAN, AND THE ANSWER WAS NO -- claim C267, docs/RE/text.md. On a
 * boot-direct tutorial run the label override composed 2,264 keycap labels
 * and ZERO prompt codepoints reached this loop; the same run with the pack
 * off draws the same string population, so enabling the pack changes nothing
 * that arrives here. prompt_labels.c writes its label as a NARROW byte
 * string and this loop walks a WIDE one, so something between them widens.
 * What the run does NOT establish is whether those labels were ever meant to
 * be on screen in that scenario -- 2,272 label READS happen either way, and a
 * read is not a draw. Do not build stage two on the arrival until a scenario
 * whose labels are demonstrably visible (the Controls binding menu) has been
 * measured.
 *
 * The zero is a measurement and not silence: tests/test_prompt_glyph_draw.c
 * drives x2_override_005ee780 over guest memory with a composed keycap label
 * and watches it counted, so the detector has produced the other answer.
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
static unsigned g_non_ascii_examples;

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

void x2_override_005ee780(CPU *C)
{
    uint32_t s = C->edx;
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
           The first 40 strings are sampled to show text arrives at all; the
           BORING case is capped, never the interesting one -- every string
           carrying a non-ASCII wchar is counted for the whole run, and the
           first few are dumped as raw codepoints. Rendering those as '?'
           was hiding the only evidence that distinguishes a near miss. */
        char buf[MAX_WALK + 1];
        unsigned k;
        int non_ascii = 0;
        for (k = 0; k < MAX_WALK; k++) {
            uint16_t ch = RD16(s + (uint32_t)k * 2u);
            if (!ch) break;
            if (ch >= 0x80) non_ascii = 1;
            buf[k] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '?';
        }
        buf[k] = 0;
        if (non_ascii) {
            g_with_non_ascii++;
            if (g_non_ascii_examples < 8) {
                g_non_ascii_examples++;
                log_example(s);
            }
        } else if (g_strings <= 40) {
            fprintf(stderr, "PROMPT DRAW: sample string: \"%s\"\n", buf);
        }
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
