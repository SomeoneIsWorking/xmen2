#ifndef X2_IGB_ANIM_H
#define X2_IGB_ANIM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Enbaya (Raven "engb") animation stream decode. Reversed from libIGSg.dll;
 * see docs/RE/enbaya_decode.md for the full format write-up. */

typedef struct {
    float quat[4];   /* x, y, z, w (unit, normalized) */
    float pos[3];    /* translation */
} igb_enbaya_pose;

typedef struct {
    int track_count;
    float scale;
    float duration;
    float interval;       /* seconds per keyframe = 1/fps */
    int frame_count;      /* number of decoded poses (init + stepped frames) */
    igb_enbaya_pose *poses; /* frame_count * track_count, frame-major */
} igb_enbaya_anim;

/* Decode a serialized PgAnimationStreamImp blob (the mem payload of an
 * igEnbayaAnimationSource's MemoryRef, typically object-1136 style payload)
 * into every keyframe of the cycle. Returns 0 on success, -1 on bad data. */
int igb_enbaya_decode(const uint8_t *blob, size_t blob_len, igb_enbaya_anim *out);
void igb_enbaya_free(igb_enbaya_anim *out);

/* Sample pose at time t in seconds (clamped to the animation's range).
 * Writes out[track] for track in [0, track_count). */
void igb_enbaya_pose_at(const igb_enbaya_anim *a, float t, igb_enbaya_pose *out);

#ifdef __cplusplus
}
#endif

#endif
