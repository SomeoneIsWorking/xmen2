#include "prompt_touch_buttons.h"

#include "../config/settings_store.h"
#include "../input/touch_prompt_buttons.h"
#include "../input/touch_runtime.h"
#include "guest_clock.h"
#include "pad_glyph_codes.h"
#include "prompt_action_labels.h"
#include "prompt_glyph_draw.h"
#include "x2_log.h"
#include "x86rt.h"

#include <math.h>
#include <stdio.h>

#define MAX_PENDING 4u
#define MAX_WALK 512u

/* One prompt whose words have been measured but whose batch has not yet
   published a finalized transform. */
struct Pending {
  X2Rect engine;
  unsigned dik;
  /* Every quad the string emits, key included: what the draw that submits it
     will count. */
  unsigned quads;
};

static struct Pending g_pending[MAX_PENDING];
static unsigned g_pending_count;

static struct {
  int armed;
  /* The emitting characters belonging to the key, as a half-open range of
     emit indices: the cap does not always open the string. */
  unsigned suppress_from;
  unsigned suppress_to;
  unsigned quads;
  unsigned dik;
  int have_head;
  float head_x;
  int have_words;
  float shift;
  X2Rect words;
} g_string;

static unsigned long g_seen, g_inactive, g_unmatched;
/* How long a published control has to stay pressable is decided by how often
   the game redraws the prompt, and Alchemy lays text out on change rather
   than per frame. Measured rather than assumed: the longest gap between two
   publications of the same key is the shortest lifetime that can hold. */
static double g_last_publish, g_longest_gap;
static unsigned long g_claimed, g_published, g_evicted, g_relaid;
static unsigned long g_no_viewport, g_offscreen, g_unclaimed_draws;
static unsigned long g_no_transform;

/* One line per distinct capped string this owner walked away from. Capped by
   CONTENT, not by arrival: a footer redrawn a thousand times costs one line
   and the rare shape still gets printed. */
#define MAX_UNCLAIMED 8u
static uint32_t g_unclaimed_hash[MAX_UNCLAIMED];
static unsigned g_n_unclaimed;

static void report_unclaimed(const uint16_t *wide, unsigned length) {
  char text[MAX_WALK + 1];
  uint32_t hash = 2166136261u;
  unsigned i;

  for (i = 0; i < length; i++) {
    hash = (hash ^ wide[i]) * 16777619u;
  }
  for (i = 0; i < g_n_unclaimed; i++) {
    if (g_unclaimed_hash[i] == hash) {
      return;
    }
  }
  if (g_n_unclaimed == MAX_UNCLAIMED) {
    return;
  }
  g_unclaimed_hash[g_n_unclaimed++] = hash;
  {
    unsigned at = 0;
    for (i = 0; i < length && at + 8u < sizeof text; i++) {
      if (wide[i] >= 0x20u && wide[i] < 0x7fu) {
        text[at++] = (char)wide[i];
      } else {
        at += (unsigned)snprintf(text + at, sizeof text - at, "<%02x>",
                                 (unsigned)wide[i]);
      }
    }
    text[at] = 0;
  }
  x2_log_info("PROMPT TOUCH: a key cap was drawn in \"%s\" and this owner "
              "did not claim it -- no key is retained under that name, or the "
              "cap is the whole string and IS the binding\n",
              text);
}

