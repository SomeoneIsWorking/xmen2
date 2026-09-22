#ifndef X2_TOUCH_VISUALS_H
#define X2_TOUCH_VISUALS_H

#include "touch_controls.h"
#include "touch_prompt_buttons.h"
#include "touch_runtime.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <span>

namespace x2::input {

/*
 * WHAT THE OVERLAY IS ASKED TO DRAW, FROM WHAT IS CURRENTLY TRUE.
 *
 * Two owners produce drawable things and neither should know about the
 * other's: the layout's control zones in gameplay, and the rewritten retail
 * action prompts on the screens that draw no controls. This is the one place
 * they become a single list, so the document renders one kind of thing and
 * the runtime keeps no opinion about presentation.
 *
 * Returns how many visuals there are, which may exceed `capacity`; the caller
 * sizes its buffer from a first call with a null `out`, exactly as the C
 * entry point above it does.
 */
std::size_t overlay_visuals(std::span<const TouchControls::ZoneVisual> zones,
                            const std::set<std::uint32_t> &active,
                            ThumbStick::Deflection stick,
                            std::span<const PromptButton> prompts,
                            X2TouchVisual *out, std::size_t capacity);

} // namespace x2::input

#endif /* X2_TOUCH_VISUALS_H */
