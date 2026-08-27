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
#include "guest_clock.h"
#include "movie_audio.h"
#include "x86rt.h"
#include "x86rt_native.h"
#include "guest_heap.h"
#include "guest_memory.h"
#include "win32_sdl.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif

#define A(i) RD32(C->esp + 4u + (uint32_t)(i) * 4u)
#define THIS A(0)

#define DS_OK                 0x00000000u
#define DSERR_INVALIDPARAM    0x8878000au
#define DSERR_OUTOFMEMORY     0x8007000eu
#define DSERR_INVALIDCALL     0x88780032u

#define DSBCAPS_PRIMARYBUFFER 0x00000001u
#define DSBPLAY_LOOPING       0x00000001u
#define DSBSTATUS_PLAYING     0x00000001u
#define DSBSTATUS_LOOPING     0x00000004u

typedef struct SampleData {
    uint32_t guest_data;
    uint32_t bytes;
    unsigned refs;
} SampleData;

typedef struct DSBuffer {
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
} DSBuffer;

typedef struct {
    uint32_t guest;
    unsigned refs;
} DSObject;

static DSObject g_ds;
static DSBuffer *g_buf;
static int g_nbuf, g_capbuf;
static uint32_t g_ds_vtable, g_buf_vtable;
static unsigned long g_creates, g_secondary_created, g_duplicates;
static unsigned long g_mix_callbacks, g_mix_frames, g_silent_advances;
static unsigned long g_mix_nonzero, g_buffer_plays, g_buffer_locks;
static unsigned long g_buffer_releases;
static float g_mix_peak;
static uint32_t g_coop_hwnd, g_coop_level;
static int g_primary_rate = 22050;
static int g_audio_attempted, g_audio_silent;
static double g_silent_time;

#ifdef X2_WITH_SDL
static SDL_AudioStream *g_stream;
static float *g_mix_scratch;
static int g_mix_scratch_frames;
#endif

enum {
    DSVT_QueryInterface, DSVT_AddRef, DSVT_Release,
    DSVT_CreateSoundBuffer, DSVT_GetCaps, DSVT_DuplicateSoundBuffer,
    DSVT_SetCooperativeLevel, DSVT_Compact, DSVT_GetSpeakerConfig,
    DSVT_SetSpeakerConfig, DSVT_Initialize, DSVT_COUNT
};
static const char *const DS_NAME[DSVT_COUNT] = {
    "QueryInterface", "AddRef", "Release", "CreateSoundBuffer", "GetCaps",
    "DuplicateSoundBuffer", "SetCooperativeLevel", "Compact",
    "GetSpeakerConfig", "SetSpeakerConfig", "Initialize"
};

enum {
    BVT_QueryInterface, BVT_AddRef, BVT_Release, BVT_GetCaps,
    BVT_GetCurrentPosition, BVT_GetFormat, BVT_GetVolume, BVT_GetPan,
    BVT_GetFrequency, BVT_GetStatus, BVT_Initialize, BVT_Lock, BVT_Play,
    BVT_SetCurrentPosition, BVT_SetFormat, BVT_SetVolume, BVT_SetPan,
    BVT_SetFrequency, BVT_Stop, BVT_Unlock, BVT_Restore, BVT_COUNT
};
static const char *const BVT_NAME[BVT_COUNT] = {
    "QueryInterface", "AddRef", "Release", "GetCaps",
    "GetCurrentPosition", "GetFormat", "GetVolume", "GetPan",
    "GetFrequency", "GetStatus", "Initialize", "Lock", "Play",
    "SetCurrentPosition", "SetFormat", "SetVolume", "SetPan",
    "SetFrequency", "Stop", "Unlock", "Restore"
};

/* The guest's clock, not a private one: see guest_clock.h. Five copies of
   this read CLOCK_MONOTONIC directly, and the guest gates real logic on
   elapsed time, so any two of them disagreeing is a timing bug wearing a
   gameplay bug's clothes. */
static double now_s(void) { return guest_clock_now_s(); }

static void ret_std(CPU *C, uint32_t value, int nargs)
{
    C->eax = value;
    C->esp += 4u + (uint32_t)nargs * 4u;
}

static void ret_com(CPU *C, uint32_t value, int nargs)
{
    ret_std(C, value, nargs + 1);
}