unsigned x2_prompt_touch_begin(uint32_t string_guest, unsigned length) {
  uint16_t wide[MAX_WALK];
  unsigned i, before = 0, inside = 0;
  X2PromptKeyCap cap;

  g_string.armed = 0;
  if (!string_guest || !length || length > MAX_WALK) {
    return 0;
  }
  g_seen++;
  if (!x2_touch_runtime_active()) {
    g_inactive++;
    return 0;
  }
  for (i = 0; i < length; i++) {
    wide[i] = RD16(string_guest + (uint32_t)i * 2u);
  }
  if (!x2_prompt_action_label_match(wide, length, &cap)) {
    g_unmatched++;
    /* THE NEGATIVE THAT CAN SPEAK. A string carrying a key cap that this
       owner did not claim is the interesting failure -- the cap is on screen
       and no finger can press it -- and "17,820 unmatched" cannot tell that
       from the thousands of ordinary words that also come through here. */
    for (i = 0; i < length; i++) {
      if (wide[i] == X2_KEYCAP_GLYPH_LEFT) {
        report_unclaimed(wide, length);
        break;
      }
    }
    return 0;
  }
  for (i = 0; i < cap.start; i++) {
    before += (unsigned)x2_glyph_loop_emits_quad(wide[i]);
  }
  for (i = cap.start; i < cap.end; i++) {
    inside += (unsigned)x2_glyph_loop_emits_quad(wide[i]);
  }
  if (!inside) {
    return 0;
  }
  g_string.armed = 1;
  g_string.suppress_from = before;
  g_string.suppress_to = before + inside;
  g_string.quads = before + inside;
  for (i = cap.end; i < length; i++) {
    g_string.quads += (unsigned)x2_glyph_loop_emits_quad(wide[i]);
  }
  g_string.dik = cap.dik;
  g_string.have_head = 0;
  g_string.have_words = 0;
  g_string.shift = 0.0f;
  g_claimed++;
  return inside;
}

/* The words' rectangle, corner order not assumed: the engine plane's y grows
   downward, but a caller that ever hands the corners the other way round
   would silently produce an inverted, untappable control. */
static void grow(const float *x0, const float *y0, const float *x1,
                 const float *y1) {
  const float left = *x0 < *x1 ? *x0 : *x1;
  const float right = *x0 < *x1 ? *x1 : *x0;
  const float top = *y0 < *y1 ? *y0 : *y1;
  const float bottom = *y0 < *y1 ? *y1 : *y0;
  if (!g_string.have_words) {
    g_string.have_words = 1;
    g_string.words.left = left;
    g_string.words.top = top;
    g_string.words.right = right;
    g_string.words.bottom = bottom;
    return;
  }
  if (left < g_string.words.left) {
    g_string.words.left = left;
  }
  if (right > g_string.words.right) {
    g_string.words.right = right;
  }
  if (top < g_string.words.top) {
    g_string.words.top = top;
  }
  if (bottom > g_string.words.bottom) {
    g_string.words.bottom = bottom;
  }
}

int x2_prompt_touch_glyph(unsigned emit_index, float *x0, float *y0, float *x1,
                          float *y1) {
  if (!g_string.armed || !x0 || !y0 || !x1 || !y1) {
    return 0;
  }
  if (emit_index < g_string.suppress_from) {
    return 1;
  }
  if (emit_index < g_string.suppress_to) {
    if (!g_string.have_head) {
      g_string.have_head = 1;
      g_string.head_x = *x0;
    }
    /* Zero area, the same way a native glyph's stock rectangle is retired:
       the emitter still runs, so the engine's vertex and batch semantics are
       untouched and only the pixels are gone. */
    *x1 = *x0;
    *y1 = *y0;
    return 1;
  }
  if (!g_string.have_words) {
    /* The words move to where the prompt began, so the key leaves no gap. */
    g_string.shift = g_string.have_head ? *x0 - g_string.head_x : 0.0f;
  }
  *x0 -= g_string.shift;
  *x1 -= g_string.shift;
  grow(x0, y0, x1, y1);
  return 1;
}

