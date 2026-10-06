#ifndef X2_AUDIO_CHANNEL_POLL_VERIFY_H
#define X2_AUDIO_CHANNEL_POLL_VERIFY_H

struct X86pCpu;

/* Differential gate for the 0x00594500 native override, armed by the runtime
   cvar audio.channel_poll_verify. When active it runs the retail guest body,
   captures its guest-memory effects, rewinds, runs the native poll, and
   aborts on any divergence. Returns 1 when it ran (the caller must not also
   run the native poll), 0 when the gate is disabled. */
int audio_channel_poll_verify(struct X86pCpu *C);

#endif /* X2_AUDIO_CHANNEL_POLL_VERIFY_H */
