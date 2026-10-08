#pragma once

namespace x2::media {

/* The native replacement deliberately covers the format actually shipped by
   this title, rather than pretending to be a general media player. */
int fmv_codec_policy(const char *container, const char *video_codec,
                     const char *audio_codec);

} // namespace x2::media
