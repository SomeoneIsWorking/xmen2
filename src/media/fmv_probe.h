#ifndef X2_FMV_PROBE_H
#define X2_FMV_PROBE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  int active;
  unsigned long decoded_frames;
  unsigned long padded_checks;
  unsigned long padded_mismatch_rows;
  unsigned long upload_candidates;
  unsigned long upload_matches;
  unsigned long upload_mismatch_rows;
  unsigned long complete_frames;
} X2FmvProbeStats;

void x2_fmv_probe_begin(const char *guest_path);
void x2_fmv_probe_decoded(const uint8_t *pixels, int width, int height,
                          size_t pitch);
void x2_fmv_probe_padded(const uint8_t *pixels, size_t bytes, size_t pitch);
void x2_fmv_probe_upload(const uint8_t *pixels, size_t bytes, int width,
                         int height, size_t pitch);
void x2_fmv_probe_get_stats(X2FmvProbeStats *stats);
void x2_fmv_probe_report(void);
void x2_fmv_probe_end(void);

#endif
