#ifndef X2_RMLUI_UI_H
#define X2_RMLUI_UI_H

#include <stdint.h>

union SDL_Event;
struct SDL_GPUCommandBuffer;
struct SDL_GPUDevice;
struct SDL_GPUTexture;
struct SDL_Window;

#ifdef __cplusplus
extern "C" {
#endif

/* The pause menu's distinct Port Settings command shows this document, and
    F2 toggles it from anywhere (settings_overlay_state owns that key).
    Returns non-zero when the UI consumed an event and the guest input layer
    must not act on it. */
int x2_ui_handle_event(union SDL_Event *event);
int x2_ui_captures_input(void);

/* Render after the guest pass and before command-buffer submission. */
void x2_ui_render(struct SDL_GPUDevice *device,
                  struct SDL_GPUCommandBuffer *command_buffer,
                  struct SDL_GPUTexture *swapchain,
                  uint32_t width, uint32_t height,
                  struct SDL_Window *window);
void x2_ui_gpu_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
