#include "x2_log.h"
/*
 * DirectSound 8 boundary used by XMen2.exe.
 *
 * This is a run-time-loaded module, not an import: FUN_00594290 loads
 * DSOUND.DLL, resolves DirectSoundCreate, creates the primary buffer and sets
 * its format. The game's mixer then creates/duplicates secondary buffers and
 * uses the ordinary IDirectSoundBuffer Lock/Unlock/Play/Stop cursor protocol.
 *
 * Numeric guest handles are COM objects in the guest heap; PCM and playback
 * state live here. DuplicateSoundBuffer shares the PCM allocation and keeps
 * independent cursor/control state, matching DirectSound object semantics.
 * One SDL3 F32 stereo stream mixes every playing secondary buffer. If a host
 * has no playback device, cursors still advance as a named SILENT device so a
 * headless run cannot deadlock in an audio poll while pretending sound exists.
 * Every interface method not implemented aborts by its published name.
 */
#include "dsound.h"
#include "dsound_mixer.h"
#include "guest_clock.h"
#include "guest_heap.h"
#include "guest_memory.h"
#include "movie_audio.h"
#include "win32_sdl.h"
#include "x86rt.h"
#include "x86rt_native.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif
#define A(i) RD32(C->reg[kX86pEsp] + 4u + (uint32_t)(i) * 4u)
#define THIS A(0)
#define DS_OK 0x00000000u
#define DSERR_INVALIDPARAM 0x8878000au
#define DSERR_OUTOFMEMORY 0x8007000eu
#define DSERR_INVALIDCALL 0x88780032u
#define DSBCAPS_PRIMARYBUFFER 0x00000001u
#define DSBPLAY_LOOPING 0x00000001u
#define DSBSTATUS_PLAYING 0x00000001u
#define DSBSTATUS_LOOPING 0x00000004u

typedef struct {
  uint32_t guest;
  unsigned refs;
} DSObject;

static DSObject g_ds;
static uint32_t g_ds_vtable, g_buf_vtable;
static unsigned long g_creates, g_secondary_created, g_duplicates;
static unsigned long g_buffer_plays, g_buffer_locks;
static unsigned long g_buffer_releases;
static uint32_t g_coop_hwnd, g_coop_level;

enum {
  DSVT_QueryInterface,
  DSVT_AddRef,
  DSVT_Release,
  DSVT_CreateSoundBuffer,
  DSVT_GetCaps,
  DSVT_DuplicateSoundBuffer,
  DSVT_SetCooperativeLevel,
  DSVT_Compact,
  DSVT_GetSpeakerConfig,
  DSVT_SetSpeakerConfig,
  DSVT_Initialize,
  DSVT_COUNT
};
static const char *const DS_NAME[DSVT_COUNT] = {
    "QueryInterface",      "AddRef",    "Release",
    "CreateSoundBuffer",   "GetCaps",   "DuplicateSoundBuffer",
    "SetCooperativeLevel", "Compact",   "GetSpeakerConfig",
    "SetSpeakerConfig",    "Initialize"};

enum {
  BVT_QueryInterface,
  BVT_AddRef,
  BVT_Release,
  BVT_GetCaps,
  BVT_GetCurrentPosition,
  BVT_GetFormat,
  BVT_GetVolume,
  BVT_GetPan,
  BVT_GetFrequency,
  BVT_GetStatus,
  BVT_Initialize,
  BVT_Lock,
  BVT_Play,
  BVT_SetCurrentPosition,
  BVT_SetFormat,
  BVT_SetVolume,
  BVT_SetPan,
  BVT_SetFrequency,
  BVT_Stop,
  BVT_Unlock,
  BVT_Restore,
  BVT_COUNT
};
static const char *const BVT_NAME[BVT_COUNT] = {"QueryInterface",
                                                "AddRef",
                                                "Release",
                                                "GetCaps",
                                                "GetCurrentPosition",
                                                "GetFormat",
                                                "GetVolume",
                                                "GetPan",
                                                "GetFrequency",
                                                "GetStatus",
                                                "Initialize",
                                                "Lock",
                                                "Play",
                                                "SetCurrentPosition",
                                                "SetFormat",
                                                "SetVolume",
                                                "SetPan",
                                                "SetFrequency",
                                                "Stop",
                                                "Unlock",
                                                "Restore"};

