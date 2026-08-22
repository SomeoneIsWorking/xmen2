/* libCriMovie guest ABI bridge. Decode, timing, and audio streaming belong to
 * src/media and src/audio; this file only translates the evidenced codec
 * methods and igMovieInfo layout into those narrow host interfaces. Every
 * retained guest body remains callable with X2_NATIVE_FMV=0. */
#include "dsound.h"
#include "fmv_player.h"
#include "movie_audio.h"
#include "movie_image_layout.h"
#include "x86rt.h"
#include "x86rt_native.h"
#include "pe_map.h"
#include "threads.h"
#include "win_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INFO_PATH      0x14u
#define INFO_WIDTH     0x20u
#define INFO_HEIGHT    0x24u
#define INFO_STATE     0x50u
#define INFO_IMAGE     0x58u
#define IMAGE_BYTES    0x30u
#define IMAGE_DATA     0x34u

typedef struct {
    uint32_t info;
    X2FmvPlayer *player;
    int failed;
    int needs_copy;
} NativeMovie;

static NativeMovie g_native_movie;

void fn_libCriMovie_10001ab0(CPU *C);
void fn_libCriMovie_10001fa0(CPU *C);
void fn_libCriMovie_10002040(CPU *C);
void fn_libCriMovie_100020c0(CPU *C);
void fn_libCriMovie_10002140(CPU *C);
void fn_libCriMovie_100021c0(CPU *C);

static int native_fmv_enabled(void)
{
    static int enabled = -1;
    static int announced;
    if (enabled < 0) {
        const char *value = getenv("X2_NATIVE_FMV");
        enabled = !value || strcmp(value, "0") != 0;
    }
    if (!announced++)
        printf("movie: %s SFD playback; set X2_NATIVE_FMV=0 for the retained "
               "guest CriMovie bodies.\n", enabled ? "native FFmpeg" : "guest");
    return enabled;
}

static void movie_return(CPU *C, uint32_t value, int arguments)
{
    C->eax = value;
    C->esp += 4u + (uint32_t)arguments * 4u;
}

static int queue_movie_audio(void *userdata, const float *samples,
                             size_t frames, int sample_rate)
{
    (void)userdata;
    (void)sample_rate;
    return movie_audio_queue(samples, frames);
}

static double queued_movie_audio(void *userdata)
{
    (void)userdata;
    return movie_audio_queued_seconds();
}

static void close_native_movie(void)
{
    if (g_native_movie.player) x2_fmv_report(g_native_movie.player);
    x2_fmv_close(g_native_movie.player);
    movie_audio_close();
    memset(&g_native_movie, 0, sizeof(g_native_movie));
}

static X2FmvPlayer *movie_for(uint32_t info)
{
    return g_native_movie.info == info ? g_native_movie.player : NULL;
}

void x2_movie_report(void)
{
    if (g_native_movie.player) x2_fmv_report(g_native_movie.player);
}

static void x2_movie_load(CPU *C)
{
    uint32_t info = RD32(C->esp + 4u);
    uint32_t path_address = info ? RD32(info + INFO_PATH) : 0;
    const char *guest_path = (const char *)(uintptr_t)path_address;
    const char *host_path;
    X2FmvAudioSink sink;
    X2FmvPlayer *player;
    char error[256];
    int first_frame, replaced;
    if (!native_fmv_enabled()) { fn_libCriMovie_10001ab0(C); return; }
    if (!guest_path || !*guest_path) { movie_return(C, 0, 1); return; }
    replaced = k32_open_replaced(guest_path, 0);
    host_path = k32_open_path(guest_path, 0);
    memset(&sink, 0, sizeof(sink));
    sink.queue_stereo_f32 = queue_movie_audio;
    sink.queued_seconds = queued_movie_audio;
    error[0] = '\0';
    close_native_movie();
    player = x2_fmv_open(host_path, &sink, error, sizeof(error));
    k32_open_note(guest_path, player != NULL, replaced, host_path);
    if (!player) {
        fprintf(stderr, "movie: native SFD load failed for '%s': %s\n",
                guest_path, error[0] ? error : "unknown decoder error");
        movie_return(C, 0, 1);
        return;
    }
    dsound_movie_audio_begin();
    if (!movie_audio_open(x2_fmv_sample_rate(player))) {
        fprintf(stderr, "movie: cannot allocate the SFD audio queue\n");
        x2_fmv_close(player);
        movie_return(C, 0, 1);
        return;
    }
    first_frame = x2_fmv_update(player, 0.0);
    if (first_frame < 0 || !x2_fmv_decoded_frames(player)) {
        fprintf(stderr, "movie: SFD '%s' produced no decodable video frame\n",
                guest_path);
        x2_fmv_close(player);
        movie_audio_close();
        movie_return(C, 0, 1);
        return;
    }
    g_native_movie.info = info;
    g_native_movie.player = player;
    g_native_movie.needs_copy = 1;
    WR32(info + INFO_WIDTH, (uint32_t)x2_fmv_width(player));
    WR32(info + INFO_HEIGHT, (uint32_t)x2_fmv_height(player));
    printf("movie: loaded native MPEG-1/ADX SFD '%s' at %dx%d, %d Hz\n",
           guest_path, x2_fmv_width(player), x2_fmv_height(player),
           x2_fmv_sample_rate(player));
    movie_return(C, 1, 1);
}

