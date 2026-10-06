/* See gpu_staging_ring.h. */
#include "gpu_staging_ring.h"

#include "../native/x2_log.h"

#include <SDL3/SDL.h>

#include <string.h>

/*
 * A page is big enough that an ordinary frame's uploads fit in one or two of
 * them -- a measured gameplay frame moves about 550 KB -- and small enough
 * that a run holding a few of them costs nothing worth counting.
 */
enum { kPageBytes = 4u * 1024u * 1024u };
/* More pages than a driver can have frames in flight, so the ring never has
   to wait for one; past that a page is a bug, not a workload. */
enum { kMaxPages = 8 };
/* See the header: the strictest offset alignment a buffer or image copy on
   any supported device asks for. */
enum { kCopyAlignment = 256 };

typedef struct Page {
  SDL_GPUTransferBuffer *buffer;
  uint32_t capacity;
  uint32_t used;
  /* Its bytes are referenced by copies already recorded or submitted, so the
     next write must cycle it. Set for every page when the frame's batch is
     submitted, cleared when the page is cycled. */
  int referenced;
} Page;

static Page g_pages[kMaxPages];
static int g_page_count;
static int g_current;
static unsigned long long g_allocs;
static unsigned long long g_bytes;

static uint32_t aligned_up(uint32_t value, uint32_t alignment) {
  if (alignment < 1u) {
    alignment = 1u;
  }
  return (value + alignment - 1u) / alignment * alignment;
}

static int create_page(SDL_GPUDevice *device, int index, uint32_t capacity) {
  SDL_GPUTransferBufferCreateInfo ci;

  memset(&ci, 0, sizeof ci);
  ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  ci.size = capacity;
  g_pages[index].buffer = SDL_CreateGPUTransferBuffer(device, &ci);
  if (!g_pages[index].buffer) {
    x2_log_error("gpu: an upload staging page of %u byte(s) could not be "
                 "created: %s\n",
                 capacity, SDL_GetError());
    return 0;
  }
  g_pages[index].capacity = capacity;
  g_pages[index].used = 0;
  g_pages[index].referenced = 0;
  g_allocs++;
  return 1;
}

/* The page this write belongs in, or -1. A write that does not fit in what
   is left of the current page moves to the next one; one larger than a whole
   page gets a page sized to it. */
static int page_for(SDL_GPUDevice *device, uint32_t bytes, uint32_t alignment,
                    uint32_t *offset) {
  int index;

  for (index = 0; index < g_page_count; index++) {
    const int at = (g_current + index) % g_page_count;
    /* A referenced page is cycled by the write that takes it, and a cycled
       page is a fresh generation filled from the start -- so it has its whole
       capacity free, not whatever the last frame left. Reading `used` here
       instead made every page look full the frame after it was filled, and
       the ring allocated a new one per frame: the very cost it removes. */
    const uint32_t start =
        g_pages[at].referenced ? 0u : aligned_up(g_pages[at].used, alignment);
    if (g_pages[at].buffer && start + bytes <= g_pages[at].capacity) {
      g_current = at;
      *offset = start;
      return at;
    }
  }
  if (g_page_count >= kMaxPages) {
    x2_log_error("gpu: this frame's uploads exceeded %d staging page(s) of "
                 "%u byte(s); %u more byte(s) could not be placed.\n",
                 kMaxPages, (unsigned)kPageBytes, bytes);
    return -1;
  }
  if (!create_page(device, g_page_count,
                   bytes > (uint32_t)kPageBytes ? bytes
                                                : (uint32_t)kPageBytes)) {
    return -1;
  }
  g_current = g_page_count++;
  *offset = 0;
  return g_current;
}

GpuStagingWrite gpu_staging_write(SDL_GPUDevice *device, const void *data,
                                  uint32_t bytes) {
  GpuStagingWrite write = {NULL, 0};
  uint32_t offset = 0;
  int index;
  void *mapped;
  Page *page;

  if (!device || !data || !bytes) {
    x2_log_error("gpu: an upload of %u byte(s) has nothing to stage.\n", bytes);
    return write;
  }
  index = page_for(device, bytes, kCopyAlignment, &offset);
  if (index < 0) {
    return write;
  }
  page = &g_pages[index];

  /*
   * Cycle only a page whose bytes a recorded copy still refers to, which is
   * its first write in a frame. Every other write in the frame lands on
   * bytes no command has read, which SDL states plainly may be overwritten
   * without cycling -- and that is the whole saving.
   */
  mapped = SDL_MapGPUTransferBuffer(device, page->buffer, page->referenced);
  if (!mapped) {
    x2_log_error("gpu: mapping an upload staging page failed: %s\n",
                 SDL_GetError());
    return write;
  }
  if (page->referenced) {
    /* A cycled page is a fresh generation: nothing in it is referenced, so
       it is filled from the start again. */
    page->referenced = 0;
    page->used = 0;
    offset = 0;
  }
  memcpy((unsigned char *)mapped + offset, data, bytes);
  SDL_UnmapGPUTransferBuffer(device, page->buffer);

  page->used = offset + bytes;
  g_bytes += bytes;
  write.buffer = page->buffer;
  write.offset = offset;
  return write;
}

void gpu_staging_ring_submitted(void) {
  int index;

  for (index = 0; index < g_page_count; index++) {
    if (g_pages[index].used) {
      g_pages[index].referenced = 1;
    }
  }
  g_current = 0;
}

void gpu_staging_ring_destroy(SDL_GPUDevice *device) {
  int index;

  for (index = 0; index < g_page_count; index++) {
    if (device && g_pages[index].buffer) {
      SDL_ReleaseGPUTransferBuffer(device, g_pages[index].buffer);
    }
    g_pages[index].buffer = NULL;
    g_pages[index].capacity = 0;
    g_pages[index].used = 0;
    g_pages[index].referenced = 0;
  }
  g_page_count = 0;
  g_current = 0;
}

void gpu_staging_ring_stats(unsigned long *pages, unsigned long long *allocs,
                            unsigned long long *bytes) {
  if (pages) {
    *pages = (unsigned long)g_page_count;
  }
  if (allocs) {
    *allocs = g_allocs;
  }
  if (bytes) {
    *bytes = g_bytes;
  }
}

uint32_t gpu_staging_ring_page_bytes(void) { return (uint32_t)kPageBytes; }
