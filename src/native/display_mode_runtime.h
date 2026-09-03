#ifndef X2_DISPLAY_MODE_RUNTIME_H
#define X2_DISPLAY_MODE_RUNTIME_H

#include <stdint.h>

/*
 * Publish a live output-size change into the retained title display state.
 * This is the state XMen2.exe establishes before it builds cameras and UI;
 * changing only the D3D backbuffer leaves that old aspect stretched.
 */
int x2_display_mode_runtime_apply(uint32_t width, uint32_t height, char *why,
                                  int whyn);

#endif /* X2_DISPLAY_MODE_RUNTIME_H */
