#include "x2_log.h"
/*
 * UI text size, owned by the port instead of by the font asset.
 *
 * ## The mechanism, measured
 *
 * Glyph metrics are PIXELS, and they do not depend on the output resolution.
 * The game's one concession to bigger screens is a two-tier asset switch
 * (FUN_005980f0 loads `ui/fonts/fonts_pc.xmlb` at or below 800x600 and
 * `fonts_HD.xmlb` above it), and that tier step is a single fixed factor,
 * about 1.36x. Panel art, by contrast, is laid out in a space
 * that scales with the frame. So above 800x600 the text's share of the screen
 * falls off as 1/height: on the main menu "NEW GAME" has a cap height of 7px
 * at 800x600, 11px at 1024x768 and still 11px at 1536x864 -- 1.17% of the
 * frame, then 1.43%, then 0.79%. At 4K it is about a third of the size the
 * game was drawn for, which is what a 4K player sees as tiny text inside
 * correctly-scaled panels.
 *
 * ## Where this acts, and how that was established
 *
 * NOT in the Alchemy engine. libIGGui has a whole igBitmapFont -- metrics
 * list, rasterize, getCharWidth -- and the game never enters ONE of those
 * functions: `/reached` reports NEVER for 0x1000e8c0, 0x1000e8f0, 0x1000ea30,
 * 0x1000ed20 and 0x10015320 in libIGGui across a full boot in which
 * libIGGui's own arkRegister ran twice. Two overrides were written against
 * that class before the instrument existed; both fired zero times and looked
 * exactly like a broken override mechanism.
 *
 * XMen2.exe parses the font XMLB itself, in FUN_00596af0, into its own table:
 * a 0x1c18-byte record per font (0x18 of header, then 256 glyph entries of
 * 0x1c). Every field below is named by the attribute string the loader pushes
 * before reading it -- "width" 0x00681c60, "height" 0x00681cc8, "horizAdvance"
 * 0x0069d644, "horizOffset" 0x0069d638, "baseline" 0x0069d62c -- so these are
 * the asset's own names, not a guess at what an offset means.
 *
 * Scaling the record once the loader has filled it keeps the whole game
 * self-consistent: every width, advance, offset and baseline the layout reads
 * is the one the glyphs are drawn at. The UVs are untouched, so a scale above
 * 1.0 magnifies the atlas texels -- exactly what the game's own HD tier does
 * between 800x600 and 1024x768.
 *
 * The alternative -- publishing rewritten .xmlb copies of the game's fonts
 * through the asset path -- was built and rejected: it edits copies of
 * shipped assets to express a port setting, and it has to be redone for every
 * font tier and every localisation.
 *
 * ## The scale
 *
 * `ui.text_scale` in x2native.conf, written by Port Settings; X2_TEXT_SCALE
 * overrides it for one run. 0 means AUTO: hold the size the retail game draws
 * at 1080p, at every resolution.
 *
 * 1080p is the reference because it is a size the game itself chose, not one
 * the port invented. Above 600 the retail HD tier is what draws, its metrics
 * are fixed pixels, and the port loads that same tier at every resolution
 * (see the tier override below) -- so scale 1.0 at 1080p IS retail 1080p,
 * glyph for glyph, and `height / 1080` holds that apparent size everywhere
 * else. At 4K the text is then twice the pixels rather than the same pixels
 * in twice the frame, which is the whole complaint this file exists to
 * answer.
 *
 * The 800x600 share was the previous reference and it drew about a third
 * larger (600/1080 against the tier's own 1.36x step): retail at 800x600 uses
 * the coarser PC tier, so holding ITS proportion means holding the biggest
 * thing retail ever did rather than the thing it does at the resolution
 * people play at.
 */
#include "ui_text_scale.h"

#include "prompt_glyph_metrics.h"
#include "prompt_glyphs.h"
#include "settings_store.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "guest_body.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lucent/cvar_c.h>