static void x2_movie_unload(CPU *C)
{
    uint32_t info = RD32(C->esp + 4u);
    if (!native_fmv_enabled()) { fn_libCriMovie_10001fa0(C); return; }
    if (!movie_for(info)) { movie_return(C, 0, 1); return; }
    close_native_movie();
    movie_return(C, 1, 1);
}

static void x2_movie_play(CPU *C)
{
    uint32_t info = RD32(C->esp + 4u);
    X2FmvPlayer *player;
    if (!native_fmv_enabled()) { fn_libCriMovie_10002040(C); return; }
    player = movie_for(info);
    if (!player) { movie_return(C, 0, 1); return; }
    WR32(info + INFO_STATE, 0u);
    x2_fmv_play(player);
    movie_audio_play();
    movie_return(C, 1, 1);
}

static void x2_movie_pause(CPU *C)
{
    uint32_t info = RD32(C->esp + 4u);
    uint32_t state = RD32(C->esp + 8u);
    X2FmvPlayer *player;
    if (!native_fmv_enabled()) { fn_libCriMovie_100020c0(C); return; }
    player = movie_for(info);
    if (!player) { movie_return(C, 0, 2); return; }
    WR32(info + INFO_STATE, state);
    x2_fmv_pause(player, state != 0u);
    movie_audio_pause(state != 0u);
    movie_return(C, 1, 2);
}

static void x2_movie_check_state(CPU *C)
{
    uint32_t info = RD32(C->esp + 4u);
    X2FmvPlayer *player;
    X2FmvState state;
    if (!native_fmv_enabled()) { fn_libCriMovie_10002140(C); return; }
    player = movie_for(info);
    if (!player || g_native_movie.failed) { movie_return(C, 0, 1); return; }
    state = x2_fmv_state(player);
    if (state == X2_FMV_FINISHED) WR32(info + INFO_STATE, 2u);
    if (state == X2_FMV_FAILED) WR32(info + INFO_STATE, 3u);
    movie_return(C, state != X2_FMV_FAILED, 1);
}

static void x2_movie_next_frame(CPU *C)
{
    uint32_t info = RD32(C->esp + 4u);
    X2FmvPlayer *player;
    uint32_t image, data, bytes;
    size_t pitch;
    int changed;
    if (!native_fmv_enabled()) { fn_libCriMovie_100021c0(C); return; }
    player = movie_for(info);
    if (!player || g_native_movie.failed) { movie_return(C, 0, 1); return; }
    dsound_movie_audio_tick();
    changed = x2_fmv_update(player, movie_audio_played_seconds());
    if (changed >= 0 && g_native_movie.needs_copy) changed = 1;
    if (changed > 0) {
        image = RD32(info + INFO_IMAGE);
        data = image ? RD32(image + IMAGE_DATA) : 0;
        bytes = image ? RD32(image + IMAGE_BYTES) : 0;
        if (!x2_movie_image_pitch(x2_fmv_width(player),
                                  x2_fmv_height(player), bytes, &pitch))
            pitch = 0;
        if (!data || !pitch
                || !x2_fmv_copy_bgra(player, (void *)(uintptr_t)data,
                                     bytes, pitch)) {
            fprintf(stderr, "movie: igImage storage is invalid for the %dx%d "
                            "native SFD frame (data=0x%08x bytes=%u)\n",
                    x2_fmv_width(player), x2_fmv_height(player), data, bytes);
            g_native_movie.failed = 1;
            WR32(info + INFO_STATE, 3u);
            changed = -1;
        } else g_native_movie.needs_copy = 0;
    }
    if (x2_fmv_state(player) == X2_FMV_FINISHED)
        WR32(info + INFO_STATE, 2u);
    else if (changed < 0 || x2_fmv_state(player) == X2_FMV_FAILED)
        WR32(info + INFO_STATE, 3u);
    movie_return(C, changed > 0, 1);
}