static DSBuffer *buffer_of(uint32_t guest)
{
    int i;
    for (i = 0; i < g_nbuf; ++i)
        if (g_buf[i].used && g_buf[i].guest == guest) return &g_buf[i];
    return NULL;
}

static DSBuffer *this_buffer(CPU *C)
{
    DSBuffer *b = buffer_of(THIS);
    if (!b) {
        fprintf(stderr, "DSOUND: IDirectSoundBuffer method on unknown object "
                        "0x%08x\n", THIS);
        abort();
    }
    return b;
}

static void audio_lock(void)
{
#ifdef X2_WITH_SDL
    if (g_stream && SDL_WasInit(SDL_INIT_AUDIO)) SDL_LockAudioStream(g_stream);
#endif
}

static void audio_unlock(void)
{
#ifdef X2_WITH_SDL
    if (g_stream && SDL_WasInit(SDL_INIT_AUDIO)) SDL_UnlockAudioStream(g_stream);
#endif
}

static float sample_at(const DSBuffer *b, uint64_t frame, int channel)
{
    const unsigned char *p;
    uint64_t frames;
    int srcch;
    if (!b->data || !b->data->guest_data || !b->block_align) return 0.0f;
    frames = b->data->bytes / b->block_align;
    if (!frames) return 0.0f;
    frame %= frames;
    srcch = b->channels == 1 ? 0 : channel;
    if (srcch >= b->channels) srcch = b->channels - 1;
    p = (const unsigned char *)guest_memory_const_pointer(b->data->guest_data)
        + frame * b->block_align + (uint64_t)srcch * (b->bits / 8u);
    if (b->bits == 8) return ((float)p[0] - 128.0f) / 128.0f;
    if (b->bits == 16) {
        int16_t s;
        memcpy(&s, p, sizeof s);
        return (float)s / 32768.0f;
    }
    return 0.0f;
}

static void advance_buffer(DSBuffer *b, double out_frames, double out_rate,
                           float *mix)
{
    uint64_t nsrc;
    double step;
    float gain, gl, gr;
    int i;
    if (!b->playing || b->primary || !b->data || !b->block_align) return;
    nsrc = b->data->bytes / b->block_align;
    if (!nsrc) { b->playing = 0; return; }
    step = (double)(b->frequency ? b->frequency : b->sample_rate) / out_rate;
    gain = b->volume <= -10000 ? 0.0f : powf(10.0f, (float)b->volume / 2000.0f);
    gl = gr = gain;
    if (b->pan > 0) gl *= powf(10.0f, -(float)b->pan / 2000.0f);
    if (b->pan < 0) gr *= powf(10.0f,  (float)b->pan / 2000.0f);
    for (i = 0; i < (int)out_frames; ++i) {
        uint64_t pos = (uint64_t)b->cursor_frames;
        if (pos >= nsrc) {
            if (!b->looping) { b->playing = 0; break; }
            b->cursor_frames = fmod(b->cursor_frames, (double)nsrc);
            pos = (uint64_t)b->cursor_frames;
        }
        if (mix) {
            uint64_t next = pos + 1u;
            float frac = (float)(b->cursor_frames - (double)pos);
            float l0, l1, r0, r1;
            if (next >= nsrc) next = b->looping ? 0u : pos;
            l0 = sample_at(b, pos, 0); l1 = sample_at(b, next, 0);
            r0 = sample_at(b, pos, 1); r1 = sample_at(b, next, 1);
            mix[i * 2 + 0] += (l0 + (l1 - l0) * frac) * gl;
            mix[i * 2 + 1] += (r0 + (r1 - r0) * frac) * gr;
        }
        b->cursor_frames += step;
    }
}

