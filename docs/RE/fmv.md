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

`src/media/fmv_player.c` owns FFmpeg demux, MPEG-1 decode, BGRA conversion, and
bounded video queueing. `src/media/fmv_audio_decode.c` owns ADX receive,
stereo-F32 resampling, sink queueing, and the resampler tail. Video uses
`AVFrame.best_effort_timestamp`; missing
or non-monotonic values use the stream frame duration and a monotonic fallback.
This is required by the shipped stream: FFmpeg's command-line decode reported
a non-monotonic DTS while still decoding its frames correctly.

`src/audio/movie_audio.c` owns one locked stereo-F32 streaming voice. The
existing DirectSound SDL callback mixes it with the game's other voices, and
the existing silent timed device advances it in windowless runs. The decoder
knows only the queue callback and queued duration, not SDL.

`src/media/fmv_decoder_drain.c` owns the shared EOF contract. Sending a NULL
packet, receiving `AVERROR_EOF`, and emptying the optional converter tail are
three distinct states. Output backpressure may end one update without declaring
the decoder drained; the next update resumes receive before finish is allowed.

`src/native/movie.c` is only the guest ABI bridge. The six replaced functions
remain in the retail image and execute through the JIT when selected for A/B
comparison; `X2_SPIN=spin` independently selects the retained decoder
rendezvous loop.

`src/media/fmv_probe.c` is the opt-in production-path verifier. When
`X2_FMV_PROBE` matches a movie path, it retains the selected decoded BGRA frame
and compares every visible row after the padded `igImage` copy and again at the
successful D3D8 level-0 upload. It reports all denominators and mismatch rows;
the focused test deliberately changes one padded row and one upload row to
prove both comparisons can report the other answer.
`src/d3d8/d3d8_texture_luma.c` separately owns the existing texture-luma
diagnostic extracted from the resource implementation. The validated row-chain
instrument is recorded as I068.

## Verification

`fmv_policy`, `fmv_timing`, and `movie_audio` exercise production interfaces.
`fmv_decoder_drain` forces eighteen delayed frames through a sixteen-frame
output queue: the first drain stops full after sixteen with its flush sent but
decoder not drained, and the next emits frames seventeen and eighteen before
EOF. Its audio case emits both delayed decoder output and converter-tail output.
`fmv_decode` skips with code 77 until `X2_TEST_SFD` names a user-provided shipped
file outside git. Against `cine01.sfd`, it decoded 177 changed frames over a
six-second window (119 distinct frame hashes), 288,256 audio frames, and copied
all 480 rows identically through both tight and padded destinations while
leaving every padding sentinel intact. At least one decoded frame had real
lower-half picture range. No asset path or asset is stored in the repository.

The same real-file test then drains the complete movie. With independent
`ffprobe` counts supplied as its oracle, `cine01.sfd` reaches `FINISHED` at
exact parity: 4,478 decoded video frames and 6,602,752 queued audio frames.
This specifically falsifies the former finish-on-flush-sent behavior (issue
#109), including the decoder and resampler tails.

A bounded 360-presented-frame engine run reached `i102.sfd`, used the retail
image-to-texture path, and reported 236 video frames decoded, 229 selected for
display, 347,392 audio frames, and zero decoder/copy failures. A second bounded
capture kept engine frames 300, 330, 360, 390, and 420: the native Activision
logo presented cleanly through the guest texture upload, including the lower
half, with no old horizontal block corruption. The run stopped through
`X2_MAX_FRAMES`, not a timeout or fallback.

The exact issue #95 story scene was then reached deterministically through New
Game / Normal, with observation gated on the open of `cine01.sfd`. A retained
capture at presented frame 115,000 matches the original close-up and has a clean
lower half. Across the complete movie, the production probe recorded 4,478
decoded frames, 4,357 padded checks and 4,357 upload candidates: every upload
was byte-exact, no row mismatched, and all 4,357 chains completed. Playback
reported 6,602,752 audio samples and zero failures. This confines the old
corruption to the replaced guest CriMovie decode/output coordination because
libMovie's guest image and texture upload are shared; it does not identify the
legacy decoder's deeper internal fault.
