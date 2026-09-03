/* Prompt glyph harvest at the retail text renderer.
 *
 * The game's font assets stay untouched. FUN_005ee780 performs the retail
 * wide-string layout and FUN_005ee400 emits each resulting quad. This owner
 * matches those calls back to the source wchar and retains the port's private
 * prompt rectangles with native-atlas UVs. Every call still reaches the retail
 * emitter: a private glyph's rectangle is collapsed to zero area first. This
 * preserves Alchemy's vertex and batch-finalization semantics (including a
 * string made of one prompt glyph) without drawing a stock-font pixel.
 * prompt_glyph_batch.c inserts the retained art at that finalized boundary;
 * ordinary letters continue through the retail batch over keycap backgrounds.
 *
 * The string is the first STACK argument. An earlier detector read EDX, which
 * the retail body overwrites before use; I069 and docs/RE/text.md retain that
 * failure because the wrong pointer still decoded plausible text and made a
 * false zero look trustworthy.
 */
#include "prompt_glyph_draw.h"

#include "pad_glyph_codes.h"
#include "prompt_glyph_atlas.h" /* GENERATED: the port's own cells */
#include "prompt_glyph_metrics.h"
#include "prompt_glyph_quads.h"
#include "prompt_glyphs.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "guest_body.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_WALK 512u /* widest line buffer the game builds */