static void mix_frames(float *mix, int frames, int rate)
{
    int i;
    if (mix) memset(mix, 0, (size_t)frames * 2u * sizeof(float));
    for (i = 0; i < g_nbuf; ++i)
        if (g_buf[i].used) advance_buffer(&g_buf[i], frames, rate, mix);
    movie_audio_mix(mix, frames, rate);
    if (mix) {
        for (i = 0; i < frames * 2; ++i) {
            if (mix[i] > 1.0f) mix[i] = 1.0f;
            if (mix[i] < -1.0f) mix[i] = -1.0f;
            if (fabsf(mix[i]) > g_mix_peak) g_mix_peak = fabsf(mix[i]);
            if (mix[i] != 0.0f) g_mix_nonzero++;
        }
    }
}
#ifdef X2_WITH_SDL
static void SDLCALL audio_more(void *userdata, SDL_AudioStream *stream,
                               int additional_amount, int total_amount)
{
    int frames = additional_amount / (int)(2u * sizeof(float));
    float *mix;
    (void)userdata; (void)total_amount;
    if (frames <= 0) return;
    if (frames > g_mix_scratch_frames) {
        float *next = (float *)realloc(g_mix_scratch,
                                      (size_t)frames * 2u * sizeof(float));
        if (!next) return;
        g_mix_scratch = next;
        g_mix_scratch_frames = frames;
    }
    mix = g_mix_scratch;
    mix_frames(mix, frames, g_primary_rate);
    SDL_PutAudioStreamData(stream, mix, frames * 2 * (int)sizeof(float));
    g_mix_callbacks++;
    g_mix_frames += (unsigned long)frames;
}
#endif
static void open_audio(void)
{
    if (g_audio_attempted) return;
    g_audio_attempted = 1;
    /*
     * A run with no window is a run nobody is listening to: an automated or
     * observational run should not seize the machine's speakers and talk over
     * whatever the user is actually doing.
     *
     * This takes the SILENT-BUT-TIMED device rather than skipping audio, and
     * the difference matters. The game drives real logic off buffer play
     * cursors -- a cutscene advances when its stream reports itself finished --
     * so a device whose cursors never move does not make the run quiet, it
     * makes the run hang. The silent device below advances every cursor on the
     * wall clock at the buffer's own rate, so the guest sees audio complete on
     * schedule and hears nothing.
     */
    if (win32_sdl_windows_hidden()) {
        fprintf(stderr, "DSOUND: --no-window, so no host playback device is "
                        "opened -- using the timed SILENT device. Play cursors "
                        "still advance at %d Hz, so audio-gated logic (cutscene "
                        "advance, stream-complete waits) runs exactly as it "
                        "does with sound.\n", g_primary_rate);
        g_audio_silent = 1;
        g_silent_time = now_s();
        return;
    }
#ifdef X2_WITH_SDL
    {
        SDL_AudioSpec spec;
        if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            fprintf(stderr, "DSOUND: SDL audio init failed: %s -- using a "
                            "timed SILENT device; cursors still advance.\n",
                    SDL_GetError());
            g_audio_silent = 1;
            g_silent_time = now_s();
            return;
        }
        SDL_zero(spec);
        spec.format = SDL_AUDIO_F32;
        spec.channels = 2;
        spec.freq = g_primary_rate;
        g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                             &spec, audio_more, NULL);
        if (!g_stream || !SDL_ResumeAudioStreamDevice(g_stream)) {
            fprintf(stderr, "DSOUND: no host playback stream: %s -- using a "
                            "timed SILENT device; cursors still advance.\n",
                    SDL_GetError());
            if (g_stream) SDL_DestroyAudioStream(g_stream);
            g_stream = NULL;
            g_audio_silent = 1;
            g_silent_time = now_s();
            return;
        }
        fprintf(stderr, "DSOUND: SDL3 playback opened at %d Hz, stereo F32; "
                        "the game supplies PCM through DirectSound buffers.\n",
                g_primary_rate);
    }
#else
    fprintf(stderr, "DSOUND: built without SDL audio -- using a timed SILENT "
                    "device; cursors still advance.\n");
    g_audio_silent = 1;
    g_silent_time = now_s();
#endif
}
void dsound_movie_audio_begin(void) { open_audio(); }
static void silent_advance(void)
{
    double t, elapsed;
    if (!g_audio_silent) return;
    t = now_s();
    elapsed = t - g_silent_time;
    if (elapsed <= 0.0) return;
    g_silent_time = t;
    mix_frames(NULL, (int)(elapsed * g_primary_rate), g_primary_rate);
    g_silent_advances++;
}
void dsound_movie_audio_tick(void) { silent_advance(); }
static int read_waveformat(uint32_t p, DSBuffer *b)
{
    if (!p) return 0;
    b->format_tag = RD16(p + 0u);
    b->channels = RD16(p + 2u);
    b->sample_rate = RD32(p + 4u);
    b->avg_bytes = RD32(p + 8u);
    b->block_align = RD16(p + 12u);
    b->bits = RD16(p + 14u);
    if (b->format_tag != 1 || (b->channels != 1 && b->channels != 2)
            || (b->bits != 8 && b->bits != 16) || !b->sample_rate
            || b->block_align != b->channels * b->bits / 8u) {
        fprintf(stderr, "DSOUND: unsupported WAVEFORMATEX tag=%u channels=%u "
                        "rate=%u bits=%u align=%u\n", b->format_tag,
                b->channels, b->sample_rate, b->bits, b->block_align);
        return 0;
    }
    b->frequency = b->sample_rate;
    return 1;
}

