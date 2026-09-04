/*
 * Native override for the retail per-frame audio channel poll,
 * XMen2.exe!0x00594500 (tail-called from the audio service tick
 * FUN_0058f9a0).
 *
 * The retail body walks a fixed table of 24 sound channels
 * (0x00804198, 16 bytes each). For every channel in state 2 ("started,
 * awaiting completion") it calls IDirectSoundBuffer::GetStatus through
 * the guest vtable, and when the buffer is no longer playing it either
 * releases the owned buffer and frees the slot, or (for a non-owning
 * channel) drops the channel back to state 1.
 *
 * Under the JIT this was ~2.5% of in-game block-entry weight (issue
 * #141, profile blocks 0x00594524/0x00594578): the 24-iteration loop is
 * re-translated every frame and each GetStatus call is a guest->host
 * COM crossing. Our DirectSound lives in src/native/dsound.c, so the
 * status check and the release are just C calls -- dsound_buffer_is_playing
 * and dsound_buffer_release_guest -- and the whole loop runs natively
 * with no crossing and no re-translated blocks.
 *
 * Verified against the guest body by audio_channel_poll_verify.c
 * (audio.channel_poll_verify=1) and by tests/test_audio_channel_poll.c.
 */
#include "audio_channel_poll.h"
#include "audio_channel_poll_verify.h"

#include "dsound.h"
#include "guest_body.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <lucent/cvar_c.h>

/* Retail data layout, from the disassembly of 0x00594500. */
enum {
  ACP_GUARD = 0x0080431cu,   /* recursion guard; the poll no-ops when != 0 */
  ACP_COUNTER = 0x00804330u, /* free-running poll counter, bumped each run */
  ACP_CHAN_BASE = 0x00804198u,
  ACP_CHAN_STRIDE = 0x10u,
  ACP_CHAN_COUNT = 24,
  ACP_SLOT_LO = 0x00804098u, /* [2*slot]   -> 0 when the slot is freed */
  ACP_SLOT_HI = 0x00804099u, /* [2*slot]   -> 0xff when the slot is freed */
};

/* Channel record fields, relative to ACP_CHAN_BASE + i*ACP_CHAN_STRIDE. */
enum {
  CH_STATE = 0x00u, /* 0 free, 1 idle, 2 awaiting completion */
  CH_FLAGS = 0x01u, /* bit0: this channel owns (must Release) the buffer */
  CH_SLOT = 0x03u,  /* index into the 0x00804098 slot table */
  CH_OBJ = 0x0cu,   /* guest IDirectSoundBuffer pointer */
};

#if defined(TEST_SUITE)
static int channel_poll_enabled(void) { return 1; }
#else
static int channel_poll_enabled(void) {
  static int cached = -1;
  if (__builtin_expect(cached < 0, 0))
    cached = lucent_cvar_flag("audio.channel_poll", 1) ? 1 : 0;
  return cached;
}
#endif

void audio_channel_poll_run(void) {
  if (RD32(ACP_GUARD) != 0u)
    return;
  WR32(ACP_COUNTER, RD32(ACP_COUNTER) + 1u);

  for (int i = 0; i < ACP_CHAN_COUNT; ++i) {
    const uint32_t rec = ACP_CHAN_BASE + (uint32_t)i * ACP_CHAN_STRIDE;
    if (RD8(rec + CH_STATE) != 2u)
      continue;

    const uint32_t obj = RD32(rec + CH_OBJ);
    if (dsound_buffer_is_playing(obj))
      continue;

    if (RD8(rec + CH_FLAGS) & 1u) {
      if (obj) {
        dsound_buffer_release_guest(obj);
        WR32(rec + CH_OBJ, 0u);
      }
      const uint32_t slot = (uint32_t)RD8(rec + CH_SLOT) << 1;
      WR8(ACP_SLOT_HI + slot, 0xffu);
      WR8(rec + CH_STATE, 0u);
      WR8(ACP_SLOT_LO + slot, 0u);
    } else {
      WR8(rec + CH_STATE, 1u);
    }
  }
}

void x2_override_00594500(CPU *C) {
  if (__builtin_expect(!channel_poll_enabled(), 0)) {
    x86_guest_body(C, "XMen2.exe", 0x00594500u);
    return;
  }

  if (audio_channel_poll_verify(C)) {
    C->reg[kX86pEax] = 0u;
    C->reg[kX86pEsp] += 4u;
    return;
  }

  audio_channel_poll_run();
  C->reg[kX86pEax] = 0u;
  C->reg[kX86pEsp] +=
      4u; /* __cdecl, no args: consume only the return address */
}

__attribute__((constructor)) static void register_audio_channel_poll(void) {
  x86_register_override("XMen2.exe", 0x00594500u, x2_override_00594500);
}