static void ret_std(CPU *C, uint32_t value, int nargs) {
  C->reg[kX86pEax] = value;
  C->reg[kX86pEsp] += 4u + (uint32_t)nargs * 4u;
}

static void ret_com(CPU *C, uint32_t value, int nargs) {
  ret_std(C, value, nargs + 1);
}

static DSBuffer *this_buffer(CPU *C) {
  DSBuffer *b = dsound_mixer_voice_of(THIS);
  if (!b) {
    x2_log_error("DSOUND: IDirectSoundBuffer method on unknown object "
                 "0x%08x\n",
                 THIS);
    abort();
  }
  return b;
}

void dsound_movie_audio_begin(void) { dsound_mixer_open_device(); }

void dsound_movie_audio_tick(void) { dsound_mixer_tick_silent(); }
static int read_waveformat(uint32_t p, DSBuffer *b) {
  if (!p)
    return 0;
  b->format_tag = RD16(p + 0u);
  b->channels = RD16(p + 2u);
  b->sample_rate = RD32(p + 4u);
  b->avg_bytes = RD32(p + 8u);
  b->block_align = RD16(p + 12u);
  b->bits = RD16(p + 14u);
  if (b->format_tag != 1 || (b->channels != 1 && b->channels != 2) ||
      (b->bits != 8 && b->bits != 16) || !b->sample_rate ||
      b->block_align != b->channels * b->bits / 8u) {
    x2_log_error("DSOUND: unsupported WAVEFORMATEX tag=%u channels=%u "
                 "rate=%u bits=%u align=%u\n",
                 b->format_tag, b->channels, b->sample_rate, b->bits,
                 b->block_align);
    return 0;
  }
  b->frequency = b->sample_rate;
  return 1;
}

static void write_waveformat(uint32_t p, const DSBuffer *b) {
  WR16(p + 0u, b->format_tag);
  WR16(p + 2u, b->channels);
  WR32(p + 4u, b->sample_rate);
  WR32(p + 8u, b->avg_bytes);
  WR16(p + 12u, b->block_align);
  WR16(p + 14u, b->bits);
  WR16(p + 16u, 0);
}

static DSBuffer *alloc_buffer(void) {
  DSBuffer *b = dsound_mixer_alloc_voice();
  if (!b) {
    return NULL;
  }
  b->guest = guest_malloc(8u);
  if (!b->guest) {
    dsound_mixer_free_voice(b);
    return NULL;
  }
  WR32(b->guest + 0u, g_buf_vtable);
  WR32(b->guest + 4u, 1u);
  return b;
}

static void b_unimplemented(CPU *C) {
  const char *name = (const char *)x86_callback_ctx();
  x2_log_error("DSOUND: IDirectSoundBuffer::%s is not implemented; "
               "refusing instead of returning plausible silence\n",
               name ? name : "(unknown)");
  (void)C;
  abort();
}

static void b_QueryInterface(CPU *C) {
  DSBuffer *b = this_buffer(C);
  if (A(2))
    WR32(A(2), b->guest);
  b->refs++;
  WR32(b->guest + 4u, b->refs);
  ret_com(C, DS_OK, 2);
}

static void b_AddRef(CPU *C) {
  DSBuffer *b = this_buffer(C);
  b->refs++;
  WR32(b->guest + 4u, b->refs);
  ret_com(C, b->refs, 0);
}

int dsound_buffer_is_playing(uint32_t guest) {
  DSBuffer *b = dsound_mixer_voice_of(guest);
  if (!b)
    return 0;
  dsound_mixer_tick_silent();
  return b->playing ? 1 : 0;
}