static void write_waveformat(uint32_t p, const DSBuffer *b)
{
    WR16(p + 0u, b->format_tag);
    WR16(p + 2u, b->channels);
    WR32(p + 4u, b->sample_rate);
    WR32(p + 8u, b->avg_bytes);
    WR16(p + 12u, b->block_align);
    WR16(p + 14u, b->bits);
    WR16(p + 16u, 0);
}

static DSBuffer *alloc_buffer(void)
{
    int i;
    for (i = 0; i < g_nbuf; ++i) if (!g_buf[i].used) {
        DSBuffer *b = &g_buf[i];
        memset(b, 0, sizeof *b);
        b->guest = guest_malloc(8u);
        if (!b->guest) return NULL;
        b->used = 1;
        b->refs = 1;
        b->volume = 0;
        WR32(b->guest + 0u, g_buf_vtable);
        WR32(b->guest + 4u, 1u);
        return b;
    }
    if (g_nbuf == g_capbuf) {
        int cap = g_capbuf ? g_capbuf * 2 : 64;
        DSBuffer *next = (DSBuffer *)realloc(g_buf, (size_t)cap * sizeof *next);
        if (!next) return NULL;
        memset(next + g_capbuf, 0, (size_t)(cap - g_capbuf) * sizeof *next);
        g_buf = next;
        g_capbuf = cap;
    }
    {
        DSBuffer *b = &g_buf[g_nbuf++];
        memset(b, 0, sizeof *b);
        b->guest = guest_malloc(8u);
        if (!b->guest) { g_nbuf--; return NULL; }
        b->used = 1;
        b->refs = 1;
        b->volume = 0;
        WR32(b->guest + 0u, g_buf_vtable);
        WR32(b->guest + 4u, 1u);
        return b;
    }
}

static void b_unimplemented(CPU *C)
{
    const char *name = (const char *)x86_callback_ctx();
    fprintf(stderr, "DSOUND: IDirectSoundBuffer::%s is not implemented; "
                    "refusing instead of returning plausible silence\n",
            name ? name : "(unknown)");
    (void)C;
    abort();
}

static void b_QueryInterface(CPU *C)
{
    DSBuffer *b = this_buffer(C);
    if (A(2)) WR32(A(2), b->guest);
    b->refs++;
    WR32(b->guest + 4u, b->refs);
    ret_com(C, DS_OK, 2);
}

static void b_AddRef(CPU *C)
{
    DSBuffer *b = this_buffer(C);
    b->refs++;
    WR32(b->guest + 4u, b->refs);
    ret_com(C, b->refs, 0);
}

static void b_Release(CPU *C)
{
    DSBuffer *b = this_buffer(C);
    unsigned n;
    audio_lock();
    n = b->refs ? --b->refs : 0;
    WR32(b->guest + 4u, n);
    if (!n) {
        if (g_buffer_releases < 8)
            fprintf(stderr, "DSOUND: secondary object 0x%08x released to "
                            "zero%s\n", b->guest,
                    b->data && b->data->refs > 1 ?
                    " (shared PCM remains through a duplicate)" : "");
        g_buffer_releases++;
        if (b->data && --b->data->refs == 0) {
            if (b->data->guest_data) guest_free(b->data->guest_data);
            free(b->data);
        }
        b->playing = 0;
        b->used = 0;
    }
    audio_unlock();
    ret_com(C, n, 0);
}

static void b_GetCaps(CPU *C)
{
    DSBuffer *b = this_buffer(C);
    uint32_t p = A(1), size = p ? RD32(p) : 0;
    if (!p || size < 20u) { ret_com(C, DSERR_INVALIDPARAM, 1); return; }
    WR32(p + 4u, b->flags);
    WR32(p + 8u, b->data ? b->data->bytes : 0u);
    WR32(p + 12u, 0u);
    WR32(p + 16u, 0u);
    ret_com(C, DS_OK, 1);
}

