/* Final-frame capture for live control and headless diagnostics. */
#include "gpu_capture.h"
#include "gpu_capture_internal.h"

#include "gpu_device.h"
#include "gpu_draw.h"
#include "gpu_frame_submit.h"
#include "gpu_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 64 MiB admits a 5120x2880 BGRA frame and refuses an accidental 8K-sized
   allocation. The PNG encoder has its own bounded multiple of this input, so
   this is the one authoritative raw-frame ceiling. */
#define GPU_CAPTURE_MAX_BYTES (64u * 1024u * 1024u)

enum CaptureState {
    CAPTURE_IDLE,
    CAPTURE_PENDING,
    CAPTURE_RECORDED,
    CAPTURE_READY,
    CAPTURE_FAILED,
};

static enum CaptureState g_capture_state;
static unsigned char *g_capture_pixels;
static size_t g_capture_capacity;
static uint32_t g_capture_width, g_capture_height, g_capture_bytes;
static char g_capture_failure[192];
static void (*g_frame_observer)(void);

#ifdef X2_WITH_SDL
static SDL_GPUTexture *g_capture_texture;
static uint32_t g_capture_texture_width, g_capture_texture_height;
static SDL_GPUTransferBuffer *g_capture_transfer;
#endif

static void capture_reason(char *why, int whyn, const char *reason)
{
    if (why && whyn > 0) snprintf(why, (size_t)whyn, "%s", reason);
}

static void capture_fail(const char *reason)
{
    snprintf(g_capture_failure, sizeof g_capture_failure, "%s", reason);
    g_capture_state = CAPTURE_FAILED;
}

static int capture_dimensions(uint32_t width, uint32_t height,
                              uint32_t *bytes, char *why, int whyn)
{
    size_t need;

    if (!width || !height) {
        capture_reason(why, whyn,
                       "the final frame has a zero width or height");
        return 0;
    }
    if ((size_t)width > SIZE_MAX / (size_t)height
        || (size_t)width * (size_t)height > SIZE_MAX / 4u) {
        capture_reason(why, whyn,
                       "the final frame dimensions overflow a BGRA buffer");
        return 0;
    }
    need = (size_t)width * (size_t)height * 4u;
    if (need > GPU_CAPTURE_MAX_BYTES) {
        char reason[192];
        snprintf(reason, sizeof reason,
                 "the %ux%u frame needs %zu bytes; the screenshot bound is "
                 "%u bytes", width, height, need, GPU_CAPTURE_MAX_BYTES);
        capture_reason(why, whyn, reason);
        return 0;
    }
    *bytes = (uint32_t)need;
    return 1;
}

int gpu_capture_request(char *why, int whyn)
{
    if (!gpu_device_ready()) {
        capture_reason(why, whyn,
                       "the GPU device does not exist yet, so there is no "
                       "frame to capture");
        return 0;
    }
    if (g_capture_state == CAPTURE_RECORDED) {
        capture_reason(why, whyn,
                       "the previous screenshot is already being submitted");
        return 0;
    }
    g_capture_failure[0] = '\0';
    g_capture_state = CAPTURE_PENDING;
    return 1;
}

int gpu_capture_result(const unsigned char **bgra, uint32_t *width,
                       uint32_t *height, char *why, int whyn)
{
    if (bgra) *bgra = NULL;
    if (width) *width = 0;
    if (height) *height = 0;
    if (g_capture_state == CAPTURE_PENDING
        || g_capture_state == CAPTURE_RECORDED)
        return 0;
    if (g_capture_state == CAPTURE_FAILED) {
        capture_reason(why, whyn, g_capture_failure[0]
                       ? g_capture_failure : "the frame capture failed");
        return -1;
    }
    if (g_capture_state != CAPTURE_READY) {
        capture_reason(why, whyn, "no screenshot has been requested");
        return -1;
    }
    if (bgra) *bgra = g_capture_pixels;
    if (width) *width = g_capture_width;
    if (height) *height = g_capture_height;
    return 1;
}

void gpu_capture_discard(void)
{
    g_capture_state = CAPTURE_IDLE;
    g_capture_width = g_capture_height = g_capture_bytes = 0;
    g_capture_failure[0] = '\0';
}

