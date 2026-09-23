#include "guest_memory.h"
#include "x2_log.h"
/* Prompt glyph harvest at the retail text renderer.
 *
 * The game's font assets stay untouched. FUN_005ee780 performs the retail
 * wide-string layout and FUN_005ee400 emits each resulting quad. This owner
 * matches those calls back to the source wchar and retains the port's private
 * prompt rectangles with native-atlas UVs. Every call still reaches the retail
 * emitter: a private glyph's rectangle is collapsed to zero area first. This
 * preserves Alchemy's vertex and batch-finalization semantics (including a
 * string made of one prompt glyph) without drawing a stock-font pixel.
 * prompt_glyph_batch.c inserts the retained art at that finalized boundary.
 * A keyboard key (keycap_run.h) is drawn whole from shared art over the span
 * its edges reserve, so the binding's letters inside it are collapsed too.
 *
 * The string is the first STACK argument. An earlier detector read EDX, which
 * the retail body overwrites before use; I069 and docs/RE/text.md retain that
 * failure because the wrong pointer still decoded plausible text and made a
 * false zero look trustworthy.
 */
#include "prompt_glyph_draw.h"

#include "keycap_labels.h"
#include "keycap_run.h"
#include "pad_glyph_codes.h"
#include "prompt_glyph_atlas.h" /* GENERATED: the port's own cells */
#include "prompt_glyph_metrics.h"
#include "prompt_glyph_quads.h"
#include "prompt_glyphs.h"
#include "prompt_string_census.h"
#include "prompt_touch_buttons.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "guest_body.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long g_super_called;

/* The wide string is FUN_005ee780's FIRST STACK ARGUMENT, not EDX.
 *
 * Read out of the retail body: the prologue is `SUB ESP,0x2c` then four
 * pushes (EBX, EBP, ESI, EDI), putting ESP 0x3c below entry; the character
 * walk at 0x005ee7dc then does `MOV EAX,[ESP+0x40]` / `MOVZX EAX,word [EAX]`,
 * and 0x3c - 0x40 lands exactly on entry_esp+4. The entry prologue reads the
 * same slot as `[ESP+0x30]` before the pushes, which agrees.
 *
 * This detector previously read C->reg[kX86pEdx], on the strength of a
 * "__fastcall (ECX=owner, EDX=&wide buf)" note. EDX is not an input at all:
 * 0x005ee797 overwrites it from `[EDI+0x8]` before it is ever read. Whatever
 * the caller happened to leave in EDX often pointed at real wide text -- which
 * is why the wrong pointer still decoded as "Cyclops" and the legal screen and
 * read like a working instrument -- but it was never the string being drawn.
 * Every zero this file reported before that fix was measured against the wrong
 * memory (C267 is retracted on those grounds).
 */
static uint32_t glyph_loop_string(const CPU *C) {
  return RD32(C->reg[kX86pEsp] + 4u);
}

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
/* The cursor is armed for one of two reasons: to swap prompt art in, or to
   take a key off a touch prompt and slide its words over. They never overlap
   -- a rewritten prompt draws no native glyph -- so one cursor serves both
   and the emitter cannot be asked to do both to the same quad. */
static int g_touch_mode;
static unsigned g_touch_emits;
static unsigned long g_unavailable_refused, g_color_refused, g_queue_refused;
static unsigned long g_emitted_seen_before;
/* The keycap run the cursor is inside: its left edge's rectangle and where
   its name starts. */
static struct {
  int open;
  float left[4];
  unsigned name_at;
} g_cap;
static unsigned long g_keys_drawn;

