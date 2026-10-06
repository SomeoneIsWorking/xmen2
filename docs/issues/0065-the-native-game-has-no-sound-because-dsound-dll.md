---
id: 65
title: The native game has no sound because DSOUND.DLL is unavailable
status: resolved
symptom: kernel32: LoadLibraryA("DSOUND.DLL") -> NULL; native gameplay renders but produces no game audio
tags: pc,native,audio,dsound,sdl,engine
created: 2026-08-14
updated: 2026-08-14
---

## Root cause


## What was tried / dead ends


## Resolution

### Resolution (2026-08-14)
Root cause: the native loader truthfully returned NULL for DSOUND.DLL, so XMen2.exe FUN_00594290 returned audio-init error 0x101 and the entire native run was silent. Static RE located its exact boundary: LoadLibraryA/GetProcAddress(DirectSoundCreate), IDirectSound SetCooperativeLevel/GetCaps/CreateSoundBuffer, then primary-buffer SetFormat/Play. The runtime creates PCM secondaries, fills Lock/Unlock ranges, duplicates static samples into independent voices, and controls cursor/volume/pan/frequency/play/stop. src/native/dsound.c now publishes that dynamic export and implements those object semantics over an SDL3 F32 stereo mixer. A first fixed 256-object registry was wrong: the tutorial crosses it, CreateSoundBuffer failed, the guest ignored HRESULT and dereferenced NULL in FUN_00595fa0. The registry now grows, and a 300-object discriminator fails under the old ceiling. Real run to frame 2900: 270 secondary buffers, 18 duplicates, 485 locks, 28 plays, 2,665,472 mixed frames, 1,249,706 nonzero samples, peak 1.0, zero refused GPU draws, clean frame-limit exit.