void x2_prompt_touch_end(void) {
  if (!g_string.armed) {
    return;
  }
  g_string.armed = 0;
  if (!g_string.have_words) {
    return;
  }
  {
    /* THE SAME PROMPT, LAID OUT AGAIN. A frame lays the footer out whether or
       not it will submit it, several times over for each draw that does --
       measured at 754 of 796 retained prompts in one run -- so a second copy
       is not a second control. Keeping the newer measurement in place is the
       whole of it; queueing it instead pushed the prompt that was waiting for
       its draw out of a four-deep queue. */
    unsigned i;
    for (i = 0; i < g_pending_count; i++) {
      if (g_pending[i].dik == g_string.dik &&
          g_pending[i].quads == g_string.quads) {
        g_pending[i].engine = g_string.words;
        g_relaid++;
        return;
      }
    }
  }
  if (g_pending_count == MAX_PENDING) {
    /* The OLDEST goes, not this one. A retained prompt whose draw never came
       -- an element laid out and then culled, a screen changed between the
       layout and the draw -- would otherwise sit in the queue for the rest of
       the run and stop every later prompt from being retained at all, which
       is how a footer stopped republishing and its controls timed out under
       the player's finger. */
    unsigned i;
    g_evicted++;
    g_pending_count--;
    for (i = 0; i < g_pending_count; i++) {
      g_pending[i] = g_pending[i + 1u];
    }
  }
  g_pending[g_pending_count].engine = g_string.words;
  g_pending[g_pending_count].dik = g_string.dik;
  g_pending[g_pending_count].quads = g_string.quads;
  g_pending_count++;
}

/* One corner of the stock (x, 0, y) text plane through a row-vector MVP. */
static int project_point(const float m[16], float x, float z, float *ndc_x,
                         float *ndc_y) {
  const float cx = x * m[0] + z * m[8] + m[12];
  const float cy = x * m[1] + z * m[9] + m[13];
  const float cw = x * m[3] + z * m[11] + m[15];
  if (!isfinite(cx) || !isfinite(cy) || !isfinite(cw) || cw <= 0.0f) {
    return 0;
  }
  *ndc_x = cx / cw;
  *ndc_y = cy / cw;
  return 1;
}

int x2_prompt_touch_project(const float mvp[16], X2AspectRect frame,
                            X2Rect engine, X2Rect *out) {
  float x0, y0, x1, y1;
  float left, top, right, bottom;

  if (!mvp || !out || !frame.width || !frame.height) {
    return 0;
  }
  if (!project_point(mvp, engine.left, engine.top, &x0, &y0) ||
      !project_point(mvp, engine.right, engine.bottom, &x1, &y1)) {
    return 0;
  }
  left = (float)frame.x + (x0 + 1.0f) * 0.5f * (float)frame.width;
  right = (float)frame.x + (x1 + 1.0f) * 0.5f * (float)frame.width;
  top = (float)frame.y + (1.0f - y0) * 0.5f * (float)frame.height;
  bottom = (float)frame.y + (1.0f - y1) * 0.5f * (float)frame.height;
  out->left = left < right ? left : right;
  out->right = left < right ? right : left;
  out->top = top < bottom ? top : bottom;
  out->bottom = top < bottom ? bottom : top;
  return 1;
}

/* Deduplicated by WHERE, not by count: a footer republished a hundred times a
   second drowns a sampled trace, and the question a reader brings here is
   which distinct rectangles this owner produced. */
static void trace_publication(const struct Pending *pending, X2Rect pixels,
                              double now) {
  static uint32_t seen[32];
  static unsigned n_seen;
  const uint32_t hash = (uint32_t)(pixels.left * 8.0f) * 2654435761u +
                        (uint32_t)(pixels.top * 8.0f) * 40503u + pending->dik;
  unsigned k;

  for (k = 0; k < n_seen && seen[k] != hash; k++) {
  }
  if (k < n_seen || n_seen == 32u) {
    return;
  }
  seen[n_seen++] = hash;
  x2_log_info("PROMPT TOUCH: publication %lu: DIK 0x%02x engine %g,%g..%g,%g "
              "-> %g,%g %gx%g (guest %.3fs)\n",
              g_published, pending->dik, (double)pending->engine.left,
              (double)pending->engine.top, (double)pending->engine.right,
              (double)pending->engine.bottom, (double)pixels.left,
              (double)pixels.top, (double)(pixels.right - pixels.left),
              (double)(pixels.bottom - pixels.top), now);
}

