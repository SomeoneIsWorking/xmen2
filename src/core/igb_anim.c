#include "igb_anim.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- bitstream readers (MSB-first 2-bit tags) ---- */

#define SH_2BIT_MSB  { 6, 4, 2, 0 }
#define SH_4BIT_MSB  { 4, 0, 0, 0 }
static const int sh2[4] = SH_2BIT_MSB;
static const int sh4[4] = SH_4BIT_MSB;

static const int v2[4]   = { 0, 1, 0, -1 };
static const int v4[16]  = { 0, 8, 2, 3, 4, 5, 6, 7,
                             -8, -7, -6, -5, -4, -3, -2, -1 };

typedef struct {
    const uint8_t *tag;    /* 2-bit tag stream   */
    const uint8_t *code;   /* 4-bit code stream (delta only) */
    const uint8_t *byte;   /* 8-bit values       */
    const uint8_t *sh;     /* 16-bit values      */
    const uint8_t *in;     /* 32-bit values      */
    int bit;               /* tag bit position   */
    int cbit;              /* code bit position  */
} enb_reader;

static int rd_s8(const uint8_t *p) { return (int)(int8_t)*p; }
static int rd_s16(const uint8_t *p) { return (int)(int16_t)(p[0] | (p[1] << 8)); }
static int rd_u16(const uint8_t *p) { return (int)(p[0] | (p[1] << 8)); }
static int rd_s32(const uint8_t *p) { return (int)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); }

/* FUN_1007ff40: initial keyframe values (signed). */
static int rd_init(enb_reader *r)
{
    if (r->bit == 4) { r->bit = 0; r->tag++; }
    int t = (rd_s8(r->tag) >> sh2[r->bit]) & 3;
    r->bit++;
    if (t == 0) return 0;
    if (t == 1) { int v = rd_s8(r->byte); r->byte++; return v; }
    if (t == 2) { int v = rd_s16(r->sh); r->sh += 2; return v; }
    int v = rd_s32(r->in); r->in += 4; return v;
}

/* FUN_1007fc00: run lengths (unsigned). */
static uint32_t rd_unsigned(enb_reader *r)
{
    if (r->bit == 4) { r->bit = 0; r->tag++; }
    int t = (rd_s8(r->tag) >> sh2[r->bit]) & 3;
    r->bit++;
    if (t == 0) return 0;
    if (t == 1) { uint32_t v = *r->byte; r->byte++; return v; }
    if (t == 2) { uint32_t v = (uint32_t)rd_u16(r->sh); r->sh += 2; return v; }
    uint32_t v = (uint32_t)rd_s32(r->in); r->in += 4; return v;
}

/* FUN_10080140: per-keyframe delta values (signed, nested VLC). */
static int rd_delta(enb_reader *r)
{
    if (r->bit == 4) { r->bit = 0; r->tag++; }
    int t = (rd_s8(r->tag) >> sh2[r->bit]) & 3;
    r->bit++;
    if (t != 2) return v2[t];
    if (r->cbit == 2) { r->cbit = 0; r->code++; }
    int code = (rd_s8(r->code) >> sh4[r->cbit]) & 0xf;
    r->cbit++;
    if (code != 0) return v4[code];
    int v1 = rd_s8(r->byte); r->byte++;
    if (v1 == 0) {
        int v2_ = rd_s16(r->sh); r->sh += 2;
        if (v2_ == 0) { int v3 = rd_s32(r->in); r->in += 4; return v3; }
        return v2_;
    }
    if (v1 >= 1 && v1 <= 8) return v1 + 0x7f;
    if (v1 >= -8 && v1 <= -1) return v1 - 0x80;
    return v1;
}

static void normalize_quat(float q[4])
{
    float n = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    if (n > 1e-12f) {
        n = sqrtf(n);
        q[0] /= n; q[1] /= n; q[2] /= n; q[3] /= n;
    }
}