static void b_GetCurrentPosition(CPU *C)
{
    DSBuffer *b = this_buffer(C);
    uint32_t play = A(1), write = A(2), pos = 0, bytes = 0;
    silent_advance();
    if (b->data && b->block_align) {
        bytes = b->data->bytes;
        pos = ((uint32_t)b->cursor_frames * b->block_align) % (bytes ? bytes : 1u);
    }
    if (play) WR32(play, pos);
    /* Writes hold SDL's stream lock from Lock through Unlock, so the exact
       first safe byte is the play cursor itself; there is no separate DMA
       cursor in this software mixer. */
    if (write) WR32(write, pos);
    ret_com(C, DS_OK, 2);
}

static void b_GetFormat(CPU *C)
{
    DSBuffer *b = this_buffer(C);
    uint32_t p = A(1), bytes = A(2), wrote = A(3);
    if (wrote) WR32(wrote, 18u);
    if (p && bytes < 18u) { ret_com(C, DSERR_INVALIDPARAM, 3); return; }
    if (p) write_waveformat(p, b);
    ret_com(C, DS_OK, 3);
}

static void b_GetVolume(CPU *C) { DSBuffer *b=this_buffer(C); if(A(1))WR32(A(1),(uint32_t)b->volume); ret_com(C,DS_OK,1); }
static void b_GetPan(CPU *C) { DSBuffer *b=this_buffer(C); if(A(1))WR32(A(1),(uint32_t)b->pan); ret_com(C,DS_OK,1); }
static void b_GetFrequency(CPU *C) { DSBuffer *b=this_buffer(C); if(A(1))WR32(A(1),b->frequency); ret_com(C,DS_OK,1); }

static void b_GetStatus(CPU *C)
{
    DSBuffer *b = this_buffer(C);
    uint32_t status;
    silent_advance();
    status = b->playing ? DSBSTATUS_PLAYING : 0u;
    if (b->playing && b->looping) status |= DSBSTATUS_LOOPING;
    if (A(1)) WR32(A(1), status);
    ret_com(C, DS_OK, 1);
}

static void b_Lock(CPU *C)
{
    DSBuffer *b = this_buffer(C);
    uint32_t off=A(1), bytes=A(2), p1=A(3), n1=A(4), p2=A(5), n2=A(6), flags=A(7);
    uint32_t total, first;
    (void)flags;
    audio_lock();
    if (b->locked) {
        audio_unlock();
        ret_com(C, DSERR_INVALIDCALL, 7); return;
    }
    if (!b->data || !b->data->bytes || off >= b->data->bytes) {
        audio_unlock(); ret_com(C, DSERR_INVALIDPARAM, 7); return;
    }
    total = bytes ? bytes : b->data->bytes;
    if (total > b->data->bytes) total = b->data->bytes;
    first = total;
    if (off + first > b->data->bytes) first = b->data->bytes - off;
    if (p1) WR32(p1, b->data->guest_data + off);
    if (n1) WR32(n1, first);
    if (p2) WR32(p2, total > first ? b->data->guest_data : 0u);
    if (n2) WR32(n2, total - first);
    b->locks++;
    g_buffer_locks++;
    b->locked = 1;
    /* The guest writes directly to the returned PCM range. Keep SDL's
       callback out until the matching Unlock makes those bytes visible. */
    ret_com(C, DS_OK, 7);
}

static void b_Play(CPU *C)
{
    DSBuffer *b = this_buffer(C);
    audio_lock(); b->playing = 1;
    b->looping = (A(3) & DSBPLAY_LOOPING) != 0; b->plays++;
    g_buffer_plays++; audio_unlock();
    ret_com(C, DS_OK, 3);
}

static void b_SetCurrentPosition(CPU *C)
{
    DSBuffer *b = this_buffer(C);
    uint32_t byte = A(1);
    if (!b->data || byte >= b->data->bytes || !b->block_align) {
        ret_com(C, DSERR_INVALIDPARAM, 1); return;
    }
    audio_lock(); b->cursor_frames = byte / b->block_align; audio_unlock();
    ret_com(C, DS_OK, 1);
}

static void b_SetFormat(CPU *C)
{
    DSBuffer *b = this_buffer(C);
    if (!read_waveformat(A(1), b)) { ret_com(C, DSERR_INVALIDPARAM, 1); return; }
    if (b->primary) {
        g_primary_rate = (int)b->sample_rate;
        open_audio();
    }
    ret_com(C, DS_OK, 1);
}

