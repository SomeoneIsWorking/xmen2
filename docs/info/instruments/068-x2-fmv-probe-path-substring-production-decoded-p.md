---
id: I068
kind: instrument
status: trusted
created: 2026-08-22
---

## Instrument

X2_FMV_PROBE=<path-substring> production decoded/padded/D3D8-upload row-chain verifier

## Validated by

test_fmv_probe deliberately mutates one padded row and one upload row and requires both mismatch counters to move before an exact chain completes; a full external cine01.sfd replay then reported 4,357 complete chains and zero mismatched production rows at the exact issue-95 scene.

## Known failure modes

It is inactive unless the configured substring matches the guest movie path.
It compares visible BGRA rows only through the successful level-0 power-of-two
texture upload; it does not validate audio samples, compressed/non-BGRA movie
textures, GPU sampling after upload, or frames the presentation clock drops
before the guest image copy. Every report carries decoded, padded, upload, and
completed-chain denominators so those gaps remain visible.
