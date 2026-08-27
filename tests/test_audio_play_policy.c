#include "audio_play_policy.h"

#include <stdio.h>

int main(void)
{
    AudioPlayPolicySnapshot before, after;
    int failures = 0;

    audio_play_policy_snapshot(&before);
    failures += !audio_play_policy_allow_start();
    audio_play_suppression_begin();
    audio_play_suppression_begin();
    failures += audio_play_policy_allow_start();
    audio_play_suppression_end();
    failures += audio_play_policy_allow_start();
    audio_play_suppression_end();
    failures += !audio_play_policy_allow_start();
    audio_play_suppression_end();
    audio_play_policy_snapshot(&after);

    failures += after.ordinary_starts != before.ordinary_starts + 2u;
    failures += after.suppressed_starts != before.suppressed_starts + 2u;
    failures += after.suppression_depth != 0u;
    printf("audio play policy: %s -- nested cutscene scope suppresses new "
           "starts and ordinary playback remains enabled\n",
           failures ? "FAILED" : "PASSED");
    return failures != 0;
}
