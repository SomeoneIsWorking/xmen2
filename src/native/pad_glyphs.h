#ifndef X2_PAD_GLYPHS_H
#define X2_PAD_GLYPHS_H

#include <stdint.h>

/* XMen2.exe FUN_006281f0's physical-input code -> published font byte.
   Returns zero for codes whose original text name must remain in use. */
uint8_t pad_glyph_code(uint32_t code);

/*
 * The (device kind, physical code) FUN_006281f0 was last asked to name.
 *
 * A composed label is a picture of a binding, and by the time it is drawn the
 * binding is gone. The namer is the last point that still holds it, and the
 * label is built from its answer immediately afterwards, so this is the exact
 * pairing rather than a guess recovered from the letters. Returns 0 before
 * the first call. Kinds: 1 keyboard, 2 mouse, 3..0xc gamepad 0..9.
 */
int x2_pad_glyph_last_named(uint32_t *kind, uint32_t *code);
void pad_glyphs_report(void);

#endif /* X2_PAD_GLYPHS_H */