unsigned dsound_buffer_release_guest(uint32_t guest) {
  DSBuffer *b = dsound_mixer_voice_of(guest);
  if (!b)
    return 0;
  dsound_mixer_lock();
  unsigned n = b->refs ? --b->refs : 0;
  WR32(b->guest + 4u, n);
  if (!n) {
    if (g_buffer_releases < 8)
      x2_log_error("DSOUND: secondary object 0x%08x released to zero%s\n",
                   b->guest,
                   b->data && b->data->refs > 1
                       ? " (shared PCM remains through a duplicate)"
                       : "");
    g_buffer_releases++;
    dsound_mixer_free_voice(b);
  }
  dsound_mixer_unlock();
  return n;
}

static void b_Release(CPU *C) {
  ret_com(C, dsound_buffer_release_guest(THIS), 0);
}

static void b_GetCaps(CPU *C) {
  DSBuffer *b = this_buffer(C);
  uint32_t p = A(1), size = p ? RD32(p) : 0;
  if (!p || size < 20u) {
    ret_com(C, DSERR_INVALIDPARAM, 1);
    return;
  }
  WR32(p + 4u, b->flags);
  WR32(p + 8u, b->data ? b->data->bytes : 0u);
  WR32(p + 12u, 0u);
  WR32(p + 16u, 0u);
  ret_com(C, DS_OK, 1);
}

static void b_GetCurrentPosition(CPU *C) {
  DSBuffer *b = this_buffer(C);
  uint32_t play = A(1), write = A(2), pos = 0, bytes = 0;
  dsound_mixer_tick_silent();
  if (b->data && b->block_align) {
    bytes = b->data->bytes;
    pos = ((uint32_t)b->cursor_frames * b->block_align) % (bytes ? bytes : 1u);
  }
  if (play)
    WR32(play, pos);
  /* Writes hold SDL's stream lock from Lock through Unlock, so the exact
     first safe byte is the play cursor itself; there is no separate DMA
     cursor in this software mixer. */
  if (write)
    WR32(write, pos);
  ret_com(C, DS_OK, 2);
}

static void b_GetFormat(CPU *C) {
  DSBuffer *b = this_buffer(C);
  uint32_t p = A(1), bytes = A(2), wrote = A(3);
  if (wrote)
    WR32(wrote, 18u);
  if (p && bytes < 18u) {
    ret_com(C, DSERR_INVALIDPARAM, 3);
    return;
  }
  if (p)
    write_waveformat(p, b);
  ret_com(C, DS_OK, 3);
}

static void b_GetVolume(CPU *C) {
  DSBuffer *b = this_buffer(C);
  if (A(1))
    WR32(A(1), (uint32_t)b->volume);
  ret_com(C, DS_OK, 1);
}
static void b_GetPan(CPU *C) {
  DSBuffer *b = this_buffer(C);
  if (A(1))
    WR32(A(1), (uint32_t)b->pan);
  ret_com(C, DS_OK, 1);
}
static void b_GetFrequency(CPU *C) {
  DSBuffer *b = this_buffer(C);
  if (A(1))
    WR32(A(1), b->frequency);
  ret_com(C, DS_OK, 1);
}

static void b_GetStatus(CPU *C) {
  DSBuffer *b = this_buffer(C);
  uint32_t status;
  dsound_mixer_tick_silent();
  status = b->playing ? DSBSTATUS_PLAYING : 0u;
  if (b->playing && b->looping)
    status |= DSBSTATUS_LOOPING;
  if (A(1))
    WR32(A(1), status);
  ret_com(C, DS_OK, 1);
}

