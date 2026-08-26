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
#include "prompt_glyph_atlas.h"   /* GENERATED: the port's own cells */
#include "prompt_glyph_metrics.h"
#include "prompt_glyph_pack.h"
#include "prompt_glyph_quads.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* The seven stack arguments, dumped from a REAL call that carries our
 * codepoints. `RET 0x1c` says there are seven; which one is the pen, which
 * the draw object and which the cached scales is not something to derive
 * from frame arithmetic -- ESP moves across the body's own calls, and that
 * is precisely where hand-derivation goes wrong. So the run says it.
 *
 * Each argument is printed raw, and again as a float, and if it points into
 * mapped guest memory the first six dwords behind it are printed both ways.
 * Gated by X2_GLYPH_ARGS so ordinary runs pay nothing.
 */
static void dump_args(const CPU *C)
{
    unsigned a, k;
    fprintf(stderr, "GLYPH ARGS: ecx=0x%08x edx=0x%08x esp=0x%08x\n",
            C->ecx, C->edx, C->esp);
    for (a = 1; a <= 7; a++) {
        uint32_t v = RD32(C->esp + (uint32_t)a * 4u);
        float f;
        uint32_t probe;
        memcpy(&f, &v, sizeof f);
        fprintf(stderr, "GLYPH ARGS:   arg%u [esp+0x%02x] = 0x%08x  (%.4f)",
                a, a * 4u, v, (double)f);
        if (v && x86_peek32(v, &probe)) {
            /* args 5 and 7 are the objects carrying the position scales and
               offsets the stock quad is built from, so they get a deeper
               read than the rest. */
            unsigned depth = (a == 5u || a == 7u) ? 16u : 6u;
            fprintf(stderr, "  ->");
            for (k = 0; k < depth; k++) {
                uint32_t w;
                float wf;
                if (!x86_peek32(v + k * 4u, &w)) break;
                memcpy(&wf, &w, sizeof wf);
                fprintf(stderr, " [%u]=0x%08x/%.4f", k, w, (double)wf);
            }
        }
        fprintf(stderr, "\n");
    }
}

/* The quad emitter, watched only while OUR string is being drawn.
 *
 * FUN_005ee400 is where a character becomes a quad in the current batch
 * (`RET 0x20` -- eight stack args, ECX = the batch owner). Stage two has to
 * emit through this same function, so its arguments have to be known; and
 * because the pen is a LOCAL inside FUN_005ee780 (EBP, initialised to
 * *(arg6) + arg3 and never written back), the per-character x it passes here
 * is the only way to watch the pen advance from outside.
 *
 * Scoped to a prompt-carrying string: the emitter fires for every character
 * of every string in the frame, and an unscoped dump is unreadable. The
 * scope is set around the super-call, so what prints is exactly the quads of
 * a label we composed.
 */
static int g_watch_quads;
static unsigned g_quads_dumped;

/* Which wchar the emitter is on.
 *
 * FUN_005ee400 does not say which character it is drawing, so the override
 * walks the same string in the same order and consumes one quad per wchar
 * THAT EMITS ONE. The exceptions are the drawer's own, read out of the body
 * (docs/RE/text.md): a space and a tab advance the pen without drawing,
 * colour tokens (1000..1999) and absolute pen sets (>=3000) draw nothing,
 * and only wchar < 256 reaches the glyph path at all.
 *
 * If that model is wrong the cursor drifts and the wrong glyph is
 * intercepted, so the run CHECKS it: the number of quads the emitter
 * produced is compared against the number this walk predicted, and a
 * mismatch is reported rather than absorbed. */
static uint32_t g_cursor_string;
static unsigned g_cursor_index;
static unsigned long g_intercepted, g_emitted_seen, g_predicted, g_desync;
static unsigned long g_emitted_seen_before;

static int wchar_emits_quad(uint16_t c)
{
    if (c >= 256u) return 0;          /* colour tokens, pen sets, markup */
    if (c == ' ' || c == '\t') return 0;   /* advance without drawing */
    return 1;
}

