#include "keycap_labels.h"

#include "keycap_run.h"
#include "prompt_glyph_atlas.h"
#include "x2_log.h"

#include "../ui/ui_resources.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <string.h>

#define KEY_FONT_FILE "NotoSans-Bold-keys.ttf"
#define MAX_LABELS 64u
/* One sheet row is one cap: the label keeps the cap's full height. */
#define ROW_H (X2_PROMPT_SOURCE_CELL_DESIGN * X2_PROMPT_SUPERSAMPLE)
#define GAP 2u

struct Label {
  uint16_t name[X2_KEYCAP_NAME_MAX];
  unsigned length;
  struct x2_keycap_art art;
};

static FT_Library g_library;
static FT_Face g_face;
static int g_font_state; /* 0 unopened, 1 open, -1 refused */
static uint8_t g_sheet[X2_KEYCAP_LABEL_SHEET_W * X2_KEYCAP_LABEL_SHEET_H * 4u];
static uint64_t g_generation;
static struct Label g_labels[MAX_LABELS];
static unsigned g_count, g_pen_x, g_pen_y;
static unsigned long g_lettered, g_refused_full, g_refused_font;

static int open_font(void) {
  const char *path;
  if (g_font_state) {
    return g_font_state > 0;
  }
  g_font_state = -1;
  path = x2_ui_resource_path(KEY_FONT_FILE);
  if (FT_Init_FreeType(&g_library)) {
    x2_log_error("KEY LABELS: FreeType could not start; keyboard prompts "
                 "stay the game's [NAME] text.\n");
    return 0;
  }
  if (FT_New_Face(g_library, path, 0, &g_face) ||
      FT_Set_Pixel_Sizes(g_face, 0,
                         X2_KEYCAP_LABEL_SIZE * ROW_H / X2_KEYCAP_CAP_UNITS)) {
    x2_log_error("KEY LABELS: the shared key typeface %s could not be "
                 "opened; keyboard prompts stay the game's [NAME] text.\n",
                 path);
    FT_Done_FreeType(g_library);
    g_library = 0;
    return 0;
  }
  g_font_state = 1;
  return 1;
}

static const struct Label *find(const uint16_t *name, unsigned length) {
  unsigned i;
  for (i = 0; i < g_count; i++) {
    if (g_labels[i].length == length &&
        memcmp(g_labels[i].name, name, length * sizeof *name) == 0) {
      return &g_labels[i];
    }
  }
  return 0;
}

/* The pen advance of `name` in pixels, kerning included. */
static long measure(const uint16_t *name, unsigned length) {
  FT_UInt previous = 0;
  long pen = 0;
  unsigned i;
  for (i = 0; i < length; i++) {
    const FT_UInt glyph = FT_Get_Char_Index(g_face, name[i]);
    FT_Vector kern;
    if (previous && glyph &&
        !FT_Get_Kerning(g_face, previous, glyph, FT_KERNING_DEFAULT, &kern)) {
      pen += kern.x >> 6;
    }
    if (FT_Load_Glyph(g_face, glyph, FT_LOAD_DEFAULT)) {
      return -1;
    }
    pen += g_face->glyph->advance.x >> 6;
    previous = glyph;
  }
  return pen;
}

/* Composite one rendered glyph's coverage into the sheet in the set's ink. */
static void blit(const FT_Bitmap *bitmap, long x, long y) {
  unsigned row, col;
  for (row = 0; row < bitmap->rows; row++) {
    for (col = 0; col < bitmap->width; col++) {
      const long px = x + (long)col, py = y + (long)row;
      const uint8_t coverage = bitmap->buffer[row * bitmap->pitch + col];
      uint8_t *out;
      if (px < 0 || py < 0 || px >= (long)X2_KEYCAP_LABEL_SHEET_W ||
          py >= (long)X2_KEYCAP_LABEL_SHEET_H || !coverage) {
        continue;
      }
      out = &g_sheet[((size_t)py * X2_KEYCAP_LABEL_SHEET_W + (size_t)px) * 4u];
      out[0] = (uint8_t)(X2_KEYCAP_INK >> 16);
      out[1] = (uint8_t)(X2_KEYCAP_INK >> 8);
      out[2] = (uint8_t)X2_KEYCAP_INK;
      if (coverage > out[3]) {
        out[3] = coverage;
      }
    }
  }
}