static void b_Lock(CPU *C) {
  DSBuffer *b = this_buffer(C);
  uint32_t off = A(1), bytes = A(2), p1 = A(3), n1 = A(4), p2 = A(5), n2 = A(6),
           flags = A(7);
  uint32_t total, first;
  (void)flags;
  dsound_mixer_lock();
  if (b->locked) {
    dsound_mixer_unlock();
    ret_com(C, DSERR_INVALIDCALL, 7);
    return;
  }
  if (!b->data || !b->data->bytes || off >= b->data->bytes) {
    dsound_mixer_unlock();
    ret_com(C, DSERR_INVALIDPARAM, 7);
    return;
  }
  total = bytes ? bytes : b->data->bytes;
  if (total > b->data->bytes)
    total = b->data->bytes;
  first = total;
  if (off + first > b->data->bytes)
    first = b->data->bytes - off;
  if (p1)
    WR32(p1, b->data->guest_data + off);
  if (n1)
    WR32(n1, first);
  if (p2)
    WR32(p2, total > first ? b->data->guest_data : 0u);
  if (n2)
    WR32(n2, total - first);
  b->locks++;
  g_buffer_locks++;
  b->locked = 1;
  /* The guest writes directly to the returned PCM range. Keep SDL's
     callback out until the matching Unlock makes those bytes visible. */
  ret_com(C, DS_OK, 7);
}

static void b_Play(CPU *C) {
  DSBuffer *b = this_buffer(C);
  dsound_mixer_lock();
  b->playing = 1;
  b->looping = (A(3) & DSBPLAY_LOOPING) != 0;
  b->plays++;
  g_buffer_plays++;
  dsound_mixer_unlock();
  ret_com(C, DS_OK, 3);
}

static void b_SetCurrentPosition(CPU *C) {
  DSBuffer *b = this_buffer(C);
  uint32_t byte = A(1);
  if (!b->data || byte >= b->data->bytes || !b->block_align) {
    ret_com(C, DSERR_INVALIDPARAM, 1);
    return;
  }
  dsound_mixer_lock();
  b->cursor_frames = byte / b->block_align;
  dsound_mixer_unlock();
  ret_com(C, DS_OK, 1);
}

static void b_SetFormat(CPU *C) {
  DSBuffer *b = this_buffer(C);
  if (!read_waveformat(A(1), b)) {
    ret_com(C, DSERR_INVALIDPARAM, 1);
    return;
  }
  if (b->primary) {
    dsound_mixer_set_rate((int)b->sample_rate);
    dsound_mixer_open_device();
  }
  ret_com(C, DS_OK, 1);
}

static void b_SetVolume(CPU *C) {
  DSBuffer *b = this_buffer(C);
  int32_t v = (int32_t)A(1);
  if (v > 0)
    v = 0;
  if (v < -10000)
    v = -10000;
  dsound_mixer_lock();
  b->volume = v;
  dsound_mixer_unlock();
  ret_com(C, DS_OK, 1);
}
static void b_SetPan(CPU *C) {
  DSBuffer *b = this_buffer(C);
  int32_t v = (int32_t)A(1);
  if (v > 10000)
    v = 10000;
  if (v < -10000)
    v = -10000;
  dsound_mixer_lock();
  b->pan = v;
  dsound_mixer_unlock();
  ret_com(C, DS_OK, 1);
}
static void b_SetFrequency(CPU *C) {
  DSBuffer *b = this_buffer(C);
  uint32_t v = A(1);
  if (!v)
    v = b->sample_rate;
  dsound_mixer_lock();
  b->frequency = v;
  dsound_mixer_unlock();
  ret_com(C, DS_OK, 1);
}
static void b_Stop(CPU *C) {
  DSBuffer *b = this_buffer(C);
  dsound_mixer_lock();
  b->playing = 0;
  dsound_mixer_unlock();
  ret_com(C, DS_OK, 0);
}
static void b_Unlock(CPU *C) {
  DSBuffer *b = this_buffer(C);
  if (!b->locked) {
    ret_com(C, DSERR_INVALIDCALL, 4);
    return;
  }
  b->locked = 0;
  dsound_mixer_unlock();
  ret_com(C, DS_OK, 4);
}
static void b_Restore(CPU *C) {
  (void)this_buffer(C);
  ret_com(C, DS_OK, 0);
}