/* XMen2.exe's own font record, from FUN_00596af0. */
#define FONT_STRIDE 0x1c18u
#define FONT_POINTSIZE 0x04u /* short, FONT_TABLE pointsize */
#define FONT_HEIGHT 0x06u    /* short, height */
#define FONT_ASCENDER 0x08u  /* short */
#define FONT_DESCENDER 0x0au /* short */
#define GLYPH_FIRST 0x18u
#define GLYPH_STRIDE 0x1cu
#define GLYPH_COUNT 256u
#define GL_WIDTH 0x00u    /* short */
#define GL_HEIGHT 0x02u   /* short */
#define GL_ADVANCE 0x04u  /* short */
#define GL_OFFSET 0x06u   /* short */
#define GL_BASELINE 0x08u /* dword */
/* +0x0c..+0x1b are s, t, s2, t2 -- atlas UVs, which must NOT be scaled. */

#define X2_RETAIL_1080P_HEIGHT 1080u

#define LINKED_FONT_LOADER 0x00596af0u
#define LINKED_FONT_TIER 0x005f5fd0u

static unsigned long g_fonts, g_glyphs, g_zero;
static unsigned long g_tier_asked, g_tier_forced;
static float g_applied = -1.0f;

/*
 * Every font record the loader filled, holding the metrics IT wrote.
 *
 * The AUTO scale is derived from the output height, and the output height
 * changes while the game runs: Port Settings applies a resolution live. A
 * record scaled once at boot therefore keeps the boot size for the rest of the
 * run -- start at 4K, switch to 800x600, and the text stays 4K-sized.
 *
 * The re-apply below re-derives every metric from what the loader wrote rather
 * than multiplying the already-scaled record by the ratio between two scales.
 * Each metric is a 16-bit pixel count, so a ratio pass would compound its own
 * rounding at every switch; from the origin, each scale is exact whatever came
 * before it.
 */
#define TRACKED_FONTS 64u
#define HEADER_METRICS 4u  /* pointsize, height, ascender, descender */
#define GLYPH_METRICS 4u   /* width, height, advance, offset */

typedef struct {
  uint32_t at;
  int16_t header[HEADER_METRICS];
  int16_t glyph[GLYPH_COUNT][GLYPH_METRICS];
  int32_t baseline[GLYPH_COUNT];
} FontOriginal;

static FontOriginal g_tracked[TRACKED_FONTS];
static FontOriginal g_overflow;
static unsigned g_tracked_count;
static unsigned long g_untracked;

float x2_ui_text_scale(void) {
  const X2Settings *settings = x2_settings_store();
  const char *forced = lucent_cvar_text("text_scale");
  float configured = settings->text_scale;
  float scale;

  if (forced && *forced)
    configured = (float)atof(forced);
  if (configured > 0.0f)
    return configured;

  /* AUTO: hold what retail draws at 1080p. The port loads the HD tier at
     every resolution, and retail's own HD metrics are what it draws above
     600, so 1.0 here is retail 1080p exactly and everything else is that
     size held. No clamp: below 1080 the text SHOULD get smaller in pixels,
     because the frame did. */
  scale = (float)settings->height / (float)X2_RETAIL_1080P_HEIGHT;
  return scale;
}

static const char *scale_source(void) {
  if (lucent_cvar_text("text_scale")[0])
    return "X2_TEXT_SCALE";
  if (x2_settings_store()->text_scale > 0.0f)
    return "ui.text_scale";
  return "auto, holding the retail 1080p size";
}

/* Scale one metric, keeping 0 at 0: a zero width is a glyph the font does not
   draw, and rounding it up would give every unused codepoint a one-pixel box. */
static int16_t scaled_i16(int16_t v, float k) {
  return v ? (int16_t)lrintf((float)v * k) : (int16_t)0;
}

static int32_t scaled_i32(int32_t v, float k) {
  return v ? (int32_t)lrintf((float)v * k) : 0;
}

/*
 * Remember one freshly-loaded record's own metrics.
 *
 * A repeat of an address is a reload of that table slot, so the snapshot is
 * replaced: the record on the other side of a reload is a different font, and
 * a stale origin would rescale it into the previous font's proportions.
 */