static int prompt_codepoint(uint16_t c) {
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

static uint32_t wide_hash(uint32_t s_guest, unsigned *length_out) {
  uint32_t h = 2166136261u;
  unsigned i;
  for (i = 0; i < MAX_WALK; i++) {
    uint16_t c = RD16(s_guest + (uint32_t)i * 2u);
    if (!c)
      break;
    h = (h ^ c) * 16777619u;
  }
  if (length_out)
    *length_out = i;
  return h ? h : 1u;
}

/* First sighting of this exact content? Records it if there is room. */
static int first_sighting(uint32_t hash) {
  unsigned i;
  for (i = 0; i < g_distinct; i++)
    if (g_seen_hash[i] == hash)
      return 0;
  if (g_distinct == DISTINCT_SLOTS) {
    g_distinct_dropped++;
    return 0;
  }
  g_seen_hash[g_distinct++] = hash;
  return 1;
}

int x2_string_has_prompt_glyph(uint32_t s_guest, unsigned max) {
  unsigned i;
  if (!s_guest)
    return 0;
  for (i = 0; i < max; i++) {
    uint16_t c = RD16(s_guest + (uint32_t)i * 2u);
    if (!c)
      return 0;
    if (prompt_codepoint(c))
      return 1;
  }
  return 0;
}

static void log_example(uint32_t where) {
  char buf[MAX_WALK + 1];
  unsigned i;
  fprintf(stderr, "PROMPT DRAW: string at guest 0x%08x wchars:", where);
  for (i = 0; i < MAX_WALK; i++) {
    uint16_t c = RD16(where + (uint32_t)i * 2u);
    if (!c)
      break;
    buf[i] = (c >= 0x20 && c < 0x7f) ? (char)c
             : prompt_codepoint(c)   ? '#'
                                     : '?';
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
static uint32_t glyph_loop_string(const CPU *C) { return RD32(C->esp + 4u); }

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
static uint32_t g_cursor_color;
static unsigned long g_intercepted, g_emitted_seen, g_predicted, g_desync;
static unsigned long g_unavailable_refused, g_color_refused, g_queue_refused;
static unsigned long g_emitted_seen_before;

static int wchar_emits_quad(uint16_t c) {
  if (c >= 256u)
    return 0; /* colour tokens, pen sets, markup */
  if (c == ' ' || c == '\t')
    return 0; /* advance without drawing */
  return 1;
}

/* The next wchar the emitter is about to draw, advancing the cursor. */
static uint16_t cursor_take(void) {
  while (g_cursor_string) {
    uint16_t c = RD16(g_cursor_string + (uint32_t)g_cursor_index * 2u);
    if (!c)
      return 0;
    g_cursor_index++;
    if (wchar_emits_quad(c))
      return c;
  }
  return 0;
}

/* The interception. The outer string override arms this only after it has
 * validated the batch colour and capacity for every native glyph. Each native
 * rectangle is retained, then its retail emitter call is super-called with a
 * zero-area rectangle. The call and its RET 0x20 remain the retail body's
 * responsibility; bypassing it removed the sole vertex from one-glyph labels
 * and therefore removed the drawNonIndexed finalizer we render through. */
void x2_override_005ee400(CPU *C) {
  if (g_cursor_string) {
    uint16_t c = cursor_take();
    const struct x2_prompt_cell *cell = x2_prompt_glyph_cell(c);
    g_emitted_seen++;
    if (cell) {
      struct X2PromptQuad q;
      uint32_t a[8];
      float ex0, ey0, ex1, ey1;
      unsigned i;
      for (i = 0; i < 8u; i++)
        a[i] = RD32(C->esp + (uint32_t)(i + 1u) * 4u);
      memcpy(&ex0, &a[0], 4);
      memcpy(&ey0, &a[1], 4);
      memcpy(&ex1, &a[2], 4);
      memcpy(&ey1, &a[3], 4);
      q.u0 = cell->u0;
      q.v0 = cell->v0;
      q.u1 = cell->u1;
      q.v1 = cell->v1;
      q.codepoint = c;
      /* Keep the engine coordinates exactly as FUN_005ee400 receives
         them. The stock sink stores (x, 0, y), then the text batch's
         world/view/projection places that plane on screen. Inverting
         arg7's local glyph scale here was a false screen-space
         assumption and put otherwise-correct SVG art near the origin. */
      q.x0 = ex0;
      q.y0 = ey0;
      q.x1 = ex1;
      q.y1 = ey1;
      q.color = g_cursor_color;
      if (!x2_prompt_quads_add(&q)) {
        fprintf(stderr, "PROMPT DRAW: reserved queue capacity was "
                        "lost inside one synchronous retail string; "
                        "atomic interception cannot continue.\n");
        abort();
      }
      g_intercepted++;
      /* Keep the call but give the retail atlas zero area. The stock
         function owns both vertex submission and its RET 0x20 ABI. */
      WR32(C->esp + 12u, a[0]);
      WR32(C->esp + 16u, a[1]);
    }
  }
  x86_guest_body(C, "XMen2.exe", 0x005ee400u);
}

struct PromptStringPlan {
  unsigned emitted;
  unsigned native;
  int unavailable;
};

static struct PromptStringPlan plan_string(uint32_t s) {
  struct PromptStringPlan plan = {0};
  unsigned i;
  for (i = 0; i < MAX_WALK; i++) {
    uint16_t c = RD16(s + (uint32_t)i * 2u);
    if (!c)
      break;
    if (wchar_emits_quad(c))
      plan.emitted++;
    if (!prompt_codepoint(c))
      continue;
    if (!x2_prompt_glyph_cell(c))
      plan.unavailable = 1;
    else
      plan.native++;
  }
  return plan;
}

void x2_override_005ee780(CPU *C) {
  uint32_t s = glyph_loop_string(C);
  unsigned i;

  g_strings++;
  if (x2_prompt_glyphs_enabled() && s &&
      x2_string_has_prompt_glyph(s, MAX_WALK)) {
    g_with_prompts++;
    for (i = 0; i < MAX_WALK; i++) {
      uint16_t c = RD16(s + (uint32_t)i * 2u);
      if (!c)
        break;
      if (prompt_codepoint(c))
        g_prompt_codepoints++;
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
      if (!ch)
        break;
      if (ch >= 0x80) {
        non_ascii = 1;
        break;
      }
    }
    if (non_ascii)
      g_with_non_ascii++;
    if (first_sighting(wide_hash(s, NULL)))
      log_example(s);
  }
  /* The cursor is armed only for a string carrying our codepoints, so
     every other string's quads take the untouched path. */
  if (x2_prompt_glyphs_enabled() && s &&
      x2_string_has_prompt_glyph(s, MAX_WALK)) {
    struct PromptStringPlan plan = plan_string(s);
    uint32_t batch = RD32(C->esp + 8u);
    uint32_t color;

    /* This decision is string-atomic. Once the retail loop starts, an
       earlier native glyph may already have been collapsed; discovering
       a later refusal then cannot restore the original string. Validate
       every precondition first, including the one-glyph case. */
    if (plan.unavailable || !plan.native) {
      g_unavailable_refused++;
      g_super_called++;
      x86_guest_body(C, "XMen2.exe", 0x005ee780u);
      return;
    }
    if (!x86_peek32(batch + 8u, &color)) {
      g_color_refused++;
      g_super_called++;
      x86_guest_body(C, "XMen2.exe", 0x005ee780u);
      return;
    }
    if (x2_prompt_quads_available() < plan.native) {
      g_queue_refused++;
      g_super_called++;
      x86_guest_body(C, "XMen2.exe", 0x005ee780u);
      return;
    }

    g_predicted += plan.emitted;
    g_emitted_seen_before = g_emitted_seen;
    g_cursor_string = s;
    g_cursor_index = 0;
    g_cursor_color = color;
    g_super_called++;
    x86_guest_body(C, "XMen2.exe", 0x005ee780u);
    g_cursor_string = 0;
    if (g_emitted_seen - g_emitted_seen_before != plan.emitted) {
      g_desync++;
      fprintf(stderr,
              "PROMPT DRAW: quad/wchar DESYNC -- predicted %u "
              "quad(s) for this string, the emitter produced "
              "%lu. The cursor model is wrong, so an "
              "interception may have hit the wrong glyph.\n",
              plan.emitted, g_emitted_seen - g_emitted_seen_before);
    }
    return;
  }
  g_super_called++;
  x86_guest_body(C, "XMen2.exe", 0x005ee780u);
}

__attribute__((constructor)) static void x2_prompt_draw_register(void) {
  x86_register_override("XMen2.exe", 0x005ee780, x2_override_005ee780);
  x86_register_override("XMen2.exe", 0x005ee400, x2_override_005ee400);
}

void x2_prompt_draw_report(void) {
  fprintf(stderr,
          "PROMPT DRAW: %lu string(s) reached the glyph loop, %lu "
          "carried prompt codepoint(s) (0x%02x..0x%02x), %lu "
          "codepoint(s) total, %lu carried some other non-ASCII "
          "wchar; %lu super-call(s)\n",
          g_strings, g_with_prompts, X2_PROMPT_GLYPH_FIRST,
          X2_PROMPT_GLYPH_LAST, g_prompt_codepoints, g_with_non_ascii,
          g_super_called);
  fprintf(stderr,
          "PROMPT DRAW: %lu quad(s) intercepted for the port out of "
          "%lu the emitter produced for our strings (%lu predicted); "
          "%lu desync(s); %lu whole string(s) kept stock because a "
          "codepoint was not private, %lu because the engine batch color "
          "was unreadable, %lu because "
          "the frame queue lacked capacity\n",
          g_intercepted, g_emitted_seen, g_predicted, g_desync,
          g_unavailable_refused, g_color_refused, g_queue_refused);
  if (g_desync)
    fprintf(stderr, "PROMPT DRAW: the quad/wchar cursor DESYNCED -- the "
                    "harvested rectangles are not trustworthy.\n");
  fprintf(stderr, "PROMPT DRAW: %u distinct string(s) dumped%s\n", g_distinct,
          g_distinct_dropped ? " (SLOTS FULL -- later distinct strings "
                               "went undumped, so this list is not the "
                               "whole set)"
                             : "");
  if (!g_strings)
    fprintf(stderr, "PROMPT DRAW: ZERO strings seen -- either nothing "
                    "drew text in this run or the override never armed.\n");
  else if (!x2_prompt_glyphs_enabled())
    fprintf(stderr, "PROMPT DRAW: native prompt glyphs were DISABLED for this "
                    "run (X2_PROMPT_GLYPHS=0), so no string could "
                    "have carried a prompt codepoint. This is not "
                    "evidence about the draw path.\n");
  else if (!g_with_prompts)
    fprintf(stderr, "PROMPT DRAW: native prompt glyphs were enabled and "
                    "text drew, yet "
                    "no prompt codepoint reached the glyph loop. Compare "
                    "against the composed-label count in the prompt-label "
                    "report: labels composed but never arriving here means "
                    "they were not drawn in this run OR they reach the "
                    "screen by some other path.\n");
}
