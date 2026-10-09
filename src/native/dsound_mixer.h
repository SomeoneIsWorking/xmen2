#pragma once

#include <cstdint>

/*
 * The voice registry, the software mixer and the host playback stream.
 *
 * DSOUND.DLL's COM surface is a translation layer: it turns guest calls into
 * voice state. What that state MEANS -- how a cursor advances, how two voices
 * that share one PCM allocation stay independent, when a one-shot stops, and
 * where the frames go -- belongs here, with the SDL stream that pulls them.
 *
 * A voice is allocated without a guest object: the caller binds one. That
 * keeps every COM detail (the vtable pointer, the guest refcount word) on the
 * other side of this boundary, so the mixer can be exercised without a guest.
 */

namespace x2::native {

struct SampleData {
  uint32_t guest_data;
  uint32_t bytes;
  unsigned refs;
};

struct DSBuffer {
  int used, primary;
  uint32_t guest;
  unsigned refs;
  uint32_t flags;
  uint16_t format_tag, channels, block_align, bits;
  uint32_t sample_rate, avg_bytes;
  SampleData *data;
  double cursor_frames;
  uint32_t frequency;
  int32_t volume, pan;
  int playing, looping, locked;
  unsigned long plays, locks;
};

/* What the mixer has actually done. `attempted` says a device was asked for
   at all, so a report can tell "never opened" from "open and silent". */
struct DsoundMixerStats {
  unsigned long callbacks, frames, nonzero, silent_advances;
  float peak;
  int attempted, silent;
};

void dsound_mixer_open_device(void);
/* Advance every cursor on the wall clock when the device is the timed SILENT
   one. A no-op with a real host stream, whose callback does the advancing. */
void dsound_mixer_tick_silent(void);

void dsound_mixer_lock(void);
void dsound_mixer_unlock(void);

int dsound_mixer_rate(void);
void dsound_mixer_set_rate(int hz);

/* An unbound voice: used, one reference, full volume, no guest object. */
DSBuffer *dsound_mixer_alloc_voice(void);
DSBuffer *dsound_mixer_voice_of(uint32_t guest);
/* Drop this voice's PCM reference and return it to the free list. Call under
   the mixer lock: the callback walks the same array. */
void dsound_mixer_free_voice(DSBuffer *b);
void dsound_mixer_voice_counts(int *live, int *playing);

void dsound_mixer_stats(DsoundMixerStats *out);
int dsound_mixer_selftest(void);

} // namespace x2::native
