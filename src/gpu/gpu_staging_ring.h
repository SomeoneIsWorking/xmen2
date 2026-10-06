/*
 * THE FRAME'S UPLOAD BYTES, IN ONE TRANSFER BUFFER.
 *
 * SDL_GPU has no "write straight into a GPU resource": bytes go into a
 * transfer buffer and a copy pass moves them. Giving every resource its own
 * transfer buffer and cycling it per upload cost a measured 85 ms of an 88 ms
 * gameplay frame -- 308,362 uploads produced 57,856 transfer-buffer
 * allocations, about 31 new GPU allocations every frame, and the frame was
 * spent inside the driver's virtual-address allocator rather than moving the
 * ~550 KB the uploads actually carried.
 *
 * Cycling is what produced them. A resource written while the open command
 * buffer has already referenced it must be cycled, and cycling allocates when
 * no unbound generation is free -- so a buffer uploaded twice in a frame paid
 * for two allocations, exactly as SDL documents.
 *
 * The ring writes each upload at its OWN offset in a shared page instead, and
 * SDL's rule is explicit that this needs no cycle at all: "It is OK to
 * overwrite unreferenced data in a bound resource without cycling." Only
 * reusing a page in a later frame overwrites referenced bytes, so a page is
 * cycled exactly once per frame, on its first write. The allocation count
 * stops scaling with the number of uploads and becomes the number of pages in
 * flight.
 */
#ifndef GPU_STAGING_RING_H
#define GPU_STAGING_RING_H

#include <stdint.h>

struct SDL_GPUDevice;
struct SDL_GPUTransferBuffer;

/* Where an upload's bytes landed: SDL copy commands take both. */
typedef struct GpuStagingWrite {
  struct SDL_GPUTransferBuffer *buffer;
  uint32_t offset;
} GpuStagingWrite;

/*
 * Copy `bytes` into the ring and say where they landed.
 *
 * `buffer` is NULL when the ring could not place them, which is reported by
 * this owner; the caller records nothing. An upload larger than a page gets a
 * page of its own rather than being refused.
 *
 * Every write starts on a 256-byte boundary. A copy into an image requires an
 * offset that is a multiple of both four and the texel block size, and a
 * driver may ask for more for an optimal copy; 256 satisfies every alignment
 * any format or device here asks for, and the waste is bounded by the number
 * of uploads in a frame rather than by their size.
 */
GpuStagingWrite gpu_staging_write(struct SDL_GPUDevice *device,
                                  const void *data, uint32_t bytes);

/*
 * The frame's copies have been submitted: every page's bytes are referenced
 * now, so the next write to a page must cycle it.
 *
 * Called from the batch flush, which is the one place that knows the frame's
 * copy commands are gone. A ring that is never told this would overwrite
 * bytes a submitted copy still reads.
 */
void gpu_staging_ring_submitted(void);

void gpu_staging_ring_destroy(struct SDL_GPUDevice *device);

/* Pages held, allocations asked of the driver, and bytes written. The
   allocation count against the upload count is the whole point of this
   owner: it must stay flat while uploads climb. */
void gpu_staging_ring_stats(unsigned long *pages, unsigned long long *allocs,
                            unsigned long long *bytes);

/* Test seam: the page size the ring bump-allocates within. */
uint32_t gpu_staging_ring_page_bytes(void);

#endif
