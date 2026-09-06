#ifndef X2_FONT_TIER_H
#define X2_FONT_TIER_H

#include <stdint.h>

/*
 * THE STEP BETWEEN THE GAME'S TWO FONT TIERS, AS ARITHMETIC.
 *
 * XMen2.exe loads ui/fonts/fonts_pc.xmlb at or below 800x600 and fonts_HD.xmlb
 * above it (FUN_005980f0). Glyph metrics are pixels, so that switch is the
 * only size change the engine makes for a bigger screen, and the port's AUTO
 * text scale has to divide the step back out.
 *
 * The number is a MEASUREMENT and it differs per localisation, since each
 * ships its own fonts. It used to be measured at build time from the player's
 * install and emitted as a generated header, which made the binary
 * unbuildable without a game -- so no package could be built for anyone. The
 * measurement now happens at run time from the same fonts, through the
 * engine's own loader; this file is the arithmetic half, separated so it can
 * be tested without a game, a guest or a window.
 */

/* Glyph heights are compared over A-Z a-z: one glyph would hide a font whose
   capitals happen to match while everything else moved. */
#define X2_FONT_TIER_SAMPLES 52u

/* The MEDIAN of the per-glyph ratios -- a mean lets one outlier move it, and
   these are small integers where a 3px glyph against a 4px one is a 33%
   outlier. Sorts `ratios` in place. */
float x2_font_tier_median(float *ratios, unsigned count);

/*
 * The tier step from two tiers' glyph heights, indexed alike; a height of 0 is
 * a glyph that tier does not draw and is skipped on both sides.
 *
 * Returns 0 and leaves *samples at what it had when the step cannot be
 * measured: fewer than half the sample glyphs drawn in both tiers, or a step
 * outside [1, 4], which is not a plausible font step. The caller must not
 * substitute 1.0 for a refusal -- that silently turns AUTO into "hold
 * nothing" at every resolution, which is the failure the old generator
 * refused to emit.
 */
float x2_font_tier_ratio(const int16_t *pc_heights, const int16_t *hd_heights,
                         unsigned count, unsigned *samples);

#endif /* X2_FONT_TIER_H */
