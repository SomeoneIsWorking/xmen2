#pragma once

#include "fmv_audio_sink.h"

#include <cstddef>
#include <cstdint>

namespace x2::media {

struct FmvPlayer;

enum class FmvState { Ready, Playing, Paused, Finished, Failed };

FmvPlayer *fmv_open(const char *path, const X2FmvAudioSink *sink, char *error,
                    std::size_t error_size);
void fmv_close(FmvPlayer *player);
void fmv_play(FmvPlayer *player);
void fmv_pause(FmvPlayer *player, int paused);
int fmv_update(FmvPlayer *player, double playback_seconds);
int fmv_copy_bgra(const FmvPlayer *player, void *destination,
                  std::size_t destination_bytes, std::size_t destination_pitch);
int fmv_width(const FmvPlayer *player);
int fmv_height(const FmvPlayer *player);
int fmv_sample_rate(const FmvPlayer *player);
FmvState fmv_state(const FmvPlayer *player);
unsigned long fmv_decoded_frames(const FmvPlayer *player);
unsigned long fmv_decoded_audio_frames(const FmvPlayer *player);
void fmv_report(const FmvPlayer *player);

} // namespace x2::media
