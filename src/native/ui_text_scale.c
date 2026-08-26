/*
 * UI text size, owned by the port instead of by the font asset.
 *
 * ## The mechanism, measured
 *
 * Glyph metrics are PIXELS, and they do not depend on the output resolution.
 * The game's one concession to bigger screens is a two-tier asset switch
 * (FUN_005980f0 loads `ui/fonts/fonts_pc.xmlb` at or below 800x600 and
 * `fonts_HD.xmlb` above it), and that tier step is a single fixed factor --
 * 1.36x, measured over 52 glyphs of this install's own fonts by
 * tools/font_tier_ratio.py. Panel art, by contrast, is laid out in a space
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
 * overrides it for one run. 0 means AUTO: hold the share of the screen the
 * text has at 800x600, which is height/600 against the PC tier, divided by
 * the loaded tier's own step.
 */
#include "ui_text_scale.h"

#include "font_tier_ratio.h"   /* GENERATED, see tools/font_tier_ratio.py */
#include "prompt_glyph_metrics.h"
#include "prompt_glyphs.h"
#include "settings_store.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* XMen2.exe's own font record, from FUN_00596af0. */
#define FONT_STRIDE     0x1c18u
#define FONT_POINTSIZE  0x04u    /* short, FONT_TABLE pointsize */
#define FONT_HEIGHT     0x06u    /* short, height */
#define FONT_ASCENDER   0x08u    /* short */
#define FONT_DESCENDER  0x0au    /* short */
#define GLYPH_FIRST     0x18u
#define GLYPH_STRIDE    0x1cu
#define GLYPH_COUNT     256u
#define GL_WIDTH        0x00u    /* short */
#define GL_HEIGHT       0x02u    /* short */
#define GL_ADVANCE      0x04u    /* short */
#define GL_OFFSET       0x06u    /* short */
#define GL_BASELINE     0x08u    /* dword */
/* +0x0c..+0x1b are s, t, s2, t2 -- atlas UVs, which must NOT be scaled. */

#define LINKED_FONT_LOADER 0x00596af0u
#define LINKED_FONT_TIER   0x005f5fd0u

void fn_XMen2_00596af0(CPU *C);      /* the recompiled bodies, kept alive */
void fn_XMen2_005f5fd0(CPU *C);

static unsigned long g_fonts, g_glyphs, g_zero;
static unsigned long g_tier_asked, g_tier_forced;
static float g_applied = -1.0f;

float x2_ui_text_scale(void)
{
    const X2Settings *settings = x2_settings_store();
    const char *forced = getenv("X2_TEXT_SCALE");
    float configured = settings->text_scale;
    float scale;

    if (forced && *forced) configured = (float)atof(forced);
    if (configured > 0.0f) return configured;

    /* AUTO: the 800x600 proportion. The HD tier is loaded at EVERY
       resolution now (see the tier override below), so its own step comes out
       unconditionally -- including below 601, where holding the retail look
       means scaling the HD metrics DOWN to what the PC tier would have been.
       No clamp at 1.0: a clamp there would silently make 640x480 bigger than
       the game draws it. */
    scale = (float)settings->height / 600.0f / x2_font_tier_ratio();
    return scale;
}

static const char *scale_source(void)
{
    if (getenv("X2_TEXT_SCALE")) return "X2_TEXT_SCALE";
    if (x2_settings_store()->text_scale > 0.0f) return "ui.text_scale";
    return "auto, holding the 800x600 share";
}

/* Scale one 16-bit metric, keeping 0 at 0: a zero width is a glyph the font
   does not draw, and rounding it up would give every unused codepoint a
   one-pixel box. */
static void scale_i16(uint32_t at, float k)
{
    int16_t v = (int16_t)RD16(at);
    if (!v) return;
    WR16(at, (uint16_t)(int16_t)lrintf((float)v * k));
}

static void scale_i32(uint32_t at, float k)
{
    int32_t v = (int32_t)RD32(at);
    if (!v) return;
    WR32(at, (uint32_t)(int32_t)lrintf((float)v * k));
}