static void b_SetVolume(CPU *C) { DSBuffer *b=this_buffer(C); int32_t v=(int32_t)A(1); if(v>0)v=0;if(v<-10000)v=-10000;audio_lock();b->volume=v;audio_unlock();ret_com(C,DS_OK,1); }
static void b_SetPan(CPU *C) { DSBuffer *b=this_buffer(C); int32_t v=(int32_t)A(1); if(v>10000)v=10000;if(v<-10000)v=-10000;audio_lock();b->pan=v;audio_unlock();ret_com(C,DS_OK,1); }
static void b_SetFrequency(CPU *C) { DSBuffer *b=this_buffer(C); uint32_t v=A(1); if(!v)v=b->sample_rate;audio_lock();b->frequency=v;audio_unlock();ret_com(C,DS_OK,1); }
static void b_Stop(CPU *C) { DSBuffer *b=this_buffer(C);audio_lock();b->playing=0;audio_unlock();ret_com(C,DS_OK,0); }
static void b_Unlock(CPU *C) { DSBuffer *b=this_buffer(C);if(!b->locked){ret_com(C,DSERR_INVALIDCALL,4);return;}b->locked=0;audio_unlock();ret_com(C,DS_OK,4); }
static void b_Restore(CPU *C) { (void)this_buffer(C); ret_com(C,DS_OK,0); }

static void ds_unimplemented(CPU *C)
{
    const char *name = (const char *)x86_callback_ctx();
    fprintf(stderr, "DSOUND: IDirectSound::%s is not implemented; refusing "
                    "instead of disabling sound quietly\n", name ? name : "(unknown)");
    (void)C;
    abort();
}

static void ds_QueryInterface(CPU *C) { if(A(2))WR32(A(2),g_ds.guest);g_ds.refs++;WR32(g_ds.guest+4,g_ds.refs);ret_com(C,DS_OK,2); }
static void ds_AddRef(CPU *C) { g_ds.refs++;WR32(g_ds.guest+4,g_ds.refs);ret_com(C,g_ds.refs,0); }
static void ds_Release(CPU *C) { if(g_ds.refs)g_ds.refs--;WR32(g_ds.guest+4,g_ds.refs);ret_com(C,g_ds.refs,0); }

static void ds_CreateSoundBuffer(CPU *C)
{
    uint32_t desc=A(1), out=A(2), flags, bytes, fmt;
    DSBuffer *b;
    SampleData *data;
    if (!desc || RD32(desc) < 20u || !out) { ret_com(C,DSERR_INVALIDPARAM,3); return; }
    flags=RD32(desc+4u); bytes=RD32(desc+8u); fmt=RD32(desc+16u);
    audio_lock();
    b=alloc_buffer();
    if (!b) { audio_unlock();WR32(out,0);ret_com(C,DSERR_OUTOFMEMORY,3);return; }
    b->flags=flags; b->primary=(flags&DSBCAPS_PRIMARYBUFFER)!=0;
    if (!b->primary) {
        if (!bytes || !read_waveformat(fmt,b)) {
            fprintf(stderr, "DSOUND: CreateSoundBuffer REFUSED desc=0x%08x "
                            "flags=0x%x bytes=%u format=0x%08x\n",
                    desc, flags, bytes, fmt);
            b->used=0;audio_unlock();WR32(out,0);ret_com(C,DSERR_INVALIDPARAM,3);return;
        }
        data=(SampleData *)calloc(1,sizeof *data);
        if (!data || !(data->guest_data=guest_malloc(bytes))) {
            free(data);b->used=0;audio_unlock();WR32(out,0);ret_com(C,DSERR_OUTOFMEMORY,3);return;
        }
        data->bytes=bytes;data->refs=1;b->data=data;
        memset(guest_memory_pointer(data->guest_data),b->bits==8?0x80:0,bytes);
        g_secondary_created++;
        if (g_secondary_created <= 12)
            fprintf(stderr, "DSOUND: secondary %lu -> object 0x%08x, out "
                            "0x%08x, %u bytes of %u Hz %u-bit %s PCM\n",
                    g_secondary_created, b->guest, out, bytes, b->sample_rate,
                    b->bits, b->channels == 1 ? "mono" : "stereo");
    }
    WR32(out,b->guest);
    audio_unlock();
    ret_com(C,DS_OK,3);
}