/* The next wchar the emitter is about to draw, advancing the cursor. */
static uint16_t cursor_take(void)
{
    while (g_cursor_string) {
        uint16_t c = RD16(g_cursor_string + (uint32_t)g_cursor_index * 2u);
        if (!c) return 0;
        g_cursor_index++;
        if (wchar_emits_quad(c)) return c;
    }
    return 0;
}

void fn_XMen2_005ee400(CPU *C);
static void x2_probe_005ee400_dump(CPU *C);

/* The interception. While one of our labels is being drawn, each quad is
 * matched to its wchar; a quad belonging to one of the port's codepoints is
 * RECORDED with the port atlas's UVs and then NOT emitted, so the engine
 * never draws our codepoint out of its own font atlas. Every other quad
 * super-calls untouched. */
static void x2_override_005ee400(CPU *C)
{
    if (g_cursor_string) {
        uint16_t c = cursor_take();
        const struct x2_prompt_cell *cell = x2_prompt_glyph_cell(c);
        g_emitted_seen++;
        if (cell) {
            struct X2PromptQuad q;
            uint32_t a[8];
            unsigned i;
            for (i = 0; i < 8u; i++)
                a[i] = RD32(C->esp + (uint32_t)(i + 1u) * 4u);
            memcpy(&q.x0, &a[0], 4); memcpy(&q.y0, &a[1], 4);
            memcpy(&q.x1, &a[2], 4); memcpy(&q.y1, &a[3], 4);
            q.u0 = cell->u0; q.v0 = cell->v0;
            q.u1 = cell->u1; q.v1 = cell->v1;
            q.codepoint = c;
            x2_prompt_quads_add(&q);
            g_intercepted++;
            /* Suppressed on purpose -- but the ABI still has to be honoured.
               FUN_005ee400 is `RET 0x20`: the CALLEE pops the return address
               and its eight dword arguments. Returning without running the
               body has to do exactly that, or the guest stack is left 32
               bytes deep in arguments and the caller unwinds into garbage.
               (It does; skipping the pop crashed the run.) */
            C->esp += 4u + 0x20u;
            return;
        }
    }
    x2_probe_005ee400_dump(C);
    fn_XMen2_005ee400(C);
}

