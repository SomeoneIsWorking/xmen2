#ifndef X2_MOVIE_AUDIO_H
#define X2_MOVIE_AUDIO_H

#include <stddef.h>

/* One streaming stereo-F32 voice owned by the audio subsystem. The media
   decoder never sees SDL; DirectSound's existing device callback asks this
   owner to mix into the same output stream as the game. */
int    movie_audio_open(int sample_rate);
int    movie_audio_queue(const float *stereo, size_t frames);
void   movie_audio_play(void);
void   movie_audio_pause(int paused);
void   movie_audio_close(void);
void   movie_audio_mix(float *stereo_mix, int frames, int output_rate);
double movie_audio_played_seconds(void);
double movie_audio_queued_seconds(void);
int    movie_audio_active(void);
void   movie_audio_report(void);

#endif