static void scale_font_record(uint32_t font, float k)
{
    unsigned i, drawn = 0;

    scale_i16(font + FONT_POINTSIZE, k);
    scale_i16(font + FONT_HEIGHT, k);
    scale_i16(font + FONT_ASCENDER, k);
    scale_i16(font + FONT_DESCENDER, k);
    for (i = 0; i < GLYPH_COUNT; i++) {
        uint32_t g = font + GLYPH_FIRST + i * GLYPH_STRIDE;
        if (RD16(g + GL_WIDTH) || RD16(g + GL_HEIGHT)) drawn++;
        scale_i16(g + GL_WIDTH, k);
        scale_i16(g + GL_HEIGHT, k);
        scale_i16(g + GL_ADVANCE, k);
        scale_i16(g + GL_OFFSET, k);
        scale_i32(g + GL_BASELINE, k);
    }
    g_fonts++;
    g_glyphs += drawn;
    /* A font that loaded with nothing drawable gets its own line: the silent
       version of this is a pass that "succeeded" on every font while nothing
       on screen moved. */
    if (!drawn) {
        g_zero++;
        fprintf(stderr, "UI TEXT: a font loaded with 0 drawing glyph(s) -- "
                        "scaling it changes nothing.\n");
    }
    if (g_fonts == 1)
        fprintf(stderr, "UI TEXT: scaling every font the game loads by %.3f "
                        "(%s, output %ux%u); first font %u drawing glyph(s).\n",
                k, scale_source(), x2_settings_store()->width,
                x2_settings_store()->height, drawn);
}

/*
 * FUN_00596af0(this = font table, const char *name, int index) fills
 * table[index] from ui/fonts/<name>.xmlb. Super-call first: the record has to
 * exist before it can be scaled, and a load that failed must stay failed.
 */
static void x2_override_font_loader(CPU *C)
{
    uint32_t table = C->ecx;
    uint32_t name = RD32(C->esp + 4u);
    uint32_t index = RD32(C->esp + 8u);
    float k = x2_ui_text_scale();

    fn_XMen2_00596af0(C);
    if (!table || !C->eax) return;                /* eax 0 == nothing loaded */
    if (k != 1.0f) {
        g_applied = k;
        scale_font_record(table + index * FONT_STRIDE, k);
    }
    /* The port's own codepoints get their metrics here too, at the same
       scale and the same moment -- AFTER the scaler, so they are published
       already-scaled rather than scaled twice. Unconditional on k, since the
       port's glyphs need metrics even when the text scale is 1.0. */
    if (x2_prompt_glyphs_enabled())
        x2_prompt_glyph_publish_metrics(table + index * FONT_STRIDE,
                                        k != 1.0f ? k : 1.0f);
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
static void x2_override_font_tier(CPU *C)
{
    fn_XMen2_005f5fd0(C);
    g_tier_asked++;
    if (!C->eax) g_tier_forced++;
    C->eax = 1u;
}

void x2_ui_text_scale_report(void)
{
    /* Every denominator: "0 fonts scaled" has three different causes -- the
       scale was 1, the loader never ran, or it ran and every font was empty
       -- and they must not print the same line. */
    fprintf(stderr, "UI TEXT: scale %.3f (%s); %lu font(s) scaled, %lu drawing "
                    "glyph(s), %lu font(s) that had none.\n",
            g_applied < 0.0f ? x2_ui_text_scale() : g_applied,
            scale_source(), g_fonts, g_glyphs, g_zero);
    fprintf(stderr, "UI TEXT: the HD font set was asked for %lu time(s); the "
                    "retail answer would have been the PC set %lu time(s).\n",
            g_tier_asked, g_tier_forced);
}

__attribute__((constructor))
static void x2_ui_text_scale_register_overrides(void)
{
    x86_register_override("XMen2.exe", LINKED_FONT_LOADER,
                          x2_override_font_loader);
    x86_register_override("XMen2.exe", LINKED_FONT_TIER,
                          x2_override_font_tier);
}
