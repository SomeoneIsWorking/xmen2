# Enbaya animation decode (Raven `engb`) — fully reversed

Reverse-engineered from `libIGSg.dll` (PC debug build, `scratch/ref/alchemy5/.../DirectX9/libdbg/`)
with Ghidra. Prototyped in Python (`scratch/logs/dec2.py`); validates structurally against
the real `03_wolverine.IGB` walk animation (all sub-streams consumed exactly to their
region ends after the full cycle).

## Entry points (all in libIGSg.dll)

- `igEnbayaAnimationSource::getComponentValues` @ `0x10051eb0` — per-bone eval.
- `igEnbayaTransformSource::getComponentValues` @ `0x10013860` — wraps source + trackId.
- `igEnbayaAnimationState::activate` @ `0x10053100` — binds context.
- `igEnbayaAnimationState::_currentContext` — global set to the active `PgAnimationContextImp`.
- `FUN_1007f280` — `PgAnimationContextImp` init from stream + trackCount + fastCache.

## Stream blob (`igEnbayaAnimationSource` slot-2 mem, here 666 bytes)

Serialized `PgAnimationStreamImp` = 0x50-byte header + sub-stream data. Wolverine walk:

```
+0x00 u32   0x10079c10   # magic/format tag (replaced by real vtable at load)
+0x04 u32   33           # track count (bones)
+0x08 f32   0.003996     # DELTA SCALE  (integrate: kf = prev + accum * scale)
+0x0c f32   0.3          # duration (s)   (getDuration reads stream+0xc)
+0x10 u32   20            # frames/sec -> interval = 1/20 = 0.05s => 6 frames
+0x14..0x44 13×u32        # cumulative sub-stream cursor offsets
+0x48 u32   33            # final region length (total data = sum(+0x14..+0x48) = 586)
+0x4c u32   self-ptr to +0x50
+0x50       data          # = base
```

Sub-stream cursor offsets (cumulative, from accessors `FUN_1007f420`..`FUN_1007f5a0`):

| offset | region | used by |
|--------|--------|---------|
| 0      | 106 B  | initial-value shorts (+ reader2 ints / reader3 ints, unused) |
| 106    | 16 B   | reader2 (delta) short values  (8 shorts) |
| 122    | 4 B    | reader3 (run-length) short values |
| 126    | 58 B   | reader1 (initial) tag stream  (4 tags/byte) |
| 184    | 60 B   | reader1 byte values |
| 244    | 116 B  | reader2 tag stream |
| 360    | 117 B  | reader2 nibble-code stream  (2 codes/byte) |
| 477    | 73 B   | reader2 byte values |
| 550    | 1 B    | reader3 tag stream |
| 551    | 2 B    | reader3 byte values |
| 553    | 33 B   | per-track initial channel flags (trackCount bytes) |

## Lookup tables (libIGSg.dll)

```
DAT_101a8004 = {6,4,2,0}      # 2-bit tag bit-shifts (MSB-first), all readers
DAT_101a8014 = {4,0,...}      # 4-bit code bit-shifts (MSB-first), nibble stream
DAT_10091668 = {0,1,0,-1}     # tag 0..3 -> const value (tag1=+1, tag3=-1)
DAT_10091678 = {0,8,2,3,4,5,6,7,-8,-7,-6,-5,-4,-3,-2,-1}  # 4-bit code -> value
DAT_100a56e0 ~ 0              # eps for frame-step (small float)
DAT_100a1af0 ~ 0              # zero compare (as float ~ 0)
```

## Readers (stream cursors)

Each reader = [tag_ptr, (code_ptr), byte_ptr, short_ptr, int_ptr] + bitpos + (cbitpos).
All values advance a per-width cursor only (the same bytes can be re-read at different
widths; unused width-regions simply sit at 0).

- **FUN_1007ff40** `@0x1007ff40` (initial values): tag→0/byte/short/int.
- **FUN_1007fc00** `@0x1007fc00` (run-lengths, unsigned): tag→0/u8/u16/u32.
- **FUN_10080140** `@0x10080140` (delta values): tag→ `DAT_10091668[tag]`, tag2 → nibble
  code → `DAT_10091678[code]`, code0 → raw byte (bias ±127/128), byte0 → short, short0 → int.
  (backward twin `FUN_100803c0`.)

## Context (`PgAnimationContextImp`, FUN_1007f280 / FUN_1007f8d0)

- `+0x00 i32` frame counter; `+0x04/+0x08` next/prev keyframe times.
- `+0x0c` header ptr (stream object). `+0x10` bone-buffer base (per-track 0x60 B:
  quat[4]+pos[3] at +0, second buffer +0x20, accum deltas +0x40, flags byte +0x5c).
- `+0x24` interval = 1/fps. `+0x28` channel state [runlen, remaining].
- `+0x30` initial flags ptr; `+0x34/0x48/0x60` readers; `+0xa8` dir byte; `+0xa9` buffer idx.
- `+0xb0` output keyframe cache: `base + buf*0x20 + track*0x60`, entry = quat[4]+pos[3]+time.

Decode per frame step (`FUN_1007f8d0` forward loop):
1. advance channel state (`FUN_100805b0`): when runlen==0 toggle a channel flag bit and
   read next run-length; else skip.  (Flags persist across frames — one global schedule.)
2. `FUN_10080040`: for each flagged component read a delta value (reader2) and ADD to accum.
3. `FUN_1007f670`: `next_kf = prev_kf + accum * scale` (scale = stream+0x08, negated on
   reverse), time = frame*interval. Normalize quat.
4. Init (`FUN_1007fe90` + `FUN_1007f7b0`): accum = 7 initial values/track × duration-as-scale,
   first kf = that, zero accum, flags = initial flags byte/track.

Keyframes: 1 init pose + `ceil(duration/interval)` stepped poses (frame counter increments
per step; the first step at frame 0 skips the channel advance but still applies deltas).
The LAST step can carry nonzero deltas (walk tracks 9/11/29/30 change on step 6) — it is
not a silent hold; it is the pose shown at t == duration before the loop rewinds.

## Time conversion

`FUN_100520e0`: global int64 time -> float seconds. `FUN_100521a0`: wrap into
[0, duration] by play mode (1 = clamp, 2 = pingpong, ...). `_currentContext` must be set;
interp factor = (localTime - prevFrameTime)/interval, slerp quat (`FUN_10052650`) /
lerp vec (`FUN_100527c0`) with source+0x08/+0x09 interp modes.

## Validate

- `python3 scratch/logs/dec2.py` — reference decode; streams consumed exactly to the
  region ends after the full cycle.
- `src/core/igb_anim.c` — production C port, verified identical to the Python oracle on
  every track (values match; C adds the engine's quaternion normalization).
- `./build/igb_dump -anim <file.igb> [source_index] [frame]` — decode any
  igEnbayaAnimationSource in an IGB (27 in 03_wolverine.IGB).
- `tests/test_enbaya.c` — regression test embedding the real walk blob.