static FontOriginal *track_font(uint32_t font) {
  FontOriginal *slot = NULL;
  unsigned i;

  for (i = 0; i < g_tracked_count && !slot; i++)
    if (g_tracked[i].at == font)
      slot = &g_tracked[i];
  if (!slot && g_tracked_count < TRACKED_FONTS)
    slot = &g_tracked[g_tracked_count++];
  if (!slot) {
    /* Still scaled correctly for the resolution it loaded at; it just cannot
       follow a later change, and the report says how many are in that state. */
    g_untracked++;
    slot = &g_overflow;
  }
  slot->at = font;
  for (i = 0; i < HEADER_METRICS; i++)
    slot->header[i] = (int16_t)RD16(font + FONT_POINTSIZE + i * 2u);
  for (i = 0; i < GLYPH_COUNT; i++) {
    const uint32_t g = font + GLYPH_FIRST + i * GLYPH_STRIDE;
    unsigned f;
    for (f = 0; f < GLYPH_METRICS; f++)
      slot->glyph[i][f] = (int16_t)RD16(g + f * 2u);
    slot->baseline[i] = (int32_t)RD32(g + GL_BASELINE);
  }
  return slot;
}

/* Write one record at `k`, always from the loader's own values.
   The UVs at +0x0c..+0x1b are never touched. */
static unsigned write_font_record(const FontOriginal *slot, float k) {
  unsigned i, drawn = 0;

  for (i = 0; i < HEADER_METRICS; i++)
    WR16(slot->at + FONT_POINTSIZE + i * 2u,
         (uint16_t)scaled_i16(slot->header[i], k));
  for (i = 0; i < GLYPH_COUNT; i++) {
    const uint32_t g = slot->at + GLYPH_FIRST + i * GLYPH_STRIDE;
    unsigned f;
    if (slot->glyph[i][0] || slot->glyph[i][1])
      drawn++;
    for (f = 0; f < GLYPH_METRICS; f++)
      WR16(g + f * 2u, (uint16_t)scaled_i16(slot->glyph[i][f], k));
    WR32(g + GL_BASELINE, (uint32_t)scaled_i32(slot->baseline[i], k));
  }
  return drawn;
}

static void scale_font_record(const FontOriginal *slot, float k) {
  const unsigned drawn = write_font_record(slot, k);

  g_fonts++;
  g_glyphs += drawn;
  /* A font that loaded with nothing drawable gets its own line: the silent
     version of this is a pass that "succeeded" on every font while nothing
     on screen moved. */
  if (!drawn) {
    g_zero++;
    x2_log_error("UI TEXT: a font loaded with 0 drawing glyph(s) -- "
                 "scaling it changes nothing.\n");
  }
  if (g_fonts == 1)
    x2_log_error("UI TEXT: scaling every font the game loads by %.3f "
                 "(%s, output %ux%u); first font %u drawing glyph(s).\n",
                 k, scale_source(), x2_settings_store()->width,
                 x2_settings_store()->height, drawn);
}

/*
 * Re-derive every loaded font at the current output height.
 *
 * Called after a live resolution change has been accepted. Fonts already in
 * memory are not reloaded by that change, so without this the text keeps the
 * size the boot resolution asked for.
 */
int x2_ui_text_scale_reapply(void) {
  const float k = x2_ui_text_scale();
  unsigned i;

  if (!g_tracked_count || (g_applied >= 0.0f && k == g_applied))
    return 0;
  for (i = 0; i < g_tracked_count; i++) {
    write_font_record(&g_tracked[i], k);
    /* The port's own codepoints are written into the same record and were
       just overwritten from the origin, so they are republished here at the
       same scale and in the same order as at load. */
    if (x2_prompt_glyphs_enabled())
      x2_prompt_glyph_publish_metrics(g_tracked[i].at, k);
  }
  x2_log_error("UI TEXT: output is %ux%u now; %u loaded font(s) re-derived "
               "from %.3f to %.3f (%s).\n",
               x2_settings_store()->width, x2_settings_store()->height,
               g_tracked_count, (double)g_applied, (double)k, scale_source());
  g_applied = k;
  return (int)g_tracked_count;
}