static void ds_GetCaps(CPU *C)
{
    uint32_t p=A(1), size=p?RD32(p):0;
    if(!p||size<24u){ret_com(C,DSERR_INVALIDPARAM,1);return;}
    memset(guest_memory_pointer(p+4u),0,size-4u);
    /* Primary/secondary mono+stereo, 8+16-bit, continuous rates. Mixing is
       software, so the hardware-buffer capacity fields remain zero. */
    WR32(p+4u,0x00000f1fu);
    WR32(p+8u,100u);
    WR32(p+12u,200000u);
    WR32(p+16u,1u);
    ret_com(C,DS_OK,1);
}

static void ds_DuplicateSoundBuffer(CPU *C)
{
    DSBuffer *src=buffer_of(A(1)), *b;
    if(!src||src->primary||!A(2)){ret_com(C,DSERR_INVALIDPARAM,2);return;}
    audio_lock();
    /* realloc may move the registry, so retain the source index rather than
       a host pointer across alloc_buffer. */
    {
        int src_index=(int)(src-g_buf);
        b=alloc_buffer();
        src=&g_buf[src_index];
    }
    if(!b){audio_unlock();WR32(A(2),0);ret_com(C,DSERR_OUTOFMEMORY,2);return;}
    {
        uint32_t guest=b->guest; unsigned refs=b->refs;
        *b=*src;b->guest=guest;b->refs=refs;b->used=1;b->playing=0;
        b->cursor_frames=0;b->plays=0;b->locks=0;b->data->refs++;
    }
    WR32(b->guest,g_buf_vtable);WR32(b->guest+4,b->refs);WR32(A(2),b->guest);
    audio_unlock();
    g_duplicates++;
    if (g_duplicates <= 12)
        fprintf(stderr, "DSOUND: duplicate %lu of 0x%08x -> 0x%08x, out "
                        "0x%08x (shared PCM, independent cursor)\n",
                g_duplicates, src->guest, b->guest, A(2));
    ret_com(C,DS_OK,2);
}

static void ds_SetCooperativeLevel(CPU *C) { g_coop_hwnd=A(1);g_coop_level=A(2);ret_com(C,DS_OK,2); }
static void ds_Compact(CPU *C) { ret_com(C,DS_OK,0); }
static void ds_GetSpeakerConfig(CPU *C) { if(A(1))WR32(A(1),4u);ret_com(C,DS_OK,1); }
static void ds_SetSpeakerConfig(CPU *C) { ret_com(C,DS_OK,1); }
static void ds_Initialize(CPU *C) { ret_com(C,DS_OK,1); }

static void build_vtables(void)
{
    static void (*const ds_impl[DSVT_COUNT])(CPU *) = {
        ds_QueryInterface,ds_AddRef,ds_Release,ds_CreateSoundBuffer,ds_GetCaps,
        ds_DuplicateSoundBuffer,ds_SetCooperativeLevel,ds_Compact,
        ds_GetSpeakerConfig,ds_SetSpeakerConfig,ds_Initialize
    };
    static void (*const b_impl[BVT_COUNT])(CPU *) = {
        b_QueryInterface,b_AddRef,b_Release,b_GetCaps,b_GetCurrentPosition,
        b_GetFormat,b_GetVolume,b_GetPan,b_GetFrequency,b_GetStatus,
        b_unimplemented,b_Lock,b_Play,b_SetCurrentPosition,b_SetFormat,
        b_SetVolume,b_SetPan,b_SetFrequency,b_Stop,b_Unlock,b_Restore
    };
    int i;
    if (g_ds_vtable) return;
    g_ds_vtable=guest_malloc(DSVT_COUNT*4u);
    g_buf_vtable=guest_malloc(BVT_COUNT*4u);
    g_ds.guest=guest_malloc(8u);g_ds.refs=1;
    if(!g_ds_vtable||!g_buf_vtable||!g_ds.guest){fprintf(stderr,"DSOUND: no guest memory for COM objects\n");abort();}
    for(i=0;i<DSVT_COUNT;i++)WR32(g_ds_vtable+i*4u,x86_native_callback(ds_impl[i]?ds_impl[i]:ds_unimplemented,"IDirectSound",DS_NAME[i],(void *)DS_NAME[i]));
    for(i=0;i<BVT_COUNT;i++)WR32(g_buf_vtable+i*4u,x86_native_callback(b_impl[i]?b_impl[i]:b_unimplemented,"IDirectSoundBuffer",BVT_NAME[i],(void *)BVT_NAME[i]));
    WR32(g_ds.guest,g_ds_vtable);WR32(g_ds.guest+4,g_ds.refs);
}

