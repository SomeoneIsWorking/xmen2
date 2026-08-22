# Native SFD playback

The shipped movie set is deliberately the scope of the native player. An
`ffprobe` inventory of all fourteen files (`cine01`–`cine07`, `i101`–`i107`)
found the same stream combination in every file:

- MPEG program-stream container (`mpeg`)
- 640×480 MPEG-1 video (`mpeg1video`, YUV420P)
- 44.1 kHz stereo CRI ADX ADPCM (`adpcm_adx`)

`src/media/fmv_policy.c` is the production gate for that exact combination.
It refuses another container or codec by name; this is not a general media
player whose untested formats happen to open.

## Retail seam

The replacement is at the `igCriMovieCodec` virtual interface, not at
`libMovie`'s scene manager. Ghidra decompilation and the raw vtable bytes in
the shipped `libCriMovie.dll` establish these entries:

| Vtable | Entry point | Method |
|---:|---:|---|
| `+0x58` | `0x10001990` | `setContext` |
| `+0x5c` | `0x10001ab0` | `loadMovie` |
| `+0x60` | `0x10001fa0` | `unloadMovie` |
| `+0x64` | `0x10002040` | `playMovie` |
| `+0x68` | `0x100020c0` | `pauseMovie` |
| `+0x6c` | `0x100021c0` | `nextFrame` |
| `+0x70` | `0x10002140` | `checkState` |

`libMovie` creates the codec and calls `setContext`/`loadMovie`, calls `play`
from its begin path, calls `pause(1)` then `unload` during removal, and calls
`nextFrame` before uploading the runtime `igImage` to its textures. Those
scene, texture, lifetime, and authored-callback paths remain guest code.

The relevant `igMovieInfo` fields are path `+0x14`, dimensions `+0x20/+0x24`,
state `+0x50`, and runtime image `+0x58`. `libMovie` constructs that image as
format `0x65`; its allocation byte count is at image `+0x30` and pixels at
image `+0x34`. The bridge validates the allocation-derived pitch and writes
BGRA rows there. It never owns a GPU object and therefore cannot bypass the
engine's existing dirty/update/upload contract.

The runtime allocation is power-of-two storage even though the image keeps
the 640×480 display dimensions: the live bridge receives 2,097,152 bytes,
exactly 1024×512×4, and therefore a 4,096-byte row pitch. This agrees with
`buildScene`'s explicit next-power-of-two geometry. `movie_image_layout`
derives and validates that layout; treating the whole allocation as 480 equal
rows was correctly refused by the first bounded run rather than copied with an
invented pitch.

## Host ownership and timing

`src/media/fmv_player.c` owns FFmpeg demux, MPEG-1/ADX decode, BGRA conversion,
and bounded video queueing. Video uses `AVFrame.best_effort_timestamp`; missing
or non-monotonic values use the stream frame duration and a monotonic fallback.
This is required by the shipped stream: FFmpeg's command-line decode reported
a non-monotonic DTS while still decoding its frames correctly.

`src/audio/movie_audio.c` owns one locked stereo-F32 streaming voice. The
existing DirectSound SDL callback mixes it with the game's other voices, and
the existing silent timed device advances it in windowless runs. The decoder
knows only the queue callback and queued duration, not SDL.

`src/native/movie.c` is only the guest ABI bridge. The six replaced bodies stay
emitted and linked. `X2_NATIVE_FMV=0` calls them directly for A/B comparison;
`X2_SPIN=spin` independently selects the retained decoder rendezvous loop.

## Verification

`fmv_policy`, `fmv_timing`, and `movie_audio` exercise production interfaces.
`fmv_decode` skips with code 77 until `X2_TEST_SFD` names a user-provided shipped
file outside git. Against `cine01.sfd`, it decoded 177 changed frames over a
six-second window (119 distinct frame hashes), 288,256 audio frames, and copied
all 480 rows identically through both tight and padded destinations while
leaving every padding sentinel intact. At least one decoded frame had real
lower-half picture range. No asset path or asset is stored in the repository.

A bounded 360-presented-frame engine run reached `i102.sfd`, used the retail
image-to-texture path, and reported 236 video frames decoded, 229 selected for
display, 347,392 audio frames, and zero decoder/copy failures. A second bounded
capture kept engine frames 300, 330, 360, 390, and 420: the native Activision
logo presented cleanly through the guest texture upload, including the lower
half, with no old horizontal block corruption. The run stopped through
`X2_MAX_FRAMES`, not a timeout or fallback.

Issue #95 remains open only because its original observation was a later story
FMV. The intro capture proves the complete replacement path, not that exact
authored scene; the old guest decoder's precise corruption mechanism was never
isolated and is not claimed here.
