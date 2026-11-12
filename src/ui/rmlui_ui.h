#pragma once

#include <cstdint>

struct SDL_GPUCommandBuffer;
struct SDL_GPUDevice;
struct SDL_GPUTexture;
struct SDL_Window;

namespace x2::ui {

/* Render after the guest pass and before command-buffer submission. */
void ui_render(struct SDL_GPUDevice *device,
               struct SDL_GPUCommandBuffer *command_buffer,
               struct SDL_GPUTexture *swapchain, uint32_t width,
               uint32_t height, struct SDL_Window *window);
void ui_gpu_shutdown(void);

} // namespace x2::ui
