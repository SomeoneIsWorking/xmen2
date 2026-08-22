#ifndef X2_FMV_POLICY_H
#define X2_FMV_POLICY_H

/* The native replacement deliberately covers the format actually shipped by
   this title, rather than pretending to be a general media player. */
int x2_fmv_codec_policy(const char *container, const char *video_codec,
                        const char *audio_codec);

#endif