/* ---------------------------------------------------------------------
 * libCriMovie 0x10002520 -- the movie decoder's spin partner (issue #57).
 *
 * GUEST PROTOCOL, read off the disassembly, not inferred: across a global
 * lock, the decoder (libCriMovie 0x10002630) and this partner rendezvous on
 * the flag at libCriMovie+0x572b0. The partner
 *
 *     1000252f  MOV [0x100572b0], 1        -- arm the flag
 *     1000255d  CMP ESI, 0x2dc6c0          -- then, up to 3,000,000 times:
 *        SetThreadPriority(decoder, [0x10057294])   via EDI = [0x10042070]
 *        ResumeThread(decoder)                      via EBX = [0x1004206c]
 *        ... until the flag clears; ESI counts the retries, and ESI == 0x2dc6c0
 *        on the way out is the out-of-patience error (FUN_10008370).
 *     1000257a  SetThreadPriority(decoder, [0x100572a8])  -- restore
 *     RET 4+0, EAX = that SetThreadPriority call's return (1 in this port).
 *
 * The decoder clears the flag to 0 and parks -- SuspendThread on ITSELF via
 * [0x10042058], the park path at 0x1000266d -- at its very next loop top
 * whenever the flag is 1, whether or not it has work to do. The flag being 1
 * is the signal "park now so I can see you caught up", and the spin is a poll
 * for the park. It is NOT a general run-to-park on every resume: it is armed
 * explicitly, before we wait, and the arm is exactly what guarantees the
 * decoder parks instead of continuing.
 *
 * SO THE SPIN IS REPLACED, not just yielded on: arm the flag, resume the
 * decoder once the way a single spin iteration would, then BLOCK until the
 * flag clears. One resume is enough because the decoder, once runnable, clears
 * the flag at its loop top by the same logic the spin polled for. If it has
 * not parked within a generous bound, DEFER to the retained body, which spins
 * exactly as today -- the override cannot make the load window worse, only
 * better, and "deferred" is logged when that happens, never silent.
 */
void fn_libCriMovie_10002520(CPU *C);

#define LCR_FLAG       0x572b0u   /* 1 = "park now"; the decoder zeroes it        */
#define LCR_SPIN_PRIO  0x57294u   /* the priority the spin raises the decoder to */
#define LCR_REST_PRIO  0x572a8u   /* the priority restored before returning      */
#define LCR_DECODER    0x14a1fcu  /* the decoder thread's own handle             */
#define LCR_MAX_WAITS  1000u      /* 1 ms waits; on expiry, defer to the spin    */

void x2_override_10002520(CPU *C)
{
    static int mode = -1;                     /* -1 unknown, 0 wait, 1 spin */
    static int said;
    const X86Module *m;
    uint32_t base = 0;
    uint32_t handle;
    unsigned long waits;
    int deferred;

    if (mode < 0) {
        const char *e = getenv("X2_SPIN");
        mode = (e && *e && *e != '0' && !strcmp(e, "spin")) ? 1 : 0;
    }
    if (mode) {                              /* the CONTROL: run as shipped */
        fn_libCriMovie_10002520(C);
        return;
    }

    for (m = x86_modules(); m; m = m->next)
        if (!strcmp(m->name, "libCriMovie.dll")) { base = *m->base; break; }
    if (!base) {
        fprintf(stderr, "override: libCriMovie 0x10002520 cannot find the "
                        "module; deferring the spin to the original body.\n");
        fn_libCriMovie_10002520(C);
        return;
    }

    if (!said++)
        printf("override: libCriMovie 0x10002520, the decoder rendezvous spin, "
               "is WAITED FOR rather than spun (issue #57).\n"
               "  Set X2_SPIN=spin to run the original 3,000,000-iteration "
               "resume loop instead -- the control this treatment is judged "
               "against.\n");

    WR32(base + LCR_FLAG, 1u);                       /* arm, as 0x1000252f */
    guest_thread_priority_set((int32_t)RD32(base + LCR_SPIN_PRIO)); /* the spin's raise */
    handle = RD32(base + LCR_DECODER);
    guest_thread_resume(handle);                     /* one spin iteration */

    for (waits = 0; waits < LCR_MAX_WAITS && RD32(base + LCR_FLAG) == 1u; waits++)
        guest_cond_wait_ms(1);                       /* give the decoder CPU */

    deferred = (RD32(base + LCR_FLAG) == 1u);
    if (deferred) {
        fprintf(stderr, "override: libCriMovie 0x10002520: the decoder did not "
                        "clear the flag within %lu ms of being resumed. "
                        "DEFERRING to the original spin, which is the faithful "
                        "behaviour the override exists to avoid -- logged so "
                        "the fallback is never silent.\n", waits);
        fn_libCriMovie_10002520(C);           /* spins as today; sets EAX */
        return;
    }

    guest_thread_priority_set((int32_t)RD32(base + LCR_REST_PRIO)); /* restore */
    C->eax = 1u;      /* the port's SetThreadPriority returns 1; same as the body */
    C->esp += 4u;     /* RET: the body pops only its own return address */
}

__attribute__((constructor))
static void x2_movie_register_overrides(void)
{
    x86_register_override("libCriMovie.dll", 0x10001ab0, x2_movie_load);
    x86_register_override("libCriMovie.dll", 0x10001fa0, x2_movie_unload);
    x86_register_override("libCriMovie.dll", 0x10002040, x2_movie_play);
    x86_register_override("libCriMovie.dll", 0x100020c0, x2_movie_pause);
    x86_register_override("libCriMovie.dll", 0x10002140,
                          x2_movie_check_state);
    x86_register_override("libCriMovie.dll", 0x100021c0,
                          x2_movie_next_frame);
    x86_register_override("libCriMovie.dll", 0x10002520, x2_override_10002520);
}
