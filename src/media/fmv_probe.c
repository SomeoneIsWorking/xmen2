/* Exact row-chain evidence from decoded frame to guest image to D3D8 upload. */
#include "fmv_probe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    X2FmvProbeStats stats;
    uint8_t *expected;
    size_t expected_bytes;
    int width;
    int height;
    unsigned long generation;
    unsigned long padded_generation;
    unsigned long completed_generation;
    char movie[96];
} FmvProbe;

static FmvProbe g_probe;

static size_t power_of_two_at_least(int value)
{
    size_t result = 1;
    if (value <= 0) return 0;
    while (result < (size_t)value) {
        if (result > SIZE_MAX / 2u) return 0;
        result *= 2u;
    }
    return result;
}

static unsigned long mismatched_rows(const uint8_t *actual, size_t actual_pitch)
{
    size_t row_bytes = (size_t)g_probe.width * 4u;
    unsigned long mismatches = 0;
    int row;
    for (row = 0; row < g_probe.height; ++row) {
        if (memcmp(actual + (size_t)row * actual_pitch,
                   g_probe.expected + (size_t)row * row_bytes,
                   row_bytes) != 0) mismatches++;
    }
    return mismatches;
}

static int readable_rows(size_t bytes, size_t pitch)
{
    size_t row_bytes = (size_t)g_probe.width * 4u;
    return pitch >= row_bytes && g_probe.height > 0
        && bytes >= pitch * (size_t)g_probe.height;
}

void x2_fmv_probe_begin(const char *guest_path)
{
    const char *filter = getenv("X2_FMV_PROBE");
    free(g_probe.expected);
    memset(&g_probe, 0, sizeof(g_probe));
    if (!filter || !*filter || !guest_path || !strstr(guest_path, filter))
        return;
    g_probe.stats.active = 1;
    snprintf(g_probe.movie, sizeof(g_probe.movie), "%s", guest_path);
    printf("movie probe: tracing decoded -> padded igImage -> D3D8 upload rows "
           "for '%s'\n", guest_path);
}

void x2_fmv_probe_decoded(const uint8_t *pixels, int width, int height,
                          size_t pitch)
{
    size_t row_bytes;
    size_t bytes;
    uint8_t *expected;
    int row;
    if (!g_probe.stats.active || !pixels || width <= 0 || height <= 0)
        return;
    row_bytes = (size_t)width * 4u;
    bytes = row_bytes * (size_t)height;
    if (pitch < row_bytes) return;
    if (bytes != g_probe.expected_bytes) {
        expected = (uint8_t *)realloc(g_probe.expected, bytes);
        if (!expected) return;
        g_probe.expected = expected;
        g_probe.expected_bytes = bytes;
    }
    for (row = 0; row < height; ++row)
        memcpy(g_probe.expected + (size_t)row * row_bytes,
               pixels + (size_t)row * pitch, row_bytes);
    g_probe.width = width;
    g_probe.height = height;
    g_probe.generation++;
    g_probe.padded_generation = 0;
    g_probe.stats.decoded_frames++;
}

void x2_fmv_probe_padded(const uint8_t *pixels, size_t bytes, size_t pitch)
{
    unsigned long mismatches;
    if (!g_probe.stats.active || !g_probe.generation || !pixels
            || !readable_rows(bytes, pitch)) return;
    mismatches = mismatched_rows(pixels, pitch);
    g_probe.stats.padded_checks++;
    g_probe.stats.padded_mismatch_rows += mismatches;
    if (!mismatches) g_probe.padded_generation = g_probe.generation;
}

void x2_fmv_probe_upload(const uint8_t *pixels, size_t bytes,
                         int width, int height, size_t pitch)
{
    unsigned long mismatches;
    size_t storage_width;
    size_t storage_height;
    if (!g_probe.stats.active
            || g_probe.padded_generation != g_probe.generation || !pixels)
        return;
    storage_width = power_of_two_at_least(g_probe.width);
    storage_height = power_of_two_at_least(g_probe.height);
    if ((size_t)width != storage_width || (size_t)height != storage_height
            || pitch != storage_width * 4u
            || bytes < pitch * storage_height) return;
    mismatches = mismatched_rows(pixels, pitch);
    g_probe.stats.upload_candidates++;
    g_probe.stats.upload_mismatch_rows += mismatches;
    if (mismatches) return;
    g_probe.stats.upload_matches++;
    if (g_probe.completed_generation != g_probe.generation) {
        g_probe.completed_generation = g_probe.generation;
        g_probe.stats.complete_frames++;
    }
}

void x2_fmv_probe_get_stats(X2FmvProbeStats *stats)
{
    if (stats) *stats = g_probe.stats;
}

void x2_fmv_probe_report(void)
{
    if (g_probe.stats.active) {
        printf("movie probe: '%s': %lu decoded, %lu padded check(s) / %lu "
               "mismatched row(s), %lu upload candidate(s) / %lu exact / "
               "%lu mismatched row(s), %lu complete frame chain(s)\n",
               g_probe.movie, g_probe.stats.decoded_frames,
               g_probe.stats.padded_checks,
               g_probe.stats.padded_mismatch_rows,
               g_probe.stats.upload_candidates, g_probe.stats.upload_matches,
               g_probe.stats.upload_mismatch_rows,
               g_probe.stats.complete_frames);
    }
}

void x2_fmv_probe_end(void)
{
    x2_fmv_probe_report();
    free(g_probe.expected);
    g_probe.expected = NULL;
    g_probe.expected_bytes = 0;
    g_probe.stats.active = 0;
}