void gpu_capture_set_frame_observer(void (*fn)(void))
{
    g_frame_observer = fn;
}

#ifdef X2_WITH_SDL
static int capture_reserve_pixels(uint32_t width, uint32_t height)
{
    unsigned char *larger;
    char reason[192];
    uint32_t bytes;

    if (!capture_dimensions(width, height, &bytes, reason, sizeof reason)) {
        capture_fail(reason);
        return 0;
    }
    if ((size_t)bytes > g_capture_capacity) {
        larger = (unsigned char *)realloc(g_capture_pixels, bytes);
        if (!larger) {
            snprintf(reason, sizeof reason,
                     "could not allocate the bounded %u-byte BGRA screenshot",
                     bytes);
            capture_fail(reason);
            return 0;
        }
        g_capture_pixels = larger;
        g_capture_capacity = bytes;
    }
    g_capture_width = width;
    g_capture_height = height;
    g_capture_bytes = bytes;
    return 1;
}

SDL_GPUTexture *gpu_capture_frame_target(SDL_GPUDevice *device,
                                         SDL_GPUTexture *fallback,
                                         uint32_t width, uint32_t height)
{
    SDL_GPUTextureCreateInfo info;
    char reason[192];
    uint32_t bytes;

    if (g_capture_state != CAPTURE_PENDING) return fallback;
    if (!device || !capture_dimensions(width, height, &bytes,
                                       reason, sizeof reason)) {
        capture_fail(device ? reason : "the GPU device disappeared before "
                                         "the screenshot frame began");
        return fallback;
    }
    if (g_capture_texture
        && (g_capture_texture_width != width
            || g_capture_texture_height != height)) {
        SDL_ReleaseGPUTexture(device, g_capture_texture);
        g_capture_texture = NULL;
    }
    if (!g_capture_texture) {
        memset(&info, 0, sizeof info);
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET
                     | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = width;
        info.height = height;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        g_capture_texture = SDL_CreateGPUTexture(device, &info);
        if (!g_capture_texture) {
            snprintf(reason, sizeof reason,
                     "could not create the retained %ux%u screenshot target: "
                     "%s", width, height, SDL_GetError());
            capture_fail(reason);
            return fallback;
        }
        g_capture_texture_width = width;
        g_capture_texture_height = height;
    }
    (void)bytes;
    return g_capture_texture;
}

