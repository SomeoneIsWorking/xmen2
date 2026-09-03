/*
 * `audio.adpcm_verify` -- the differential gate for the native ADPCM overrides
 * in audio_adpcm.c.
 *
 * When the flag is set, every native decode is followed by running the guest's
 * own body from the same start state; any difference in the decoded output or
 * the written-back predictor/step-index state aborts the run. This is the
 * proof-on-real-data check to run once against actual game audio streams; it
 * needs the execution engine, which is why it is not in audio_adpcm.c (that
 * file and its unit test stay engine-free).
 */
#include "audio_adpcm.h"

#include "guest_body.h"
#include "x86rt.h"

#include <lucent/cvar_c.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int verify_enabled(void) {
  static int cached = -1;
  if (cached < 0)
    cached = lucent_cvar_flag("audio.adpcm_verify", 0) ? 1 : 0;
  return cached;
}

void audio_adpcm_verify_or_abort(const CPU *C, uint32_t ep, uint32_t out,
                                 uint32_t out_bytes, uint32_t pp, uint32_t ip,
                                 const int32_t *pred0, const int32_t *idx0,
                                 const int32_t *native_pred,
                                 const int32_t *native_idx, int channels) {
  if (!verify_enabled())
    return;

  uint8_t *mine = malloc(out_bytes ? out_bytes : 1u);
  if (!mine)
    return;
  for (uint32_t k = 0; k < out_bytes; k++)
    mine[k] = (uint8_t)RD8(out + k);

  for (int c = 0; c < channels; c++) {
    WR32(pp + (uint32_t)(c * 4), (uint32_t)pred0[c]);
    WR32(ip + (uint32_t)(c * 4), (uint32_t)idx0[c]);
  }

  CPU guest = *C;
  x86_guest_body(&guest, "XMen2.exe", ep);

  int bad = 0;
  for (int c = 0; c < channels; c++)
    if ((int32_t)RD32(pp + (uint32_t)(c * 4)) != native_pred[c] ||
        (int32_t)RD32(ip + (uint32_t)(c * 4)) != native_idx[c])
      bad = 1;
  for (uint32_t k = 0; k < out_bytes && !bad; k++)
    if ((uint8_t)RD8(out + k) != mine[k])
      bad = 1;

  if (bad) {
    fprintf(stderr,
            "audio.adpcm_verify: native decode of XMen2.exe 0x%08x disagrees "
            "with the guest body (%u output byte(s), %d channel(s)). The "
            "native ADPCM decoder is wrong; not continuing.\n",
            ep, out_bytes, channels);
    abort();
  }
  free(mine);
}
