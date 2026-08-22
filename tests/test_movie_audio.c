#include "movie_audio.h"

#include <math.h>
#include <stdio.h>

int main(void)
{
    const float input[] = { 0.25f, -0.25f, 0.5f, -0.5f,
                            0.75f, -0.75f, 1.0f, -1.0f };
    float output[8] = { 0 };
    int failures = 0;
    failures += !movie_audio_open(4);
    failures += !movie_audio_queue(input, 4);
    movie_audio_play();
    movie_audio_mix(output, 2, 4);
    failures += fabs(output[0] - 0.25f) > 0.0001f;
    failures += fabs(output[3] + 0.5f) > 0.0001f;
    failures += fabs(movie_audio_played_seconds() - 0.5) > 0.0001;
    failures += fabs(movie_audio_queued_seconds() - 0.5) > 0.0001;
    movie_audio_pause(1);
    movie_audio_mix(output + 4, 2, 4);
    failures += fabs(movie_audio_played_seconds() - 0.5) > 0.0001;
    movie_audio_close();
    failures += movie_audio_active();
    printf("movie audio: %s -- the production queue mixes, clocks, pauses, "
           "and closes one streaming stereo voice\n",
           failures ? "FAILED" : "PASSED");
    return failures != 0;
}
