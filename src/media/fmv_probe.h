#pragma once

#include <cstddef>
#include <cstdint>

namespace x2::media {

struct FmvProbeStats {
  int active;
  unsigned long decoded_frames;
  unsigned long padded_checks;
  unsigned long padded_mismatch_rows;
  unsigned long upload_candidates;
  unsigned long upload_matches;
  unsigned long upload_mismatch_rows;
  unsigned long complete_frames;
};

void fmv_probe_begin(const char *guest_path);
void fmv_probe_decoded(const std::uint8_t *pixels, int width, int height,
                       std::size_t pitch);
void fmv_probe_padded(const std::uint8_t *pixels, std::size_t bytes,
                      std::size_t pitch);
void fmv_probe_upload(const std::uint8_t *pixels, std::size_t bytes, int width,
                      int height, std::size_t pitch);
void fmv_probe_get_stats(FmvProbeStats *stats);
void fmv_probe_report();
void fmv_probe_end();

} // namespace x2::media
