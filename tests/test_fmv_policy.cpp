#include "fmv_policy.h"

#include <stdio.h>

int main(void) {
  int failures = 0;
  failures += !x2_fmv_codec_policy("mpeg", "mpeg1video", "adpcm_adx");
  failures += x2_fmv_codec_policy("avi", "mpeg1video", "adpcm_adx");
  failures += x2_fmv_codec_policy("mpeg", "mpeg2video", "adpcm_adx");
  failures += x2_fmv_codec_policy("mpeg", "mpeg1video", "aac");
  failures += x2_fmv_codec_policy(NULL, "mpeg1video", "adpcm_adx");
  printf("FMV codec policy: %s -- only the shipped MPEG-PS/MPEG-1/ADX "
         "combination is accepted\n",
         failures ? "FAILED" : "PASSED");
  return failures != 0;
}
