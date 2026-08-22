/* Headless frame-capture diagnostics, separate from shipping presentation. */
#include "gpu_capture.h"

#include "gpu_device.h"
#include "gpu_draw.h"
#include "gpu_internal.h"

#include <stdio.h>
#include <stdlib.h>

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
