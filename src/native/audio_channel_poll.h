#pragma once

struct X86pCpu;

namespace x2::native {

/* Native replacement for XMen2.exe!0x00594500 (per-frame audio channel poll).
 */
void override_00594500(struct X86pCpu *C);

/* The poll body itself, without the CPU-frame bookkeeping -- exposed for the
   differential verifier and the unit test. */
void audio_channel_poll_run(void);

} // namespace x2::native