int x2_glyph_loop_emits_quad(uint16_t c) {
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
    if (x2_glyph_loop_emits_quad(c))
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
/* The emitter's four engine-plane corners, at ESP+4..ESP+16. */
static void read_corners(const CPU *C, float corners[4]) {
  unsigned i;
  for (i = 0; i < 4u; i++) {
    uint32_t bits = RD32(C->reg[kX86pEsp] + (uint32_t)(i + 1u) * 4u);
    memcpy(&corners[i], &bits, 4);
  }
}

static void write_corners(CPU *C, const float corners[4]) {
  unsigned i;
  for (i = 0; i < 4u; i++) {
    uint32_t bits;
    memcpy(&bits, &corners[i], 4);
    WR32(C->reg[kX86pEsp] + (uint32_t)(i + 1u) * 4u, bits);
  }
}

/* Give the retail emitter a zero-area rectangle: it still owns vertex
   submission and its RET 0x20, but draws no stock-font pixel. */
static void collapse(CPU *C) {
  WR32(C->reg[kX86pEsp] + 12u, RD32(C->reg[kX86pEsp] + 4u));
  WR32(C->reg[kX86pEsp] + 16u, RD32(C->reg[kX86pEsp] + 8u));
}

static void retain(const struct X2PromptQuad *q) {
  if (!x2_prompt_quads_add(q)) {
    x2_log_error("PROMPT DRAW: reserved queue capacity was "
                 "lost inside one synchronous retail string; "
                 "atomic interception cannot continue.\n");
    abort();
  }
  g_intercepted++;
}

static void intercept_glyph(CPU *C, uint16_t c) {
  const struct x2_prompt_cell *cell = x2_prompt_glyph_cell(c);
  struct X2PromptQuad q;
  float corners[4];

  if (!cell) {
    return;
  }
  read_corners(C, corners);
  q.u0 = cell->u0;
  q.v0 = cell->v0;
  q.u1 = cell->u1;
  q.v1 = cell->v1;
  q.codepoint = c;
  /* Keep the engine coordinates exactly as FUN_005ee400 receives them. The
     stock sink stores (x, 0, y), then the text batch's world/view/projection
     places that plane on screen. Inverting arg7's local glyph scale here was
     a false screen-space assumption and put otherwise-correct SVG art near
     the origin. */
  q.x0 = corners[0];
  q.y0 = corners[1];
  q.x1 = corners[2];
  q.y1 = corners[3];
  q.color = g_cursor_color;
  q.sheet = X2_KEYCAP_SHEET_ATLAS;
  retain(&q);
  collapse(C);
}

/* One glyph of a keycap run: the left edge opens it, the name's letters
   draw nothing, and the right edge draws the whole key. plan_string has
   already proved the run is well formed and its name has art. */
static void intercept_keycap(CPU *C, uint16_t c) {
  if (c == X2_KEYCAP_GLYPH_LEFT) {
    read_corners(C, g_cap.left);
    g_cap.open = 1;
    g_cap.name_at = g_cursor_index;
  } else if (c == X2_KEYCAP_GLYPH_RIGHT) {
    uint16_t name[X2_KEYCAP_NAME_MAX];
    struct X2PromptQuad quads[X2_KEYCAP_QUADS];
    const struct x2_keycap_art *art;
    const unsigned length = g_cursor_index - 1u - g_cap.name_at;
    float right[4];
    unsigned i;

    for (i = 0; i < length && i < X2_KEYCAP_NAME_MAX; i++) {
      name[i] = RD16(g_cursor_string + (g_cap.name_at + i) * 2u);
    }
    art = x2_keycap_label_art(name, length);
    if (!art) {
      x2_log_error("PROMPT DRAW: a keycap run the plan accepted has no "
                   "label art at draw time; the cursor model is wrong.\n");
      abort();
    }
    read_corners(C, right);
    x2_keycap_quads(g_cap.left, right, art, g_cursor_color, quads);
    for (i = 0; i < X2_KEYCAP_QUADS; i++) {
      retain(&quads[i]);
    }
    g_cap.open = 0;
    g_keys_drawn++;
  }
  collapse(C);
}

void x2_override_005ee400(CPU *C) {
  if (g_cursor_string && g_touch_mode) {
    float corners[4];
    (void)cursor_take();
    g_emitted_seen++;
    read_corners(C, corners);
    if (x2_prompt_touch_glyph(g_touch_emits++, &corners[0], &corners[1],
                              &corners[2], &corners[3]))
      write_corners(C, corners);
    x86_guest_body(C, "XMen2.exe", 0x005ee400u);
    return;
  }
  if (g_cursor_string) {
    uint16_t c = cursor_take();
    g_emitted_seen++;
    if (c == X2_KEYCAP_GLYPH_LEFT || g_cap.open) {
      intercept_keycap(C, c);
    } else {
      intercept_glyph(C, c);
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
  uint16_t wide[X2_PROMPT_WALK_MAX];
  unsigned i, n, key_end = UINT_MAX; /* the open key's right edge */
  for (n = 0; n < X2_PROMPT_WALK_MAX; n++) {
    wide[n] = RD16(s + (uint32_t)n * 2u);
    if (!wide[n])
      break;
  }
  for (i = 0; i < n; i++) {
    const uint16_t c = wide[i];
    if (x2_glyph_loop_emits_quad(c))
      plan.emitted++;
    if (!x2_prompt_codepoint(c))
      continue;
    if (c == X2_KEYCAP_GLYPH_LEFT) {
      const unsigned run = x2_keycap_run_length(wide, n, i);
      if (!run || !x2_prompt_glyph_cell(X2_KEYCAP_GLYPH_LEFT) ||
          !x2_prompt_glyph_cell(X2_KEYCAP_GLYPH_RIGHT) ||
          !x2_keycap_label_art(wide + i + 1u, run - 2u)) {
        plan.unavailable = 1;
        continue;
      }
      key_end = i + run - 1u;
      plan.native += X2_KEYCAP_QUADS;
    } else if (c == X2_KEYCAP_GLYPH_RIGHT) {
      if (i != key_end)
        plan.unavailable = 1; /* an edge that closes no key */
    } else if (!x2_prompt_glyph_cell(c)) {
      plan.unavailable = 1;
    } else {
      plan.native++;
    }
  }
  return plan;
}

/* Every string the loop lays out enters the layout record, ours or not: a
   draw submits a window of adjacent strings, and a window can only be summed
   over the strings that are in it (prompt_glyph_quads.c). */
static void record_stock_string(uint32_t s) {
  if (!s || !x2_prompt_quads_begin_run(x2_prompt_string_hash(s, NULL),
                                       plan_string(s).emitted))
    return;
  x2_prompt_quads_end_run();
}

void x2_override_005ee780(CPU *C) {
  uint32_t s = glyph_loop_string(C);
  unsigned i;

  x2_prompt_string_census(s);
  /* A touch prompt is rewritten instead of decorated: its key comes off and
     its words slide into the space, so the native keycap art it would
     otherwise carry is exactly what must not be drawn. */
  if (s) {
    unsigned length = 0;
    (void)x2_prompt_string_hash(s, &length);
    if (x2_prompt_touch_begin(s, length)) {
      record_stock_string(s);
      g_cursor_string = s;
      g_cursor_index = 0;
      g_touch_mode = 1;
      g_touch_emits = 0;
      g_super_called++;
      x86_guest_body(C, "XMen2.exe", 0x005ee780u);
      g_cursor_string = 0;
      g_touch_mode = 0;
      x2_prompt_touch_end();
      return;
    }
  }
  /* The cursor is armed only for a string carrying our codepoints, so
     every other string's quads take the untouched path. */
  if (x2_prompt_glyphs_enabled() && s &&
      x2_string_has_prompt_glyph(s, X2_PROMPT_WALK_MAX)) {
    struct PromptStringPlan plan = plan_string(s);
    uint32_t batch = RD32(C->reg[kX86pEsp] + 8u);
    uint32_t color;

    /* This decision is string-atomic. Once the retail loop starts, an
       earlier native glyph may already have been collapsed; discovering
       a later refusal then cannot restore the original string. Validate
       every precondition first, including the one-glyph case. */
    if (plan.unavailable || !plan.native) {
      g_unavailable_refused++;
      record_stock_string(s);
      g_super_called++;
      x86_guest_body(C, "XMen2.exe", 0x005ee780u);
      return;
    }
    if (!guest_memory_try_read32(batch + 8u, &color)) {
      g_color_refused++;
      record_stock_string(s);
      g_super_called++;
      x86_guest_body(C, "XMen2.exe", 0x005ee780u);
      return;
    }
    if (x2_prompt_quads_available() < plan.native) {
      g_queue_refused++;
      record_stock_string(s);
      g_super_called++;
      x86_guest_body(C, "XMen2.exe", 0x005ee780u);
      return;
    }

    if (!x2_prompt_quads_begin_run(x2_prompt_string_hash(s, NULL),
                                   plan.emitted)) {
      x2_log_error("PROMPT DRAW: a string run could not open because "
                   "another was still open; atomic interception cannot "
                   "continue.\n");
      abort();
    }
    g_predicted += plan.emitted;
    g_emitted_seen_before = g_emitted_seen;
    g_cursor_string = s;
    g_cursor_index = 0;
    g_cursor_color = color;
    g_super_called++;
    x86_guest_body(C, "XMen2.exe", 0x005ee780u);
    g_cursor_string = 0;
    g_cap.open = 0;
    x2_prompt_quads_end_run();
    if (g_emitted_seen - g_emitted_seen_before != plan.emitted) {
      g_desync++;
      x2_log_error("PROMPT DRAW: quad/wchar DESYNC -- predicted %u "
                   "quad(s) for this string, the emitter produced "
                   "%lu. The cursor model is wrong, so an "
                   "interception may have hit the wrong glyph.\n",
                   plan.emitted, g_emitted_seen - g_emitted_seen_before);
    }
    return;
  }
  record_stock_string(s);
  g_super_called++;
  x86_guest_body(C, "XMen2.exe", 0x005ee780u);
}

__attribute__((constructor)) static void x2_prompt_draw_register(void) {
  x86_register_override("XMen2.exe", 0x005ee780, x2_override_005ee780);
  x86_register_override("XMen2.exe", 0x005ee400, x2_override_005ee400);
}

void x2_prompt_draw_report(void) {
  x2_prompt_string_census_report();
  x2_log_error("PROMPT DRAW: %lu super-call(s) of the glyph loop\n",
               g_super_called);
  x2_log_error("PROMPT DRAW: %lu quad(s) intercepted for the port out of "
               "%lu the emitter produced for our strings (%lu predicted); "
               "%lu desync(s); %lu whole string(s) kept stock because a "
               "codepoint was not private, %lu because the engine batch color "
               "was unreadable, %lu because "
               "the frame queue lacked capacity\n",
               g_intercepted, g_emitted_seen, g_predicted, g_desync,
               g_unavailable_refused, g_color_refused, g_queue_refused);
  x2_log_error("PROMPT DRAW: %lu keyboard key(s) drawn whole from shared "
               "art\n",
               g_keys_drawn);
  if (g_desync) {
    x2_log_error("PROMPT DRAW: the quad/wchar cursor DESYNCED -- the "
                 "harvested rectangles are not trustworthy.\n");
  }
}
