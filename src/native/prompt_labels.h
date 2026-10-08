#pragma once

#include <cstddef>
#include <cstdint>

struct X86pCpu;

namespace x2::native {

enum class PromptLabelStyle { Unchanged, PadGlyph, Keycap };

/* Restyle one complete game label. Returns unchanged when output is too small.
 */
PromptLabelStyle prompt_label_rewrite(const std::uint8_t *input,
                                      std::uint8_t *output,
                                      std::size_t capacity);
/* The one guest buffer the composed label is published through, or 0 before
   the first composition. Exposed so the token resolver's probe can tell the
   port's own bytes from the game's. */
std::uint32_t prompt_label_buffer();
void prompt_labels_report();

/* Native replacement for XMen2.exe FUN_00619e30, the action label composer. */
void override_00619e30(struct X86pCpu *cpu);

} // namespace x2::native