static void ds_unimplemented(CPU *C) {
  const char *name = (const char *)x86_callback_ctx();
  x2_log_error("DSOUND: IDirectSound::%s is not implemented; refusing "
               "instead of disabling sound quietly\n",
               name ? name : "(unknown)");
  (void)C;
  abort();
}

static void ds_QueryInterface(CPU *C) {
  if (A(2))
    WR32(A(2), g_ds.guest);
  g_ds.refs++;
  WR32(g_ds.guest + 4, g_ds.refs);
  ret_com(C, DS_OK, 2);
}
static void ds_AddRef(CPU *C) {
  g_ds.refs++;
  WR32(g_ds.guest + 4, g_ds.refs);
  ret_com(C, g_ds.refs, 0);
}
static void ds_Release(CPU *C) {
  if (g_ds.refs)
    g_ds.refs--;
  WR32(g_ds.guest + 4, g_ds.refs);
  ret_com(C, g_ds.refs, 0);
}

static void ds_CreateSoundBuffer(CPU *C) {
  uint32_t desc = A(1), out = A(2), flags, bytes, fmt;
  DSBuffer *b;
  SampleData *data;
  if (!desc || RD32(desc) < 20u || !out) {
    ret_com(C, DSERR_INVALIDPARAM, 3);
    return;
  }
  flags = RD32(desc + 4u);
  bytes = RD32(desc + 8u);
  fmt = RD32(desc + 16u);
  dsound_mixer_lock();
  b = alloc_buffer();
  if (!b) {
    dsound_mixer_unlock();
    WR32(out, 0);
    ret_com(C, DSERR_OUTOFMEMORY, 3);
    return;
  }
  b->flags = flags;
  b->primary = (flags & DSBCAPS_PRIMARYBUFFER) != 0;
  if (!b->primary) {
    if (!bytes || !read_waveformat(fmt, b)) {
      x2_log_error("DSOUND: CreateSoundBuffer REFUSED desc=0x%08x "
                   "flags=0x%x bytes=%u format=0x%08x\n",
                   desc, flags, bytes, fmt);
      b->used = 0;
      dsound_mixer_unlock();
      WR32(out, 0);
      ret_com(C, DSERR_INVALIDPARAM, 3);
      return;
    }
    data = (SampleData *)calloc(1, sizeof *data);
    if (!data || !(data->guest_data = guest_malloc(bytes))) {
      free(data);
      b->used = 0;
      dsound_mixer_unlock();
      WR32(out, 0);
      ret_com(C, DSERR_OUTOFMEMORY, 3);
      return;
    }
    data->bytes = bytes;
    data->refs = 1;
    b->data = data;
    memset(guest_memory_pointer(data->guest_data), b->bits == 8 ? 0x80 : 0,
           bytes);
    g_secondary_created++;
    if (g_secondary_created <= 12)
      x2_log_error("DSOUND: secondary %lu -> object 0x%08x, out "
                   "0x%08x, %u bytes of %u Hz %u-bit %s PCM\n",
                   g_secondary_created, b->guest, out, bytes, b->sample_rate,
                   b->bits, b->channels == 1 ? "mono" : "stereo");
  }
  WR32(out, b->guest);
  dsound_mixer_unlock();
  ret_com(C, DS_OK, 3);
}

static void ds_GetCaps(CPU *C) {
  uint32_t p = A(1), size = p ? RD32(p) : 0;
  if (!p || size < 24u) {
    ret_com(C, DSERR_INVALIDPARAM, 1);
    return;
  }
  memset(guest_memory_pointer(p + 4u), 0, size - 4u);
  /* Primary/secondary mono+stereo, 8+16-bit, continuous rates. Mixing is
     software, so the hardware-buffer capacity fields remain zero. */
  WR32(p + 4u, 0x00000f1fu);
  WR32(p + 8u, 100u);
  WR32(p + 12u, 200000u);
  WR32(p + 16u, 1u);
  ret_com(C, DS_OK, 1);
}

