#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "igb_anim.h"

static int closef(float a, float b, float eps)
{
    return fabsf(a - b) < eps;
}

/* Oracle: scratch/logs/blob666.bin, the 666-byte Enbaya stream of the
 * Wolverine walk (03_wolverine.IGB object 2159 -> blob 1136). Not committed
 * to git (game asset); the test skips when the data is absent. */
int main(void)
{
    const char *path = "scratch/logs/blob666.bin";
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        printf("test_enbaya: SKIP (%s not present)\n", path);
        return 0;
    }
    uint8_t blob[666];
    size_t n = fread(blob, 1, sizeof(blob), fp);
    fclose(fp);
    if (n != sizeof(blob)) {
        printf("test_enbaya: SKIP (bad blob)\n");
        return 0;
    }

    igb_enbaya_anim a;
    int rc = igb_enbaya_decode(blob, sizeof(blob), &a);
    assert(rc == 0);
    assert(a.track_count == 33);
    assert(a.frame_count == 7);
    assert(closef(a.duration, 0.3f, 1e-5f));
    assert(closef(a.interval, 0.05f, 1e-6f));

    /* t0 (root) stays identity through the cycle. */
    for (int f = 0; f < a.frame_count; ++f) {
        const igb_enbaya_pose *p = &a.poses[(size_t)f * a.track_count + 0];
        assert(closef(p->quat[3], 1.0f, 1e-4f));
        assert(closef(p->quat[0], 0.0f, 1e-5f));
        assert(closef(p->quat[1], 0.0f, 1e-5f));
        assert(closef(p->quat[2], 0.0f, 1e-5f));
    }

    /* Reference values from the Python oracle (scratch/logs/dec2.py) with the
     * engine's quaternion normalization applied. */
    const igb_enbaya_pose *f1 = &a.poses[a.track_count + 1];
    assert(closef(f1->quat[0], -0.0360f, 1e-3f));
    assert(closef(f1->quat[1], -0.1279f, 1e-3f));
    assert(closef(f1->quat[2], 0.0720f, 1e-3f));
    assert(closef(f1->pos[0], 0.148f, 1e-2f));
    assert(closef(f1->pos[1], -0.832f, 1e-2f));
    assert(closef(f1->pos[2], 29.740f, 1e-2f));

    const igb_enbaya_pose *f5 = &a.poses[(size_t)5 * a.track_count + 1];
    assert(closef(f5->quat[0], -0.0441f, 1e-3f));
    assert(closef(f5->quat[1], -0.2442f, 1e-3f));
    assert(closef(f5->quat[2], 0.0682f, 1e-3f));
    assert(closef(f5->pos[0], -0.712f, 1e-2f));
    assert(closef(f5->pos[2], 28.716f, 1e-2f));

    /* Step 6 still applies deltas to tracks 9/11/29/30 (quat y/z channels). */
    const igb_enbaya_pose *f6t9 = &a.poses[(size_t)6 * a.track_count + 9];
    assert(closef(f6t9->quat[0], -0.33827f, 1e-3f));
    assert(closef(f6t9->quat[1], -0.43589f, 1e-3f));
    assert(closef(f6t9->quat[2], 0.15477f, 1e-3f));
    assert(closef(f6t9->quat[3], 0.81952f, 1e-3f));
    const igb_enbaya_pose *f6t11 = &a.poses[(size_t)6 * a.track_count + 11];
    assert(closef(f6t11->quat[1], -0.31195f, 1e-3f));
    const igb_enbaya_pose *f6t29 = &a.poses[(size_t)6 * a.track_count + 29];
    assert(closef(f6t29->quat[2], 0.84393f, 1e-3f));
    const igb_enbaya_pose *f6t30 = &a.poses[(size_t)6 * a.track_count + 30];
    assert(closef(f6t30->quat[1], 0.03094f, 1e-3f));

    /* pose_at clamps and samples the right keyframe. */
    igb_enbaya_pose *at = (igb_enbaya_pose *)malloc((size_t)a.track_count * sizeof(*at));
    assert(at);
    igb_enbaya_pose_at(&a, 0.11f, at);
    for (int t = 0; t < a.track_count; ++t) {
        const igb_enbaya_pose *p2 = &a.poses[(size_t)2 * a.track_count + t];
        assert(closef(at[t].quat[0], p2->quat[0], 1e-4f));
        assert(closef(at[t].pos[1], p2->pos[1], 1e-4f));
    }
    free(at);

    igb_enbaya_free(&a);
    printf("test_enbaya: OK\n");
    return 0;
}