static void x2_probe_005ee400_dump(CPU *C)
{
    if (g_watch_quads && g_quads_dumped < 64u) {
        unsigned a;
        g_quads_dumped++;
        fprintf(stderr, "QUAD: ecx=0x%08x", C->ecx);
        for (a = 1; a <= 8u; a++) {
            uint32_t v = RD32(C->esp + (uint32_t)a * 4u);
            float f;
            memcpy(&f, &v, sizeof f);
            fprintf(stderr, "  a%u=%.3f/0x%08x", a, (double)f, v);
        }
        fprintf(stderr, "\n");
        if (g_quads_dumped == 1u) {
            /* The vertex sink, once: FUN_005ee400 pushes each corner through
               [ECX] -> vtable +0xc. Naming that target is the last hop
               between these coordinates and the screen, which is what the
               port needs if it is to draw the prompt art itself. */
            uint32_t sink, vtable, target, state;
            if (x86_peek32(C->ecx, &sink) && x86_peek32(sink, &vtable) &&
                x86_peek32(vtable + 0xcu, &target)) {
                fprintf(stderr, "QUAD SINK: batch=0x%08x sink=0x%08x "
                                "vtable=0x%08x  vtable+0xc -> 0x%08x\n",
                        C->ecx, sink, vtable, target);
                if (x86_peek32(C->ecx + 8u, &state))
                    fprintf(stderr, "QUAD SINK: [batch+8] = 0x%08x\n", state);
            } else {
                fprintf(stderr, "QUAD SINK: could not read the sink behind "
                                "batch 0x%08x -- no vtable, so the emitter's "
                                "target is unknown for this call.\n", C->ecx);
            }
        }
    }
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
            if (getenv("X2_GLYPH_ARGS")) {
                dump_args(C);
                g_watch_quads = 1;
            }
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
    /* PEN PROBE. Reasoning back from the emitted quads said the pen runs
       107 -> 300 while arg3 is 298, which cannot both be true of "the pen
       starts at *(arg6) + arg3". Rather than do more arithmetic on a frame
       -- the thing that has already been wrong twice here -- perturb the
       argument and watch what moves: X2_GLYPH_PEN=<int> adds that many pen
       units to arg3 for prompt-carrying strings only. If arg3 is the pen
       origin, every emitted quad shifts by exactly delta * the position
       scale; if nothing moves, it is not the origin. */
    if (g_watch_quads) {
        const char *delta = getenv("X2_GLYPH_PEN");
        if (delta && *delta) {
            uint32_t slot = C->esp + 0x0cu;
            int32_t was = (int32_t)RD32(slot);
            int32_t now = was + (int32_t)strtol(delta, NULL, 0);
            WR32(slot, (uint32_t)now);
            fprintf(stderr, "PEN PROBE: arg3 %d -> %d\n", was, now);
        }
    }
    /* TRUNCATION PROBE -- the question segmentation actually turns on.
       Splitting a string means NUL-terminating a prefix in place and drawing
       it. That is safe only if the run's origin does NOT depend on the
       string's own length. If the loop (or its caller's cached measurement)
       centres the text, a shorter segment starts somewhere else and every
       segment after it is misplaced. X2_GLYPH_TRUNC=<n> cuts the string to n
       wchars for the draw and puts the wchar back afterwards. */
    if (g_watch_quads) {
        const char *cut = getenv("X2_GLYPH_TRUNC");
        if (cut && *cut) {
            unsigned n = (unsigned)strtoul(cut, NULL, 0);
            uint32_t at = s + (uint32_t)n * 2u;
            uint16_t saved = RD16(at);
            fprintf(stderr, "TRUNC PROBE: cutting to %u wchar(s) "
                            "(dropping 0x%04x)\n", n, saved);
            WR16(at, 0);
            g_super_called++;
            fn_XMen2_005ee780(C);
            WR16(at, saved);
            g_watch_quads = 0;
            return;
        }
    }
    /* The cursor is armed only for a string carrying our codepoints, so
       every other string's quads take the untouched path. */
    if (prompt_glyph_pack_enabled() && s && x2_string_has_prompt_glyph(s, MAX_WALK)) {
        unsigned k, predicted = 0;
        for (k = 0; k < MAX_WALK; k++) {
            uint16_t c = RD16(s + (uint32_t)k * 2u);
            if (!c) break;
            if (wchar_emits_quad(c)) predicted++;
        }
        g_predicted += predicted;
        g_emitted_seen_before = g_emitted_seen;
        g_cursor_string = s;
        g_cursor_index = 0;
        g_super_called++;
        fn_XMen2_005ee780(C);
        g_cursor_string = 0;
        if (g_emitted_seen - g_emitted_seen_before != predicted) {
            g_desync++;
            fprintf(stderr, "PROMPT DRAW: quad/wchar DESYNC -- predicted %u "
                            "quad(s) for this string, the emitter produced "
                            "%lu. The cursor model is wrong, so an "
                            "interception may have hit the wrong glyph.\n",
                    predicted,
                    g_emitted_seen - g_emitted_seen_before);
        }
        g_watch_quads = 0;
        return;
    }
    g_super_called++;
    fn_XMen2_005ee780(C);
    g_watch_quads = 0;
}

__attribute__((constructor))
static void x2_prompt_draw_register(void)
{
    x86_register_override("XMen2.exe", 0x005ee780, x2_override_005ee780);
    x86_register_override("XMen2.exe", 0x005ee400, x2_override_005ee400);
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
    fprintf(stderr, "PROMPT DRAW: %lu quad(s) intercepted for the port out of "
            "%lu the emitter produced for our strings (%lu predicted); "
            "%lu desync(s)\n",
            g_intercepted, g_emitted_seen, g_predicted, g_desync);
    if (g_desync)
        fprintf(stderr, "PROMPT DRAW: the quad/wchar cursor DESYNCED -- the "
                        "harvested rectangles are not trustworthy.\n");
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
