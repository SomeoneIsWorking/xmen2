#ifndef X2_FMV_PLAYER_H
#define X2_FMV_PLAYER_H

#include "fmv_audio_sink.h"

#include <stddef.h>
#include <stdint.h>

typedef struct X2FmvPlayer X2FmvPlayer;

typedef enum {
  X2_FMV_READY,
  X2_FMV_PLAYING,
  X2_FMV_PAUSED,
  X2_FMV_FINISHED,
  X2_FMV_FAILED
} X2FmvState;

X2FmvPlayer *x2_fmv_open(const char *path, const X2FmvAudioSink *sink,
                         char *error, size_t error_size);
void x2_fmv_close(X2FmvPlayer *player);
void x2_fmv_play(X2FmvPlayer *player);
void x2_fmv_pause(X2FmvPlayer *player, int paused);
int x2_fmv_update(X2FmvPlayer *player, double playback_seconds);
int x2_fmv_copy_bgra(const X2FmvPlayer *player, void *destination,
                     size_t destination_bytes, size_t destination_pitch);
int x2_fmv_width(const X2FmvPlayer *player);
int x2_fmv_height(const X2FmvPlayer *player);
int x2_fmv_sample_rate(const X2FmvPlayer *player);
X2FmvState x2_fmv_state(const X2FmvPlayer *player);
unsigned long x2_fmv_decoded_frames(const X2FmvPlayer *player);
unsigned long x2_fmv_decoded_audio_frames(const X2FmvPlayer *player);
void x2_fmv_report(const X2FmvPlayer *player);

#endif
