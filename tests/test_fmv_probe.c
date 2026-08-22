#include "fmv_probe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    uint8_t decoded[3 * 3 * 4];
    uint8_t padded[3 * 20];
    uint8_t upload[4 * 4 * 4];
    X2FmvProbeStats stats;
    int row;
    int failures = 0;
    for (row = 0; row < (int)sizeof(decoded); ++row)
        decoded[row] = (uint8_t)(row * 7 + 3);
    memset(padded, 0xa5, sizeof(padded));
    memset(upload, 0, sizeof(upload));
    setenv("X2_FMV_PROBE", "cine01.sfd", 1);
    x2_fmv_probe_begin("movies/ntsc/eng/c/1/cine01.sfd");
    x2_fmv_probe_decoded(decoded, 3, 3, 12);
    for (row = 0; row < 3; ++row) {
        memcpy(padded + row * 20, decoded + row * 12, 12);
        memcpy(upload + row * 16, decoded + row * 12, 12);
    }
    padded[20 + 5] ^= 0xff;
    x2_fmv_probe_padded(padded, sizeof(padded), 20);
    padded[20 + 5] ^= 0xff;
    x2_fmv_probe_padded(padded, sizeof(padded), 20);
    upload[16 + 5] ^= 0xff;
    x2_fmv_probe_upload(upload, sizeof(upload), 4, 4, 16);
    upload[16 + 5] ^= 0xff;
    x2_fmv_probe_upload(upload, sizeof(upload), 4, 4, 16);
    x2_fmv_probe_get_stats(&stats);
    failures += !stats.active || stats.decoded_frames != 1;
    failures += stats.padded_checks != 2 || stats.padded_mismatch_rows != 1;
    failures += stats.upload_candidates != 2 || stats.upload_matches != 1;
    failures += stats.upload_mismatch_rows != 1 || stats.complete_frames != 1;
    x2_fmv_probe_end();
    x2_fmv_probe_begin("movies/ntsc/eng/i/1/i102.sfd");
    x2_fmv_probe_get_stats(&stats);
    failures += stats.active || stats.decoded_frames != 0;
    x2_fmv_probe_end();
    printf("FMV row-chain probe: %s -- deliberate padded/upload row "
           "mutations were detected and the exact chain completed once\n",
           failures ? "FAILED" : "PASSED");
    return failures != 0;
}
