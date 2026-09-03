#ifndef X2_AUDIO_ADPCM_H
#define X2_AUDIO_ADPCM_H

#include "x86rt.h"

#include <stdint.h>

/*
 * IMA ADPCM decode, the algorithm XMen2.exe links at 0x00616770 (mono) and
 * 0x00616880 (stereo, low nibble = left, high nibble = right, one byte per
 * frame, interleaved output). Both are the standard IMA scheme with the
 * canonical 89-entry step table and {-1,-1,-1,-1,2,4,6,8} index table the game
 * carries at 0x006e95a8 / 0x006e9588.
 *
 * The JIT translated their pure-integer bodies and ran them one instruction at
 * a time through the interpreter helper; the in-game block profile (issue
 * #141) put the two loops at ~6% of guest wall time combined.
 */

/*
 * Advance one nibble: updates *predictor (clamped to int16 range) and
 * *step_index (clamped to 0..88), returns the new predictor. The shared core
 * both native overrides and the reference decoder in the test use. *step_index
 * must be in 0..88 on entry -- every writer clamps it, so this is an invariant,
 * asserted not defended.
 */
int32_t ima_adpcm_step(uint32_t nibble, int32_t *predictor,
                       int32_t *step_index);

/* XMen2.exe!0x00616770 -- void(int16_t *out, const uint8_t *in, int count,
   int *predictor, int *step_index), __cdecl. */
void x2_override_00616770(CPU *C);

/* XMen2.exe!0x00616880 -- void(int16_t *out, const uint8_t *in, int frames,
   int predictor[2], int step_index[2]), __cdecl. */
void x2_override_00616880(CPU *C);

/*
 * When `audio.adpcm_verify` is set: re-run the guest body at `ep` from the
 * saved start state (`pred0`/`idx0`, `channels` ints each at `pp`/`ip`) and
 * abort if the `out_bytes` of output or the written-back state differ from the
 * native results just produced (`native_pred`/`native_idx`). A no-op otherwise.
 * Lives in audio_adpcm_verify.c -- it needs the engine, which the decode path
 * and its unit test do not.
 */
void audio_adpcm_verify_or_abort(const CPU *C, uint32_t ep, uint32_t out,
                                 uint32_t out_bytes, uint32_t pp, uint32_t ip,
                                 const int32_t *pred0, const int32_t *idx0,
                                 const int32_t *native_pred,
                                 const int32_t *native_idx, int channels);

#endif
