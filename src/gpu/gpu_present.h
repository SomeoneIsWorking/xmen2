#ifndef X2_GPU_PRESENT_H
#define X2_GPU_PRESENT_H

#include <stdint.h>

struct SDL_GPUCommandBuffer;
struct SDL_GPUDevice;
struct SDL_GPUTexture;

/* The D3D backbuffer is a logical render destination, not the host window.
   Configure its dimensions from D3DPRESENT_PARAMETERS. */
int gpu_present_set_scene_size(uint32_t width, uint32_t height);
int gpu_present_is_configured(void);

/* Resolve the logical scene texture, creating it on first use. */
struct SDL_GPUTexture *gpu_present_scene(struct SDL_GPUDevice *device,
                                         uint32_t *width, uint32_t *height);

/* Clear the output to black and scale the complete logical scene into its
   centred aspect-fit rectangle. */
int gpu_present_composite(struct SDL_GPUCommandBuffer *command_buffer,
                          struct SDL_GPUTexture *output,
                          uint32_t output_width, uint32_t output_height);

void gpu_present_shutdown(struct SDL_GPUDevice *device);

#endif
