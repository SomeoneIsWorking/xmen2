#include "fmv_policy.h"

#include <string.h>

namespace x2::media {

int fmv_codec_policy(const char *container, const char *video_codec,
                     const char *audio_codec) {
  return container && video_codec && audio_codec &&
         strcmp(container, "mpeg") == 0 &&
         strcmp(video_codec, "mpeg1video") == 0 &&
         strcmp(audio_codec, "adpcm_adx") == 0;
}

} // namespace x2::media
