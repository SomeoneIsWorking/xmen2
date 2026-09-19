#ifndef X2_DSOUND_H
#define X2_DSOUND_H

#include <stdint.h>

void dsound_install(void);
void dsound_report(void);

/*
 * The audio line on the beat.
 *
 * A host stream that is OPEN and never calls back is the worst case this
 * subsystem has: the game drives real logic off play cursors -- a cutscene
 * advances when its stream reports itself finished -- so a callback that
 * never runs stops the product dead while every other counter looks healthy.
 * Measured in the browser, that is exactly what a wedged retail boot looked
 * like. The line prints once a device has been asked for, including the beat
 * where nothing moved, and says in words when the callback has never run.
 */
void dsound_audio_beat_report(void);
int dsound_selftest(void);
void dsound_movie_audio_begin(void);
void dsound_movie_audio_tick(void);

int dsound_buffer_is_playing(uint32_t guest);
unsigned dsound_buffer_release_guest(uint32_t guest);

#endif
