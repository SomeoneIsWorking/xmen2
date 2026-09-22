/*
 * The rewrite that turns "Esc Back" into a control, from drawn string to
 * published rectangle.
 *
 * Two mistakes in the running game are what this measures. The key's quads
 * must collapse and the words must slide into the space they left, or the
 * player is offered a control around text that still names a key. And a
 * published prompt must be placed by the transform of the draw that submits
 * IT: a frame lays every prompt out before it draws any, so pairing them by
 * arrival drew "Back" on top of "Advanced Options", and pairing them one per
 * draw moved a dialog's second prompt onto another element's line. The draw's
 * own glyph count is what tells them apart, and that is checked here against
 * both a matching and a non-matching draw.
 */
#include "../src/config/settings_store.h"
#include "guest_memory.h"
#include "pad_glyph_codes.h"
#include "prompt_action_labels.h"
#include "prompt_glyph_draw.h"
#include "prompt_touch_buttons.h"
#include "x86rt.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define GUEST_PAGE 0x70000000u

static int g_checks;
static int g_failed;

#define CHECK(what, cond)                                                      \
  do {                                                                         \
    g_checks++;                                                                \
    if (!(cond)) {                                                             \
      g_failed++;                                                              \
      printf("    FAIL %s: %s\n", (what), #cond);                              \
    }                                                                          \
  } while (0)

/* ---- the boundary this owner sits on, answered by the test -------------- */

static int g_touch_active = 1;
static X2LayoutViewport g_viewport = {1280.0f, 720.0f, 0, 0, 0, 0};

int x2_touch_runtime_active(void) { return g_touch_active; }

int x2_touch_runtime_viewport(X2LayoutViewport *out) {
  *out = g_viewport;
  return 1;
}

static X2Rect g_published_rect;
static unsigned g_published_dik;
static unsigned g_publications;

void x2_touch_prompt_publish(X2Rect target, unsigned dik, double now) {
  (void)now;
  g_published_rect = target;
  g_published_dik = dik;
  g_publications++;
}

double guest_clock_now_s(void) { return 1.0; }

/* The glyph loop's own overrides come in with prompt_glyph_draw.c, which this
   test links for one thing: the cursor model that says which characters emit
   a quad. One model, not a copy of it here. Its guest boundary is never
   reached from this test, so it aborts rather than pretending. */
void x86_guest_body(CPU *C, const char *module, uint32_t linked_ep) {
  (void)C;
  (void)module;
  (void)linked_ep;
  printf("    FAIL the guest body was entered from a test that never drives "
         "an override (%s)\n",
         module);
  abort();
}

/* The stock 800x600 UI the port letterboxes; the settings owner is not part
   of what is under test here. */
static X2Settings g_settings;

X2Settings *x2_settings_store(void) {
  g_settings.width = 800;
  g_settings.height = 600;
  return &g_settings;
}

/* ---- a drawn prompt ------------------------------------------------------ */

static uint32_t g_next = GUEST_PAGE;

static uint32_t guest_wide(const uint16_t *codes, unsigned n) {
  const uint32_t at = g_next;
  unsigned i;
  for (i = 0; i < n; i++) {
    *(uint16_t *)guest_memory_pointer(at + i * 2u) = codes[i];
  }
  g_next += (n + 1u) * 2u;
  return at;
}

/* left, middle x n, rewind x n, name, right, then the action's words. */
static unsigned compose(uint16_t *out, unsigned at, const char *name,
                        const char *words) {
  const unsigned n = (unsigned)strlen(name);
  unsigned i;

  out[at++] = X2_KEYCAP_GLYPH_LEFT;
  for (i = 0; i < n; i++) {
    out[at++] = X2_KEYCAP_GLYPH_MIDDLE;
  }
  for (i = 0; i < n; i++) {
    out[at++] = X2_KEYCAP_GLYPH_REWIND;
  }
  for (i = 0; i < n; i++) {
    out[at++] = (uint16_t)name[i];
  }
  out[at++] = X2_KEYCAP_GLYPH_RIGHT;
  for (i = 0; words[i]; i++) {
    out[at++] = (uint16_t)words[i];
  }
  return at;
}

/* Walk a claimed string the way the glyph loop does: one call per emitting
   character, left to right, each glyph ten wide. Returns the rectangle the
   words ended up in. */
static void walk(unsigned emitting, X2Rect *words) {
  unsigned i;
  int have = 0;

  for (i = 0; i < emitting; i++) {
    float x0 = (float)i * 10.0f;
    float y0 = 0.0f;
    float x1 = x0 + 10.0f;
    float y1 = 8.0f;
    CHECK("every glyph of a claimed string is answered",
          x2_prompt_touch_glyph(i, &x0, &y0, &x1, &y1));
    if (x1 == x0) {
      continue; /* collapsed: part of the key */
    }
    if (!have) {
      have = 1;
      words->left = x0;
      words->top = y0;
      words->right = x1;
      words->bottom = y1;
    } else {
      words->right = x1 > words->right ? x1 : words->right;
    }
  }
}

/* The draw's transform, asked for only once a prompt has been matched to the
   draw -- so a test that never matches one must never be asked. */
static unsigned g_transform_asks;

static int transform_of_draw(void *owner, float mvp[16]) {
  memcpy(mvp, owner, sizeof(float) * 16u);
  g_transform_asks++;
  return 1;
}

int main(void) {
  uint16_t drawn[64];
  unsigned length, emitting, i;
  X2Rect words;
  X2AspectRect frame = {0, 0, 800, 600};
  X2Rect out;
  /* Row-vector MVP for the stock (x, 0, y) text plane: a half-scale identity
     that puts engine (0,0) at the middle of the frame. */
  static const float kMvp[16] = {0.01f, 0,     0, 0, 0, 0, 0, 0,
                                 0,     0.01f, 0, 0, 0, 0, 0, 1.0f};

  if (guest_memory_init() != 0 ||
      guest_memory_map_fixed(GUEST_PAGE, 0x1000, PROT_READ | PROT_WRITE) != 0) {
    printf("test_prompt_touch_buttons: no guest page\n");
    return 1;
  }

  x2_prompt_action_labels_reset();
  x2_prompt_action_label_note((const uint8_t *)"Esc", 3u, 0x01u);

  /* Not a prompt: no key was retained under that name. */
  length = compose(drawn, 0, "Tab", " Next");
  CHECK("an unknown key is not claimed",
        !x2_prompt_touch_begin(guest_wide(drawn, length), length));

  /* Touch play off: the same string is left exactly as retail drew it. */
  g_touch_active = 0;
  length = compose(drawn, 0, "Esc", " Back");
  CHECK("nothing is rewritten while touch play is off",
        !x2_prompt_touch_begin(guest_wide(drawn, length), length));
  g_touch_active = 1;

  /* The footer's shape: the authored token marker, then the cap, then the
     words. The marker emits no quad. */
  drawn[0] = 0x3edu;
  length = compose(drawn, 1u, "Esc", " Back");
  emitting = 0;
  for (i = 0; i < length; i++) {
    emitting += (unsigned)x2_glyph_loop_emits_quad(drawn[i]);
  }
  CHECK("the key's glyphs are the ones taken off",
        x2_prompt_touch_begin(guest_wide(drawn, length), length) == 11u);

  walk(emitting, &words);
  /* The key occupies emit indices 0..10; "Back" begins at 12 (the space at 11
     emits a quad of its own) and would be drawn at x=110. It must arrive
     where the key began instead. */
  CHECK("the words slide into the space the key left", words.left == 0.0f);
  CHECK("and keep their width", words.right - words.left == 40.0f);

  x2_prompt_touch_end();

  /* A draw of some other text on the screen must not place this prompt: 5
     glyphs is 30 vertices, declared as 28. */
  x2_prompt_touch_publish(transform_of_draw, (void *)kMvp, 28u);
  CHECK("another element's draw publishes nothing", g_publications == 0u);
  CHECK("and is never asked for its transform", g_transform_asks == 0u);

  /* The draw that submits it: 15 glyphs (the marker and the space draw
     nothing), 90 vertices, declared as 88. */
  x2_prompt_touch_publish(transform_of_draw, (void *)kMvp, 88u);
  CHECK("the draw of this prompt publishes it", g_publications == 1u);
  CHECK("with the key the prompt named", g_published_dik == 0x01u);
  /* The 800x600 UI letterboxed into a 1280x720 window is a 960x720 box at
     x=160, and engine x=0 is the middle of it. */
  CHECK("placed in the letterbox, not the whole window",
        g_published_rect.left == 640.0f);
  CHECK("inside the window it was published for",
        g_published_rect.right <= g_viewport.width);
  CHECK("and only once",
        (x2_prompt_touch_publish(transform_of_draw, (void *)kMvp, 88u),
         g_publications) == 1u);

  /* The projection itself. */
  out.left = out.top = out.right = out.bottom = -1.0f;
  CHECK("an engine rectangle projects into the frame",
        x2_prompt_touch_project(kMvp, frame, words, &out));
  CHECK("engine x=0 lands at the middle of the frame", out.left == 400.0f);
  CHECK("y grows downward in output pixels", out.bottom > out.top);
  {
    static const float kBehind[16] = {1, 0, 0, 0, 0, 0, 0, 0,
                                      0, 1, 0, 0, 0, 0, 0, -1.0f};
    X2Rect keep = {1, 2, 3, 4};
    CHECK("a rectangle behind the eye is refused",
          !x2_prompt_touch_project(kBehind, frame, words, &keep));
    CHECK("and the output is left alone", keep.left == 1.0f);
  }
  {
    X2AspectRect empty = {0, 0, 0, 0};
    CHECK("no frame is no placement",
          !x2_prompt_touch_project(kMvp, empty, words, &out));
  }

  printf("test_prompt_touch_buttons: %d check(s), %d failed\n", g_checks,
         g_failed);
  return g_failed ? 1 : 0;
}
