#ifndef X2_AUDIO_CHANNEL_POLL_H
#define X2_AUDIO_CHANNEL_POLL_H

struct X86pCpu;

#ifdef __cplusplus
extern "C" {
#endif

/* Native replacement for XMen2.exe!0x00594500 (per-frame audio channel poll).
 */
void x2_override_00594500(struct X86pCpu *C);

/* The poll body itself, without the CPU-frame bookkeeping -- exposed for the
   differential verifier and the unit test. */
void audio_channel_poll_run(void);

#ifdef __cplusplus
}
#endif

#endif /* X2_AUDIO_CHANNEL_POLL_H */
