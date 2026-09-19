#ifndef GPU_DEPTH_H
#define GPU_DEPTH_H

/* Forget the format this device was found to have, when that device goes.
   The next device is asked again rather than inheriting an answer that was
   true of another driver. gpu_depth_format() and gpu_depth_target() are
   declared with the rest of the renderer's internals in gpu_internal.h. */
void gpu_depth_forget(void);

#endif
