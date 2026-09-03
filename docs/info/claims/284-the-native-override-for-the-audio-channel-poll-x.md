---
id: C284
kind: claim
status: holds
created: 2026-09-04
tags: 
depends: src/native/audio_channel_poll.c, src/native/audio_channel_poll_verify.c, src/native/dsound.c
---

## Claim

The native override for the audio channel poll (XMen2.exe!0x00594500) is behaviour-equivalent to the retail guest body: it walks the same 24-entry channel table at 0x00804198, checks IDirectSoundBuffer status via dsound.c, releases finished owned buffers, frees slots in the 0x00804098 table, and drops non-owning finished channels to state 1.

## Evidence

objdump disasm of 0x00594500; tests/test_audio_channel_poll.c (test #77, all branches + guard + counter); audio.channel_poll_verify=1 over an 800-frame driven act0/tutorial/tutorial1 run reported 'native poll matches the guest body' with 0 divergences; with audio.channel_poll=1 the profile blocks 0x00594524/0x00594578 leave jit.profile top 40 (were #3/#4 at 0.6% each in the audio.channel_poll=0 baseline).

## What would falsify it

the retail body at 0x00594500 changes, the channel table layout (0x00804198 stride 16 / 0x00804098) changes, or audio.channel_poll_verify reports a divergence in a driven run