int gpu_capture_frame_record(SDL_GPUDevice *device,
                             SDL_GPUCommandBuffer *command,
                             SDL_GPUTexture *rendered,
                             SDL_GPUTexture *output,
                             uint32_t width, uint32_t height)
{
    SDL_GPUTransferBufferCreateInfo transfer_info;
    SDL_GPUTextureRegion source;
    SDL_GPUTextureTransferInfo destination;
    SDL_GPUCopyPass *copy;
    char reason[192];

    if (g_capture_state != CAPTURE_PENDING) return 0;
    /* A swapchain image cannot be retained after submit. If a request arrived
       after this direct-to-window frame began, leave it armed for the next
       frame, whose begin path will select g_capture_texture instead. */
    if (output && rendered == output) return 0;
    if (!device || !command || !rendered) {
        capture_fail("the screenshot frame ended without a GPU command or "
                     "retained render target");
        return 0;
    }
    if (!capture_reserve_pixels(width, height)) return 0;

    if (output && rendered != output) {
        SDL_GPUBlitInfo blit;
        memset(&blit, 0, sizeof blit);
        blit.source.texture = rendered;
        blit.source.w = width;
        blit.source.h = height;
        blit.destination.texture = output;
        blit.destination.w = width;
        blit.destination.h = height;
        blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
        blit.filter = SDL_GPU_FILTER_LINEAR;
        SDL_BlitGPUTexture(command, &blit);
    }

    memset(&transfer_info, 0, sizeof transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transfer_info.size = g_capture_bytes;
    g_capture_transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    if (!g_capture_transfer) {
        snprintf(reason, sizeof reason,
                 "could not allocate the %u-byte GPU screenshot readback: %s",
                 g_capture_bytes, SDL_GetError());
        capture_fail(reason);
        return 0;
    }
    copy = SDL_BeginGPUCopyPass(command);
    if (!copy) {
        snprintf(reason, sizeof reason,
                 "could not begin the screenshot copy pass: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, g_capture_transfer);
        g_capture_transfer = NULL;
        capture_fail(reason);
        return 0;
    }
    memset(&source, 0, sizeof source);
    memset(&destination, 0, sizeof destination);
    source.texture = rendered;
    source.w = width;
    source.h = height;
    source.d = 1;
    destination.transfer_buffer = g_capture_transfer;
    SDL_DownloadFromGPUTexture(copy, &source, &destination);
    SDL_EndGPUCopyPass(copy);
    g_capture_state = CAPTURE_RECORDED;
    return 1;
}

void gpu_capture_frame_complete(SDL_GPUDevice *device, int submitted)
{
    void *mapped;

    if (g_capture_state != CAPTURE_RECORDED) return;
    if (!submitted) {
        if (g_capture_transfer)
            SDL_ReleaseGPUTransferBuffer(device, g_capture_transfer);
        g_capture_transfer = NULL;
        capture_fail("the screenshot command buffer could not be submitted");
        return;
    }
    mapped = SDL_MapGPUTransferBuffer(device, g_capture_transfer, false);
    if (!mapped) {
        char reason[192];
        snprintf(reason, sizeof reason,
                 "could not map the completed screenshot readback: %s",
                 SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, g_capture_transfer);
        g_capture_transfer = NULL;
        capture_fail(reason);
        return;
    }
    memcpy(g_capture_pixels, mapped, g_capture_bytes);
    SDL_UnmapGPUTransferBuffer(device, g_capture_transfer);
    SDL_ReleaseGPUTransferBuffer(device, g_capture_transfer);
    g_capture_transfer = NULL;
    g_capture_state = CAPTURE_READY;
}

int gpu_capture_submit_frame(SDL_GPUDevice *device,
                             SDL_GPUCommandBuffer *command,
                             int wait_without_capture,
                             SDL_GPUTexture *rendered,
                             SDL_GPUTexture *output,
                             uint32_t width, uint32_t height)
{
    int recorded = gpu_capture_frame_record(
        device, command, rendered, output, width, height);
    int submitted = gpu_frame_submit(
        device, command, wait_without_capture || recorded);
    gpu_capture_frame_complete(device, submitted);
    if (g_frame_observer) g_frame_observer();
    return submitted;
}
#endif

void gpu_capture_shutdown(void)
{
#ifdef X2_WITH_SDL
    if (g_gpu && g_capture_transfer)
        SDL_ReleaseGPUTransferBuffer(g_gpu, g_capture_transfer);
    if (g_gpu && g_capture_texture)
        SDL_ReleaseGPUTexture(g_gpu, g_capture_texture);
    g_capture_transfer = NULL;
    g_capture_texture = NULL;
    g_capture_texture_width = g_capture_texture_height = 0;
#endif
    free(g_capture_pixels);
    g_capture_pixels = NULL;
    g_capture_capacity = 0;
    gpu_capture_discard();
}

void gpu_capture_frame(int headless, unsigned long frame,
                       uint32_t width, uint32_t height)
{
    static const char *path;
    static int checked, every = 60;
    static unsigned long min_draws;
    static unsigned long busy_written;
    static int require_vs;
    static long keep = -1, kept;
    static unsigned char *buf;
    static uint32_t buf_bytes;
    char numbered[512];
    uint32_t read_width, read_height, i, bytes;
    FILE *file;

    if (!checked) {
        const char *value;
        checked = 1;
        path = getenv("X2_SHOT");
        if (path && !*path) path = NULL;
        if ((value = getenv("X2_SHOT_EVERY")) && *value) every = atoi(value);
        if (every < 1) every = 1;
        if ((value = getenv("X2_SHOT_MIN_DRAWS")) && *value)
            min_draws = strtoul(value, NULL, 10);
        if ((value = getenv("X2_SHOT_VS")) && *value && *value != '0')
            require_vs = 1;
        if ((value = getenv("X2_SHOT_KEEP")) && *value) {
            keep = atol(value);
            if (keep < 1) keep = 1;
        }
        if (path)
            printf("gpu: X2_SHOT -- the headless target is written to %s every "
                   "%d frame(s), overwriting.\n", path, every);
        if (path && getenv("X2_SHOT_AFTER_FILE")
            && *getenv("X2_SHOT_AFTER_FILE"))
            printf("gpu: X2_SHOT_AFTER_FILE=%s -- NOTHING is photographed "
                   "until the game opens a file whose name contains that. If "
                   "it never does, no file is written and this run "
                   "photographed NOTHING.\n", getenv("X2_SHOT_AFTER_FILE"));
        if (path && min_draws)
            printf("gpu: X2_SHOT_MIN_DRAWS=%lu -- only frames with at least "
                   "that many draws are photographed. If none ever is, NO "
                   "file is written and this run photographed NOTHING.\n",
                   min_draws);
        if (path && require_vs)
            printf("gpu: X2_SHOT_VS=1 -- only frames that received a "
                   "programmable draw are photographed. If none ever is, "
                   "NO file is written.\n");
    }
    if (path && !headless) {
        static int said;
        if (!said++)
            printf("gpu: X2_SHOT=%s is set but this run has a REAL WINDOW. The "
                   "capture reads back the headless target, which does not "
                   "exist here, so NOTHING will be written. Add --no-window.\n",
                   path);
        return;
    }
    if (!path || !headless || (frame % (unsigned long)every)) return;
    {
        extern int k32_file_gate_open(void);
        if (!k32_file_gate_open()) return;
    }
    if (min_draws && gpu_frame_draws_so_far() < min_draws) return;
    if (require_vs && !gpu_frame_had_programmable()) return;
    if (min_draws) busy_written++;
    if (width > UINT32_MAX / 4u || height > UINT32_MAX / (width * 4u)) {
        fprintf(stderr, "gpu: X2_SHOT refuses overflowing target %ux%u.\n",
                width, height);
        path = NULL;
        return;
    }
    bytes = width * height * 4u;
    if (bytes > buf_bytes) {
        unsigned char *larger = (unsigned char *)realloc(buf, bytes);
        if (!larger) return;
        buf = larger;
        buf_bytes = bytes;
    }
    if (!gpu_device_headless_read(buf, bytes, &read_width, &read_height)) return;
    if (keep > 0 && kept >= keep) {
        static int said;
        if (!said++)
            printf("gpu: X2_SHOT_KEEP -- %ld frame(s) kept as %s.000..%s.%03ld; "
                   "everything after this point is NOT photographed.\n",
                   kept, path, path, kept - 1);
        return;
    }
    if (keep > 0) {
        snprintf(numbered, sizeof numbered, "%s.%03ld", path, kept);
        file = fopen(numbered, "wb");
    } else {
        file = fopen(path, "wb");
    }
    if (!file) {
        fprintf(stderr, "gpu: X2_SHOT could not open %s\n",
                keep > 0 ? numbered : path);
        path = NULL;
        return;
    }
    if (keep > 0 && !kept)
        printf("gpu: X2_SHOT_KEEP=%ld -- keeping the first %ld qualifying "
               "frame(s) as %s.000 onward rather than overwriting one file.\n",
               keep, keep, path);
    if (keep > 0)
        printf("gpu: X2_SHOT_KEEP -- %s.%03ld is presented frame %lu "
               "(%lu draws).\n", path, kept, frame,
               gpu_frame_draws_so_far());
    kept++;
    if (min_draws && busy_written == 1)
        printf("gpu: X2_SHOT_MIN_DRAWS -- first frame with at least %lu draws "
               "photographed (frame %lu, %lu draws).\n", min_draws,
               frame, gpu_frame_draws_so_far());
    fprintf(file, "P6\n%u %u\n255\n", read_width, read_height);
    for (i = 0; i < read_width * read_height; i++) {
        fputc(buf[i * 4 + 2], file);
        fputc(buf[i * 4 + 1], file);
        fputc(buf[i * 4 + 0], file);
    }
    fclose(file);
}