/*
 * FUN_00596af0(this = font table, const char *name, int index) fills
 * table[index] from ui/fonts/<name>.xmlb. Super-call first: the record has to
 * exist before it can be scaled, and a load that failed must stay failed.
 */
static void x2_override_font_loader(CPU *C) {
  uint32_t table = C->reg[kX86pEcx];
  uint32_t name = RD32(C->reg[kX86pEsp] + 4u);
  uint32_t index = RD32(C->reg[kX86pEsp] + 8u);
  const FontOriginal *slot;
  float k;

  k = x2_ui_text_scale();

  x86_guest_body(C, "XMen2.exe", 0x00596af0u);
  if (!table || !C->reg[kX86pEax])
    return; /* eax 0 == nothing loaded */
  /* Snapshot even at scale 1.0: a resolution change later in the run has to
     be able to derive this record's metrics, and by then the loader's own
     values are the only correct origin. */
  slot = track_font(table + index * FONT_STRIDE);
  g_applied = k;
  if (k != 1.0f)
    scale_font_record(slot, k);
  /* The port's own codepoints get their metrics here too, at the same
     scale and the same moment -- AFTER the scaler, so they are published
     already-scaled rather than scaled twice. Unconditional on k, since the
     port's glyphs need metrics even when the text scale is 1.0. */
  if (x2_prompt_glyphs_enabled())
    x2_prompt_glyph_publish_metrics(slot->at, k != 1.0f ? k : 1.0f);
}

/*
 * The tier predicate: FUN_005f5fd0 answers "use the HD font set?" and the
 * retail answer is `widescreen || height >= 601` (the 0x259 compare on
 * field +0x24). That is the game's whole response to a bigger screen, and it
 * is one step -- above 601 the HD metrics never grow again, which is why 4K
 * text is tiny.
 *
 * So the port always takes HD. There is no reason to draw the coarser atlas
 * at any resolution once the port is scaling the metrics itself: HD has the
 * same glyphs at roughly twice the texel density, and the scale below brings
 * it back to the retail size wherever retail would have used the PC set.
 *
 * Super-call first so the retail answer is what gets counted -- a run has to
 * be able to say how often it actually CHANGED the tier, not just that it was
 * asked.
 */
static void x2_override_font_tier(CPU *C) {
  x86_guest_body(C, "XMen2.exe", 0x005f5fd0u);
  g_tier_asked++;
  if (!C->reg[kX86pEax])
    g_tier_forced++;
  C->reg[kX86pEax] = 1u;
}

void x2_ui_text_scale_report(void) {
  /* Every denominator: "0 fonts scaled" has three different causes -- the
     scale was 1, the loader never ran, or it ran and every font was empty
     -- and they must not print the same line. */
  x2_log_error("UI TEXT: scale %.3f (%s); %lu font(s) scaled, %lu drawing "
               "glyph(s), %lu font(s) that had none.\n",
               g_applied < 0.0f ? x2_ui_text_scale() : g_applied,
               scale_source(), g_fonts, g_glyphs, g_zero);
  x2_log_error("UI TEXT: the HD font set was asked for %lu time(s); the "
               "retail answer would have been the PC set %lu time(s).\n",
               g_tier_asked, g_tier_forced);
  if (g_untracked)
    x2_log_error("UI TEXT: %lu font(s) loaded past the %u the port can "
                 "re-derive; those keep the size of the resolution they "
                 "loaded at.\n",
                 g_untracked, TRACKED_FONTS);
}

__attribute__((constructor)) static void
x2_ui_text_scale_register_overrides(void) {
  x86_register_override("XMen2.exe", LINKED_FONT_LOADER,
                        x2_override_font_loader);
  x86_register_override("XMen2.exe", LINKED_FONT_TIER, x2_override_font_tier);
}
