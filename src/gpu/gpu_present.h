#ifndef X2_GPU_PRESENT_H
#define X2_GPU_PRESENT_H

#include <stdint.h>

struct SDL_GPUCommandBuffer;
struct SDL_GPUDevice;
struct SDL_GPUTexture;

/* The D3D backbuffer is a logical render destination, not the host window.
   Prepare both colour and automatic-depth replacements before committing
   either, so a failed live resize leaves the last renderable pair and its
   dimensions intact. INVALID depth format means this device has no depth
   target to preserve. */
int gpu_present_resize_targets(struct SDL_GPUDevice *device,
                               uint32_t width, uint32_t height,
                               uint32_t depth_format);
int gpu_present_is_configured(void);

/* Return the already-proven logical scene texture. */
struct SDL_GPUTexture *gpu_present_scene(struct SDL_GPUDevice *device,
                                         uint32_t *width, uint32_t *height);

/* The depth attachment follows whichever render destination is active. A
   non-backbuffer size change is independently transactional. */
struct SDL_GPUTexture *gpu_present_depth_target(
    struct SDL_GPUDevice *device, uint32_t width, uint32_t height,
    uint32_t depth_format);

/* Clear the output to black and scale the complete logical scene into its
   centred aspect-fit rectangle. */
int gpu_present_composite(struct SDL_GPUCommandBuffer *command_buffer,
                          struct SDL_GPUTexture *output,
                          uint32_t output_width, uint32_t output_height);

/* Present the target as pure black for one frame -- the boot presentation
   policy's withholding of the retail boot's branding. Counts the frame in
   the boot blackout's report. */
void gpu_present_boot_blackout(struct SDL_GPUCommandBuffer *command_buffer,
                               struct SDL_GPUTexture *target);

void gpu_present_shutdown(struct SDL_GPUDevice *device);

#endif
