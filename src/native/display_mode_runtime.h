#pragma once

#include <cstdint>

namespace x2::native {

/*
 * Publish a live output-size change into the retained title display state.
 * This is the state XMen2.exe establishes before it builds cameras and UI;
 * changing only the D3D backbuffer leaves that old aspect stretched.
 */
int display_mode_runtime_apply(uint32_t width, uint32_t height, char *why,
                               int whyn);

} // namespace x2::native