static void ds_DuplicateSoundBuffer(CPU *C) {
  DSBuffer *src = dsound_mixer_voice_of(A(1)), *b;
  DSBuffer source;
  if (!src || src->primary || !A(2)) {
    ret_com(C, DSERR_INVALIDPARAM, 2);
    return;
  }
  dsound_mixer_lock();
  /* Allocating a voice may move the registry, so take the source by value
     rather than carrying a host pointer across the allocation. */
  source = *src;
  b = alloc_buffer();
  if (!b) {
    dsound_mixer_unlock();
    WR32(A(2), 0);
    ret_com(C, DSERR_OUTOFMEMORY, 2);
    return;
  }
  {
    uint32_t guest = b->guest;
    unsigned refs = b->refs;
    *b = source;
    b->guest = guest;
    b->refs = refs;
    b->used = 1;
    b->playing = 0;
    b->cursor_frames = 0;
    b->plays = 0;
    b->locks = 0;
    b->data->refs++;
  }
  WR32(b->guest, g_buf_vtable);
  WR32(b->guest + 4, b->refs);
  WR32(A(2), b->guest);
  dsound_mixer_unlock();
  g_duplicates++;
  if (g_duplicates <= 12) {
    x2_log_error("DSOUND: duplicate %lu of 0x%08x -> 0x%08x, out "
                 "0x%08x (shared PCM, independent cursor)\n",
                 g_duplicates, source.guest, b->guest, A(2));
  }
  ret_com(C, DS_OK, 2);
}

static void ds_SetCooperativeLevel(CPU *C) {
  g_coop_hwnd = A(1);
  g_coop_level = A(2);
  ret_com(C, DS_OK, 2);
}
static void ds_Compact(CPU *C) { ret_com(C, DS_OK, 0); }
static void ds_GetSpeakerConfig(CPU *C) {
  if (A(1))
    WR32(A(1), 4u);
  ret_com(C, DS_OK, 1);
}
static void ds_SetSpeakerConfig(CPU *C) { ret_com(C, DS_OK, 1); }
static void ds_Initialize(CPU *C) { ret_com(C, DS_OK, 1); }

static void build_vtables(void) {
  static void (*const ds_impl[DSVT_COUNT])(CPU *) = {
      ds_QueryInterface,      ds_AddRef,    ds_Release,
      ds_CreateSoundBuffer,   ds_GetCaps,   ds_DuplicateSoundBuffer,
      ds_SetCooperativeLevel, ds_Compact,   ds_GetSpeakerConfig,
      ds_SetSpeakerConfig,    ds_Initialize};
  static void (*const b_impl[BVT_COUNT])(CPU *) = {b_QueryInterface,
                                                   b_AddRef,
                                                   b_Release,
                                                   b_GetCaps,
                                                   b_GetCurrentPosition,
                                                   b_GetFormat,
                                                   b_GetVolume,
                                                   b_GetPan,
                                                   b_GetFrequency,
                                                   b_GetStatus,
                                                   b_unimplemented,
                                                   b_Lock,
                                                   b_Play,
                                                   b_SetCurrentPosition,
                                                   b_SetFormat,
                                                   b_SetVolume,
                                                   b_SetPan,
                                                   b_SetFrequency,
                                                   b_Stop,
                                                   b_Unlock,
                                                   b_Restore};
  int i;
  if (g_ds_vtable)
    return;
  g_ds_vtable = guest_malloc(DSVT_COUNT * 4u);
  g_buf_vtable = guest_malloc(BVT_COUNT * 4u);
  g_ds.guest = guest_malloc(8u);
  g_ds.refs = 1;
  if (!g_ds_vtable || !g_buf_vtable || !g_ds.guest) {
    x2_log_error("DSOUND: no guest memory for COM objects\n");
    abort();
  }
  for (i = 0; i < DSVT_COUNT; i++)
    WR32(g_ds_vtable + i * 4u,
         x86_native_callback(ds_impl[i] ? ds_impl[i] : ds_unimplemented,
                             "IDirectSound", DS_NAME[i], (void *)DS_NAME[i]));
  for (i = 0; i < BVT_COUNT; i++)
    WR32(g_buf_vtable + i * 4u,
         x86_native_callback(b_impl[i] ? b_impl[i] : b_unimplemented,
                             "IDirectSoundBuffer", BVT_NAME[i],
                             (void *)BVT_NAME[i]));
  WR32(g_ds.guest, g_ds_vtable);
  WR32(g_ds.guest + 4, g_ds.refs);
}