static int letter(const uint16_t *name, unsigned length, unsigned x0,
                  unsigned y0) {
  const long baseline =
      (long)y0 + X2_KEYCAP_LABEL_BASELINE * ROW_H / X2_KEYCAP_CAP_UNITS;
  FT_UInt previous = 0;
  long pen = (long)x0 + (long)GAP;
  unsigned i;
  for (i = 0; i < length; i++) {
    const FT_UInt glyph = FT_Get_Char_Index(g_face, name[i]);
    FT_Vector kern;
    if (previous && glyph &&
        !FT_Get_Kerning(g_face, previous, glyph, FT_KERNING_DEFAULT, &kern)) {
      pen += kern.x >> 6;
    }
    if (FT_Load_Glyph(g_face, glyph, FT_LOAD_RENDER)) {
      return 0;
    }
    blit(&g_face->glyph->bitmap, pen + g_face->glyph->bitmap_left,
         baseline - g_face->glyph->bitmap_top);
    pen += g_face->glyph->advance.x >> 6;
    previous = glyph;
  }
  return 1;
}

/* A cell `width` pixels wide, on the current row or the next; 0 when full. */
static int place(unsigned width, unsigned *x, unsigned *y) {
  if (width > X2_KEYCAP_LABEL_SHEET_W) {
    return 0;
  }
  if (g_pen_x + width > X2_KEYCAP_LABEL_SHEET_W) {
    g_pen_x = 0;
    g_pen_y += ROW_H + GAP;
  }
  if (g_pen_y + ROW_H > X2_KEYCAP_LABEL_SHEET_H) {
    return 0;
  }
  *x = g_pen_x;
  *y = g_pen_y;
  g_pen_x += width + GAP;
  return 1;
}

const struct x2_keycap_art *x2_keycap_label_art(const uint16_t *name,
                                                unsigned length) {
  const struct Label *known;
  struct Label *label;
  unsigned x, y, width;
  long advance;

  if (!name || !length || length > X2_KEYCAP_NAME_MAX) {
    return 0;
  }
  known = find(name, length);
  if (known) {
    return &known->art;
  }
  if (!open_font()) {
    g_refused_font++;
    return 0;
  }
  advance = measure(name, length);
  if (advance <= 0) {
    g_refused_font++;
    return 0;
  }
  width = (unsigned)advance + 2u * GAP;
  if (g_count == MAX_LABELS || !place(width, &x, &y)) {
    if (!g_refused_full++) {
      x2_log_error("KEY LABELS: the label sheet is full (%u labels); later "
                   "binding names stay the game's [NAME] text.\n",
                   g_count);
    }
    return 0;
  }
  if (!letter(name, length, x, y)) {
    g_refused_font++;
    return 0;
  }
  label = &g_labels[g_count++];
  memcpy(label->name, name, length * sizeof *name);
  label->length = length;
  label->art.u0 = (float)x / (float)X2_KEYCAP_LABEL_SHEET_W;
  label->art.u1 = (float)(x + width) / (float)X2_KEYCAP_LABEL_SHEET_W;
  label->art.v0 = 1.0f - (float)(y + ROW_H) / (float)X2_KEYCAP_LABEL_SHEET_H;
  label->art.v1 = 1.0f - (float)y / (float)X2_KEYCAP_LABEL_SHEET_H;
  label->art.design_w = (float)width / (float)X2_PROMPT_SUPERSAMPLE;
  label->art.sheet = X2_KEYCAP_SHEET_LABELS;
  g_generation++;
  g_lettered++;
  return &label->art;
}

const uint8_t *x2_keycap_label_sheet(uint64_t *generation) {
  if (generation) {
    *generation = g_generation;
  }
  return g_sheet;
}

void x2_keycap_labels_report(void) {
  x2_log_info("  Key labels: %lu binding name(s) lettered in the shared key "
              "typeface (%u cached); %lu refused because the typeface was "
              "unavailable, %lu because the sheet was full\n",
              g_lettered, g_count, g_refused_font, g_refused_full);
}
