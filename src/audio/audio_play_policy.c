#include "audio_play_policy.h"

static AudioPlayPolicySnapshot g_policy;

void audio_play_suppression_begin(void)
{
    g_policy.suppression_depth++;
}

void audio_play_suppression_end(void)
{
    if (g_policy.suppression_depth) g_policy.suppression_depth--;
}

int audio_play_policy_allow_start(void)
{
    if (g_policy.suppression_depth) {
        g_policy.suppressed_starts++;
        return 0;
    }
    g_policy.ordinary_starts++;
    return 1;
}

void audio_play_policy_snapshot(AudioPlayPolicySnapshot *out)
{
    if (out) *out = g_policy;
}