static void imp_DSOUND_DirectSoundCreate(CPU *C)
{
    uint32_t out=A(1), outer=A(2);
    if(!out||outer){if(out)WR32(out,0);ret_std(C,DSERR_INVALIDPARAM,3);return;}
    build_vtables();g_creates++;g_ds.refs++;WR32(g_ds.guest+4,g_ds.refs);WR32(out,g_ds.guest);
    if(g_creates==1)fprintf(stderr,"DSOUND: DirectSoundCreate -> native IDirectSound at 0x%08x; XMen2.exe FUN_00594290 owns this load path.\n",g_ds.guest);
    ret_std(C,DS_OK,3);
}

void dsound_install(void)
{
    x86_native_export("DSOUND.DLL","DirectSoundCreate",imp_DSOUND_DirectSoundCreate);
}

void dsound_report(void)
{
    static int done;
    int i, live=0, playing=0;
    if(done++)return;
    for(i=0;i<g_nbuf;i++)if(g_buf[i].used){live++;if(g_buf[i].playing)playing++;}
    printf("  dsound: %lu DirectSoundCreate, %lu secondary buffer(s), %lu duplicate(s); %d live / %d playing, %lu Play, %lu Lock\n",g_creates,g_secondary_created,g_duplicates,live,playing,g_buffer_plays,g_buffer_locks);
    printf("          mixer: %lu callback(s), %lu frame(s), %lu nonzero sample(s), peak %.4f, %lu silent-clock advance(s)%s\n",g_mix_callbacks,g_mix_frames,g_mix_nonzero,g_mix_peak,g_silent_advances,g_audio_silent?" -- NO HOST AUDIO DEVICE":"");
    movie_audio_report();
}

int dsound_selftest(void)
{
    DSBuffer a,b;
    SampleData d;
    unsigned char pcm[8]={0,0,0,64,0,128,0,192};
    float mix[8];
    int fails=0,i;
    memset(&a,0,sizeof a);memset(&b,0,sizeof b);memset(&d,0,sizeof d);
    d.guest_data=guest_malloc(sizeof pcm);d.bytes=sizeof pcm;d.refs=2;
    if(!d.guest_data){printf("DSOUND mixer selftest: FAILED -- no guest PCM allocation\n");return 1;}
    memcpy(guest_memory_pointer(d.guest_data),pcm,sizeof pcm);
    a.used=b.used=1;a.data=b.data=&d;a.channels=b.channels=1;a.bits=b.bits=16;
    a.block_align=b.block_align=2;a.sample_rate=b.sample_rate=4;a.frequency=b.frequency=4;
    a.playing=b.playing=1;a.looping=b.looping=1;b.volume=-10000;
    memset(mix,0,sizeof mix);advance_buffer(&a,4,4,mix);advance_buffer(&b,4,4,mix);
    if(a.cursor_frames!=4.0||b.cursor_frames!=4.0||fabsf(mix[0])>0.0001f||mix[2]<0.49f||mix[4]>-0.99f||mix[6]>-0.49f)fails++;
    a.cursor_frames=3.0;a.playing=1;a.looping=0;advance_buffer(&a,2,4,NULL);
    if(a.playing||a.cursor_frames!=4.0)fails++;
    guest_free(d.guest_data);
    /* Loading the tutorial uses more than 256 COM buffer objects. This drives
       the exact old failure class instead of testing a tiny happy path. */
    if(g_nbuf!=0)fails++;
    for(i=0;i<300;i++)if(!alloc_buffer()){fails++;break;}
    if(g_nbuf!=300||g_capbuf<300)fails++;
    for(i=0;i<g_nbuf;i++)if(g_buf[i].guest)guest_free(g_buf[i].guest);
    free(g_buf);g_buf=NULL;g_nbuf=g_capbuf=0;
    printf("DSOUND mixer selftest: %s -- shared PCM voices have independent cursors, mute and one-shot stop semantics; the registry grows past 256 objects\n",fails?"FAILED":"PASSED");
    return fails;
}
