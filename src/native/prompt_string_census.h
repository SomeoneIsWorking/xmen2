/* The diagnostic census of strings reaching the retail glyph loop -- see
 * prompt_string_census.cpp. */
#pragma once

#include <cstdint>

namespace x2::native {

/* Widest line buffer the game builds. */
inline constexpr unsigned kPromptWalkMax = 512u;

/* Is c in the WHOLE published prompt run, keycap edges included? */
int prompt_codepoint(std::uint16_t c);

/* True if the NUL-terminated guest wide string carries one of the port's
 * private prompt codepoints within the first max elements. */
int string_has_prompt_glyph(std::uint32_t s_guest, unsigned max);

/* A hash of the string's content (never 0), and its length in wchars. */
std::uint32_t prompt_string_hash(std::uint32_t s_guest, unsigned *length_out);

/* Count one string the glyph loop was entered with (0 when it had none) and
 * dump its raw codepoints the first time this content is seen. */
void prompt_string_census(std::uint32_t s_guest);

/* The census lines, with denominators; says why no prompt arrived when none
 * did. */
void prompt_string_census_report();

} // namespace x2::native
