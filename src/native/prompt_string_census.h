/* The diagnostic census of strings reaching the retail glyph loop -- see
 * prompt_string_census.c. */
#ifndef X2_PROMPT_STRING_CENSUS_H
#define X2_PROMPT_STRING_CENSUS_H

#include <stdint.h>

#define X2_PROMPT_WALK_MAX 512u /* widest line buffer the game builds */

/* Is c in the WHOLE published prompt run, keycap edges included? */
int x2_prompt_codepoint(uint16_t c);

/* True if the NUL-terminated guest wide string carries one of the port's
 * private prompt codepoints within the first max elements. */
int x2_string_has_prompt_glyph(uint32_t s_guest, unsigned max);

/* A hash of the string's content (never 0), and its length in wchars. */
uint32_t x2_prompt_string_hash(uint32_t s_guest, unsigned *length_out);

/* Count one string the glyph loop was entered with (0 when it had none) and
 * dump its raw codepoints the first time this content is seen. */
void x2_prompt_string_census(uint32_t s_guest);

/* The census lines, with denominators; says why no prompt arrived when none
 * did. */
void x2_prompt_string_census_report(void);

#endif /* X2_PROMPT_STRING_CENSUS_H */