/* The oldest pending prompt, against the transform of the draw that is
   finalizing now. */
static void publish_at(unsigned at, const float mvp[16], X2AspectRect frame,
                       double now) {
  X2Rect pixels;
  unsigned i;

  if (x2_prompt_touch_project(mvp, frame, g_pending[at].engine, &pixels)) {
    x2_touch_prompt_publish(pixels, g_pending[at].dik, now);
    trace_publication(&g_pending[at], pixels, now);
    if (g_published && now - g_last_publish > g_longest_gap) {
      g_longest_gap = now - g_last_publish;
    }
    g_last_publish = now;
    g_published++;
  } else {
    g_offscreen++;
  }
  g_pending_count--;
  for (i = at; i < g_pending_count; i++) {
    g_pending[i] = g_pending[i + 1u];
  }
}

/* The glyph count a draw of `primitives` submits, or 0 if it is not a run of
   whole glyphs. Six vertices a glyph, two fewer primitives than vertices. */
static unsigned glyphs_in_draw(uint32_t primitives) {
  const uint32_t vertices = primitives + 2u;
  return vertices % 6u ? 0u : vertices / 6u;
}

void x2_prompt_touch_publish(X2PromptTransform transform, void *owner,
                             uint32_t primitives) {
  X2LayoutViewport viewport;
  X2AspectRect frame;
  const X2Settings *settings;
  const unsigned glyphs = glyphs_in_draw(primitives);
  float mvp[16];
  unsigned at;

  if (!g_pending_count || !glyphs || !transform) {
    return;
  }
  for (at = 0; at < g_pending_count && g_pending[at].quads != glyphs; at++) {
  }
  if (at == g_pending_count) {
    g_unclaimed_draws++;
    return;
  }
  if (!transform(owner, mvp)) {
    g_no_transform++;
    return;
  }
  if (!x2_touch_runtime_viewport(&viewport)) {
    g_no_viewport++;
    return;
  }
  settings = x2_settings_store();
  if (!x2_aspect_fit((uint32_t)viewport.width, (uint32_t)viewport.height,
                     settings->width, settings->height, &frame)) {
    g_no_viewport++;
    return;
  }
  publish_at(at, mvp, frame, guest_clock_now_s());
}

void x2_prompt_touch_report(void) {
  x2_log_info("  Touch action prompts: %lu drawn string(s) offered, %lu with "
              "touch play inactive, %lu carrying no retained keyboard "
              "label\n",
              g_seen, g_inactive, g_unmatched);
  x2_log_info("  Touch action prompts: %lu drawn prompt(s) rewritten, %lu "
              "published as controls; %lu had no finalized transform at "
              "their own draw, %lu had no viewport or letterbox, %lu "
              "projected off the eye, %lu were evicted by a later prompt "
              "before any draw submitted them\n",
              g_claimed, g_published, g_no_transform, g_no_viewport,
              g_offscreen, g_evicted);
  x2_log_info("        %lu re-measurement(s) of a prompt still waiting for "
              "its draw replaced the one retained\n",
              g_relaid);
  x2_log_info("        %lu finalized draw(s) of whole glyphs matched no "
              "retained prompt -- the ordinary case, every other line of text "
              "on the screen\n",
              g_unclaimed_draws);
  x2_log_info("        longest gap between two publications: %.3fs of guest "
              "clock, against the %.2fs a published control stays "
              "pressable\n",
              g_longest_gap, (double)X2_TOUCH_PROMPT_LIFETIME);
  if (!g_claimed) {
    x2_log_info("        NO prompt was rewritten in this run -- either touch "
                "play was not active, or no screen drew a composed keyboard "
                "prompt. This says nothing about whether one would be "
                "pressable.\n");
  }
}