static void imp_DSOUND_DirectSoundCreate(CPU *C) {
  uint32_t out = A(1), outer = A(2);
  if (!out || outer) {
    if (out)
      WR32(out, 0);
    ret_std(C, DSERR_INVALIDPARAM, 3);
    return;
  }
  build_vtables();
  g_creates++;
  g_ds.refs++;
  WR32(g_ds.guest + 4, g_ds.refs);
  WR32(out, g_ds.guest);
  if (g_creates == 1)
    x2_log_error("DSOUND: DirectSoundCreate -> native IDirectSound at 0x%08x; "
                 "XMen2.exe FUN_00594290 owns this load path.\n",
                 g_ds.guest);
  ret_std(C, DS_OK, 3);
}

void dsound_install(void) {
  x86_native_export("DSOUND.DLL", "DirectSoundCreate",
                    imp_DSOUND_DirectSoundCreate);
}

void dsound_report(void) {
  static int done;
  DsoundMixerStats mixer;
  int live, playing;
  if (done++) {
    return;
  }
  dsound_mixer_voice_counts(&live, &playing);
  dsound_mixer_stats(&mixer);
  x2_log_info("  dsound: %lu DirectSoundCreate, %lu secondary buffer(s), %lu "
              "duplicate(s); %d live / %d playing, %lu Play, %lu Lock\n",
              g_creates, g_secondary_created, g_duplicates, live, playing,
              g_buffer_plays, g_buffer_locks);
  x2_log_info("          mixer: %lu callback(s), %lu frame(s), %lu nonzero "
              "sample(s), peak %.4f, %lu silent-clock advance(s)%s\n",
              mixer.callbacks, mixer.frames, mixer.nonzero, mixer.peak,
              mixer.silent_advances,
              mixer.silent ? " -- NO HOST AUDIO DEVICE" : "");
  movie_audio_report();
}

void dsound_audio_beat_report(void) {
  static unsigned long p_cb, p_frames, p_silent;
  DsoundMixerStats mixer;
  dsound_mixer_stats(&mixer);
  if (!mixer.attempted) {
    return;
  }
  x2_log_error("[HB]           audio: %s device, %lu mixer callback(s) (+%lu), "
               "%lu frame(s) (+%lu), %lu silent advance(s) (+%lu)\n",
               mixer.silent ? "timed SILENT" : "host", mixer.callbacks,
               mixer.callbacks - p_cb, mixer.frames, mixer.frames - p_frames,
               mixer.silent_advances, mixer.silent_advances - p_silent);
  if (!mixer.silent && mixer.callbacks == 0u) {
    x2_log_error("[HB]             the host stream is open and its callback "
                 "has NEVER run, so no play cursor is advancing and every "
                 "audio-gated wait in the guest is stopped\n");
  }
  x2_log_error("[HB]             movie clock: %.3fs played, %.3fs queued, "
               "%s\n",
               movie_audio_played_seconds(), movie_audio_queued_seconds(),
               movie_audio_active() ? "ACTIVE" : "idle");
  p_cb = mixer.callbacks;
  p_frames = mixer.frames;
  p_silent = mixer.silent_advances;
}

int dsound_selftest(void) { return dsound_mixer_selftest(); }
