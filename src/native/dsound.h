#ifndef X2_DSOUND_H
#define X2_DSOUND_H

#include <stdint.h>

void dsound_install(void);
void dsound_report(void);
int dsound_selftest(void);
void dsound_movie_audio_begin(void);
void dsound_movie_audio_tick(void);

int dsound_buffer_is_playing(uint32_t guest);
unsigned dsound_buffer_release_guest(uint32_t guest);

#endif
