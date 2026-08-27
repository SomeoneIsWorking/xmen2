#ifndef X2_AUDIO_PLAY_POLICY_H
#define X2_AUDIO_PLAY_POLICY_H

typedef struct AudioPlayPolicySnapshot {
    unsigned long ordinary_starts;
    unsigned long suppressed_starts;
    unsigned suppression_depth;
} AudioPlayPolicySnapshot;

/* Suppress only new playback starts. Existing ambient/gameplay voices retain
 * their state; the cutscene player separately stops its active dialogue. */
void audio_play_suppression_begin(void);
void audio_play_suppression_end(void);
int audio_play_policy_allow_start(void);
void audio_play_policy_snapshot(AudioPlayPolicySnapshot *out);

#endif