int igb_enbaya_decode(const uint8_t *blob, size_t len, igb_enbaya_anim *out)
{
    if (!blob || !out || len < 0x50 + 0x50) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    int track_count = (int)(uint32_t)(blob[0x04] | (blob[0x05] << 8) |
                                      (blob[0x06] << 16) | (blob[0x07] << 24));
    float scale = 0;
    memcpy(&scale, blob + 0x08, 4);
    float duration = 0;
    memcpy(&duration, blob + 0x0c, 4);
    int fps = (int)(uint32_t)(blob[0x10] | (blob[0x11] << 8) |
                              (blob[0x12] << 16) | (blob[0x13] << 24));

    if (track_count <= 0 || track_count > 256 || fps <= 0 || !(scale > 0) ||
        !(duration > 0)) {
        return -1;
    }

    /* Cumulative sub-stream cursors, chained in accessor order
     * (FUN_1007f420..FUN_1007f5a0). Offsets relative to the data base. */
    int f20 = (int)(uint32_t)(blob[0x20] | (blob[0x21] << 8) | (blob[0x22] << 16) | (blob[0x23] << 24));
    int f34 = (int)(uint32_t)(blob[0x34] | (blob[0x35] << 8) | (blob[0x36] << 16) | (blob[0x37] << 24));
    int f44 = (int)(uint32_t)(blob[0x44] | (blob[0x45] << 8) | (blob[0x46] << 16) | (blob[0x47] << 24));
    int f1c = (int)(uint32_t)(blob[0x1c] | (blob[0x1d] << 8) | (blob[0x1e] << 16) | (blob[0x1f] << 24));
    int f30 = (int)(uint32_t)(blob[0x30] | (blob[0x31] << 8) | (blob[0x32] << 16) | (blob[0x33] << 24));
    int f40 = (int)(uint32_t)(blob[0x40] | (blob[0x41] << 8) | (blob[0x42] << 16) | (blob[0x43] << 24));
    int f14 = (int)(uint32_t)(blob[0x14] | (blob[0x15] << 8) | (blob[0x16] << 16) | (blob[0x17] << 24));
    int f18 = (int)(uint32_t)(blob[0x18] | (blob[0x19] << 8) | (blob[0x1a] << 16) | (blob[0x1b] << 24));
    int f24 = (int)(uint32_t)(blob[0x24] | (blob[0x25] << 8) | (blob[0x26] << 16) | (blob[0x27] << 24));
    int f28 = (int)(uint32_t)(blob[0x28] | (blob[0x29] << 8) | (blob[0x2a] << 16) | (blob[0x2b] << 24));
    int f2c = (int)(uint32_t)(blob[0x2c] | (blob[0x2d] << 8) | (blob[0x2e] << 16) | (blob[0x2f] << 24));
    int f38 = (int)(uint32_t)(blob[0x38] | (blob[0x39] << 8) | (blob[0x3a] << 16) | (blob[0x3b] << 24));
    int f3c = (int)(uint32_t)(blob[0x3c] | (blob[0x3d] << 8) | (blob[0x3e] << 16) | (blob[0x3f] << 24));

    int c1  = f20;
    int c2  = c1 + f34;
    int c3  = c2 + f44;
    int c4  = c3 + f1c;
    int c5  = c4 + f30;
    int c6  = c5 + f40;
    int c7  = c6 + f14;
    int c8  = c7 + f18;
    int c9  = c8 + f24;
    int c10 = c9 + f28;
    int c11 = c10 + f2c;
    int c12 = c11 + f38;
    int c13 = c12 + f3c;

    const uint8_t *data = blob + 0x50;
    size_t dlen = len - 0x50;

    enb_reader r1 = { data + c6,  NULL, data + c7, data + c3, data + 0, 0, 0 };
    enb_reader r2 = { data + c8, data + c9, data + c10, data + c4, data + c1, 0, 0 };
    enb_reader r3 = { data + c11, NULL, data + c12, data + c5, data + c2, 0, 0 };
    const uint8_t *flags0 = data + c13;

    if (c13 < 0 || (size_t)c13 + (size_t)track_count > dlen) {
        return -1;
    }

    /* Number of stepped keyframes = ceil(duration / interval). */
    float interval = 1.0f / (float)fps;
    int n_steps = (int)ceilf(duration / interval - 1e-6f);
    if (n_steps < 1 || n_steps > 1024) {
        return -1;
    }

    int n_frame = n_steps + 1;
    out->track_count = track_count;
    out->scale = scale;
    out->duration = duration;
    out->interval = interval;
    out->frame_count = n_frame;
    out->poses = (igb_enbaya_pose *)calloc((size_t)n_frame * (size_t)track_count,
                                           sizeof(igb_enbaya_pose));
    if (!out->poses) {
        return -1;
    }

    /* Per-track: 7 accumulated deltas. */
    float *accum = (float *)calloc((size_t)track_count * 7, sizeof(float));
    if (!accum) {
        igb_enbaya_free(out);
        return -1;
    }
    /* Double-buffered poses, 7 floats each. */
    float *buf[2];
    buf[0] = (float *)calloc((size_t)track_count * 7, sizeof(float));
    buf[1] = (float *)calloc((size_t)track_count * 7, sizeof(float));
    uint8_t *flags = (uint8_t *)malloc((size_t)track_count);
    if (!buf[0] || !buf[1] || !flags) {
        free(accum); free(buf[0]); free(buf[1]); free(flags);
        igb_enbaya_free(out);
        return -1;
    }
    memcpy(flags, flags0, (size_t)track_count);

    /* Init: 7 initial values per track, pose[0] = init * scale, quats normalized. */
    for (int t = 0; t < track_count; t++) {
        for (int c = 0; c < 7; c++) {
            accum[t * 7 + c] = (float)rd_init(&r1);
        }
        for (int c = 0; c < 7; c++) {
            buf[0][t * 7 + c] = accum[t * 7 + c] * scale;
        }
        normalize_quat(&buf[0][t * 7]);
        for (int c = 0; c < 7; c++) {
            accum[t * 7 + c] = 0.0f;
        }
    }
    memcpy(buf[1], buf[0], (size_t)track_count * 7 * sizeof(float));

    for (int t = 0; t < track_count; t++) {
        memcpy(&out->poses[t], &buf[0][t * 7], sizeof(igb_enbaya_pose));
    }

    /* Channel state: remaining run-length + skipped count (R unused forward). */
    uint32_t L = rd_unsigned(&r3);
    (void)interval;

    for (int step = 1; step <= n_steps; step++) {
        if (step > 1) {
            /* FUN_100805b0: advance channel schedule. */
            int pos = 0, total = track_count * 7;
            while (pos < total) {
                if (L == 0) {
                    int t = pos / 7, c = pos % 7;
                    flags[t] ^= (uint8_t)(1u << c);
                    L = rd_unsigned(&r3);
                    pos++;
                } else {
                    int st = (int)L < total - pos ? (int)L : total - pos;
                    pos += st;
                    L -= (uint32_t)st;
                }
            }
        }
        /* FUN_10080040: add deltas to accum for flagged channels. */
        for (int t = 0; t < track_count; t++) {
            if (!flags[t]) {
                continue;
            }
            for (int c = 0; c < 7; c++) {
                if (flags[t] & (uint8_t)(1u << c)) {
                    accum[t * 7 + c] += (float)rd_delta(&r2);
                }
            }
        }
        /* FUN_1007f670: next = accum * scale + prev, then normalize. */
        int nbuf = step & 1, pbuf = nbuf ^ 1;
        for (int t = 0; t < track_count; t++) {
            for (int c = 0; c < 7; c++) {
                buf[nbuf][t * 7 + c] = accum[t * 7 + c] * scale + buf[pbuf][t * 7 + c];
            }
            normalize_quat(&buf[nbuf][t * 7]);
        }
        for (int t = 0; t < track_count; t++) {
            memcpy(&out->poses[(size_t)step * track_count + t], &buf[nbuf][t * 7],
                   sizeof(igb_enbaya_pose));
        }
    }

    free(accum);
    free(buf[0]);
    free(buf[1]);
    free(flags);
    return 0;
}

void igb_enbaya_free(igb_enbaya_anim *out)
{
    if (out) {
        free(out->poses);
        memset(out, 0, sizeof(*out));
    }
}

void igb_enbaya_pose_at(const igb_enbaya_anim *a, float t, igb_enbaya_pose *out)
{
    if (!a || !out || a->frame_count <= 0) {
        return;
    }
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t >= a->duration) {
        t = a->duration - a->interval;
    }
    int idx = (int)(t / a->interval);
    if (idx < 0) idx = 0;
    if (idx > a->frame_count - 2) idx = a->frame_count - 2;
    memcpy(out, &a->poses[(size_t)idx * a->track_count],
           (size_t)a->track_count * sizeof(igb_enbaya_pose));
}
