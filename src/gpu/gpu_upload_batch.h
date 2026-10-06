/*
 * The frame's copy commands, in ONE command buffer.
 *
 * SDL_GPU has no "write straight into a GPU resource": data goes into a
 * transfer buffer and a copy pass moves it. Recording each of those copies in
 * its own command buffer and submitting it immediately cost a measured 23 ms
 * of an 88 ms gameplay frame on a Huawei BKY-W09 -- about 0.7 ms for each of
 * ~34 uploads, against only ~550 KB of actual data, so the cost was the
 * submission and not the bytes.
 *
 * Ordering is what makes one command buffer safe: SDL_GPU executes submitted
 * command buffers in submission order, so a batch submitted at any point
 * before the frame's own command buffer still executes before every draw in
 * it. Cycling still separates generations, because SDL cycles a resource that
 * the OPEN command buffer has already written -- a buffer uploaded twice in a
 * frame gets two allocations here exactly as it did with two command buffers.
 *
 * The batch must therefore be flushed before ANY other command buffer that
 * reads what it wrote is submitted. gpu_upload_batch_flush is idempotent and
 * costs nothing when no batch is open, so those call sites simply call it.
 */
#ifndef GPU_UPLOAD_BATCH_H
#define GPU_UPLOAD_BATCH_H

struct SDL_GPUCopyPass;
struct SDL_GPUDevice;

/* The frame's open copy pass, acquiring the command buffer on first use.
   NULL means the command buffer or the pass could not be created; the caller
   reports its own failure and does not record. */
struct SDL_GPUCopyPass *gpu_upload_batch_pass(struct SDL_GPUDevice *device);

/* End and submit the open batch, if there is one. Call before submitting any
   command buffer whose commands read what the batch wrote. */
void gpu_upload_batch_flush(struct SDL_GPUDevice *device);

/* Copy command buffers submitted. Against gpu_draw's upload count this says
   whether batching is happening: a ratio near one upload per command buffer
   means the frame is paying a submission per upload again. */
unsigned long gpu_upload_batch_submits(void);

#endif
