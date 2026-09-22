# X-Men Legends II project state

## Comparison baseline

The baseline is the unmodified 2005 Windows PC release of *X-Men Legends II* running on Windows or
through Wine, with its original Direct3D 8 renderer, PC control defaults, prompts, settings, loading,
and save flow. The port's intended differences are Wine-free native execution and a modern native-PC
presentation, controller, settings, packaging, and diagnostics experience without changing the game.

This is the authoritative inventory of what the port demonstrably does now and
what remains partial, blocked, or absent. Epic intent belongs in
[`project-goals.md`](project-goals.md), atomic work in [`issues/`](issues/),
ownership in [`codemap.md`](codemap.md), and the ordered binary-evidence chain
in [`re-frontier.md`](re-frontier.md).

States: `verified` means the stated outcome was observed with durable evidence;
`partial` names both the demonstrated subset and the exact remaining gap;
`blocked` names its blocker; and `missing` means the capability is absent.

## Current focus

**S022 — native Windows host package and CI release.** The active work is to
close the title's Windows VM, file, synchronization, and remaining host
boundaries before adding a real Windows build and release artifact.

## Capability inventory

| ID | Capability or outcome | State | Factual dependency | Goals |
|---|---|---|---|---|
| S001 | Fresh-clone provisioning and the default native launcher | partial | — | G001, G005 |
| S002 | Native-overrides + x86port-JIT gameplay execution | partial | S001 | G001, G002 |
| S003 | Faithful rendering of the reached game path | partial | S002 | G002 |
| S004 | Native Alchemy 2D/UI rendering above the D3D8 seam | partial | S003, S012 | G002, G004, G006, G007 |
| S005 | Native audio mixing and SFD movie playback | verified | S002 | G002 |
| S006 | SDL3 keyboard/controller/mouse input, assignment, and hotswap | partial | S002 | G002, G004 |
| S007 | Xbox-derived controller defaults and source-sensitive prompts | partial | S006 | G002, G004 |
| S008 | Port-owned RmlUi settings and input-binding surface | partial | S003, S006 | G002, G004, G007 |
| S009 | Direct development boot into an initialized game | partial | S002 | G002, G004 |
| S010 | Measured frame and level-load performance | partial | S002, S016 | G003 |
| S011 | Wine oracle and evidence-backed differential RE workflow | partial | — | G002, G006 |
| S012 | A/B-toggleable native overrides and incremental engine replacement | partial | S002, S011 | G001, G006 |
| S014 | Apple Silicon macOS native host support | partial | S001 | G005 |
| S015 | Transactional autosave and direct retail Continue restore | verified | S002 | G002 |
| S016 | Live control, capture, input, and runtime diagnostic channel | verified | S002, S003 | G002, G006 |
| S017 | Linux AppImage packaging and no-terminal install setup | partial | S001, S008 | G005 |
| S018 | Android APK shell and measured mobile performance | partial | S002, S006, S010, S020 | G005, G007 |
| S020 | Platform-neutral touch play on any touchscreen | partial | S002, S006 | G005, G007 |
| S021 | Web (WASM + PWA) product with browser-side install | partial | S001, S020, W1, W2, W3 | G005 |
| S022 | Native Windows host package and CI release | missing | S001, S002 | G005 |
| S019 | Proven shared Alchemy gameplay boundary and deferred MUA adoption | partial | S004, S006, S012 | G006 |

## State details and evidence

### S001 — fresh-clone provisioning and launcher: partial

Observed subset: earlier Linux x86-64 and Apple Silicon macOS cold-path runs
proved that zero-argument `./run.sh` enters the locked `uv` environment,
validates user-supplied game files, restores redistributable dependencies and
native assets without Ghidra, and launches the native D3D8-backed product. The
current bootstrap no longer emits guest code; it pins `jit-common` and
`x86port`, and runtime module discovery reads the user's PE images directly.

Evidence: C182 records a cold-path run after the virtual environment, native
assets, shared dependencies, and build tree were moved aside;
the launcher recreated the then-current inputs, presented frames, and passed
its launcher and CTest controls. Issue #125 records and tests the boundary that
maintainer-only `re-harness` is not a player bootstrap dependency.

The resolver was freshly reprovisioned against published immutable revisions
for `x86port`, `jit-common`, and Alchemy, including x86port
`ca52e377040d83534f9cd9f5a976526078fa2343`; each resulting checkout matched
its required revision and was clean.

Gap: C182 predates the current runtime-JIT bootstrap and therefore does not
verify the complete current cold launcher path. That path still needs one
fresh-clone-equivalent zero-argument run through provisioning, product build,
and launch; dependency reprovision alone does not prove the whole contract.

### S017 — Linux AppImage packaging and no-terminal install setup: partial

The repository now has a game-file-free AppImage staging path, portable UI
resource lookup, and an SDL3 first-run prompt that validates and remembers the
user's `XMen2.exe` directory in the OS configuration directory. It also accepts
a ZIP containing exactly one `XMen2.exe` at any nested path and extracts it
under that same user-data root through the shared safe ZIP implementation.
The same generated validator derives the native runner's complete original-PC
image set from CMake's `X2_MODULES` and rejects an EXE, selected folder, or ZIP
that lacks any required sibling image before replacing the prior selection.
ZIP preparation is staged separately and atomically replaces only a validated
extraction, so an invalid replacement preserves the prior working install.
`build/release/X-Men-Legends-II-x86_64.AppImage` was rebuilt through current
linuxdeploy after its embedded pre-0.19 `patchelf` was proven to leave `DT_INIT`
stale while rewriting modern Fedora ELFs, causing a loader-time SIGSEGV. The
packager injects an external `patchelf` 0.19+ into linuxdeploy's temporary
payload and sets its supported `NO_STRIP=1` mode, preserving RELR-bearing
libraries while retaining the deployer's dependency scan. It runs the deployed
binary before writing the image and rejects any loader crash or failed selftest;
the final artifact independently passes its fresh no-install setup-state check.
Its extracted 241-file inventory contains the launcher, native binary, desktop
metadata, and SVG icon, with no `XMen2.exe`; SHA-256 is
`d6f211d0f61543bf3a43a0dbee56e7c70ba69fa4a99493541a896217e63c04db`.
The 143-test combined gate passes (two explicit data/tool skips), including the
configuration path, executable validation, and transactional replacement seams.
This digest is published in the public
[`v0.1.3` AppImage release](https://github.com/SomeoneIsWorking/xmen2/releases/tag/v0.1.3).

Gap: the interactive Browse flow has not been exercised on a clean Linux
desktop in this state record. The Android shell now has a native target,
setup/touch implementation, and ARM64 Cuttlefish install/menu evidence, but
the APK still lacks physical-device and performance evidence.

### S018 — Android APK shell and measured mobile performance: partial

The shared `android-port` framework owns Android Activity/SAF staging and has a
real ARM64 native target/Gradle assembly path. `android/` owns X-Men's setup UI
and publication after title validation. Android selection uses the same generated validator
before shared Android staging promotes files: it requires every loader PE image and
title-owned content sentinels spanning each boot-time asset family, so a
loader-only selection cannot become the retained install. Touch play itself is
S020, not this capability. An NDK 28
ARM64 build linked the combined `libmain.so`; the Activity now dispatches to its
exported `main`, Android launcher-icon resources compile with build-tools 36,
and release assembly refuses missing long-lived signing inputs instead of
emitting an unsigned APK. The 143-test combined native gate passes with the two
documented skips.

The Android build now pins Gradle 9.4.1 and Android Gradle Plugin 9.2.1, which
officially support this host's complete Java 26 JDK; the obsolete JDK 24 ceiling
is gone and the build rejects mismatched `java`/`javac` homes. A release assembly
completed all 50 AGP tasks under a one-day local verification key, and
`apksigner` verified its v3 signature; that artifact was deliberately not staged
as a release. The current setup is ZIP-only. The shared `android-port` Java
framework owns Activity/SAF staging, determinate byte progress, and resumable
staging under the package OBB directory. A resumed copy now verifies its staged
prefix against the reopened SAF source before appending; changed or truncated
sources are refused and their partial stage discarded. The shared Java contracts
pass, and the ARM64 debug APK assembles with this framework revision. Lucent remains the shared ZIP
extraction helper. Reinstall startup checks that retained package installation
before showing the picker when Android retained app data. OBB is app-specific:
an ordinary uninstall removes it, so reliable import-free uninstall/reinstall
still needs a shared-storage SAF design. The user reported that the Android
setup and game path runs on a device. An API 35 x86-64 emulator installed the debug APK, and
its ZIP control opened Android's DocumentsUI picker.

A loader-image-only DocumentsUI fixture exposed an install-validation defect:
it could reach SDL without gameplay content. The validator now also rejects an
empty or non-PE32 `XMen2.exe` before promotion, so malformed retained data stays
in `XMen2SetupActivity` instead of launching SDL and failing inside `pe_map`;
the focused picker/archive tests and rebuilt ARM64 APK cover that boundary.
Canonical private-path containment prevents
Android's `/data/data` and `/data/user/0` aliases from rejecting a valid
selection. This proves the packaged first-run shell and its incomplete-install
refusal, not importing a complete install or gameplay. The revised touch/HUD layout still
needs installed-APK visual and input verification. The shipping control status
now exposes exact bounded p50/p95/p99 frame timings, and
`tools/android_qualify.py` refuses a less-than-20-minute or incomplete-scenario
named-device collection while recording PSS and thermal-service observations.
The former shared API 35 emulator, with a 16 GiB data image, imported the
complete 1.57 GiB ZIP and promoted the resulting 2.37 GiB installation under
app-private storage, proving bounded staging and complete-install validation on
a real APK. An earlier Android run reached the runtime-translated executable and
faulted after `igArenaMemoryPool` called
`igPthreadSemaphore::obtainResource` with an invalid `0xffffffec` semaphore
field. That apparent startup fault is now resolved: `libIGCore`'s
retained registry-backed `igFile` setup has no Android value, so opening
`sounds/badaudio.wav` returned null. `engine_file_path.c` super-calls that body
and, at its live allocator seam, passes the selected install through the
retained setter as virtual `C:\\`. The generic case-insensitive resolver now
begins below that validated install root because the Android app cannot
enumerate `/data`; its trace resolves the guest request to the real
`Sounds/badaudio.wav`. A current Android 13 Waydroid image therefore runs the
API-21 debug APK from a deliberate debug-only complete app-private source,
maps the PC images, and reaches the retail difficulty menu. Its ZIP selection still receives a persisted read grant, but this image's
external-storage `MediaProvider` copy path fails its AppOps package check after
accepting the grant. The earlier
API-35 emulator import remains the complete production-setup proof.

The Android RmlUi overlay originally disabled its font engine despite the shared
prefix supplying FreeType. It now consumes that prefix, renders the circular
analog/action overlay, and a held Light touch visibly advances the real game to
the difficulty menu. This proves the Android contact-to-title-input route and
menu-state overlay feedback, not full gameplay HUD relocation. Waydroid's
roughly 700 ms frames are an emulator diagnostic only, not Android performance
evidence.

The debug APK now accepts a bounded
`com.someoneisworking.xmen2.debug.boot_map` intent extra and passes it through
the existing `startFirstMission` transition; `act1/deadzone/deadzone1` is the
documented combat profiling map. This maintainer path has been assembled for
ARM64, but a complete-install combat run with this exact build still needs
device evidence.

The current debug APK (`0.2.4`, `arm64-v8a`) was installed on the disposable
ARM64 Cuttlefish target and an unsafe `../unsafe` extra stayed in setup with the
visible refusal; no game files were present after the intentional uninstall,
so the valid combat-map handoff remains unexecuted there.

The complete import path was also exercised on the ARM64 Cuttlefish device:
the foreground notification reported live bounded-copy progress, the staged
tree promoted successfully, and the game reached 36,518 translated blocks with
zero JIT refusals and no Android fatal signal. The title now handles the
missing MPEG-PS metadata with a bounded byte-zero replay and explicit MPEG
parser; the host and exact ARM64 standalone `i102.sfd` tests both decode 312
video and 458,656 audio frames. The packaged run now reaches the retail main
menu after the first-movie startup latency on ARM64 Cuttlefish; pause/resume
recreation, interactive gameplay, and the named-device performance gate remain
open. The
implementation boundary and its falsifier are recorded in
[`0145`](issues/0145-android-arm64-first-movie-probe-stalls.md).

A 2026-09-11 run of the rebuilt debug APK on that same ARM64 Cuttlefish device
reached the title screen through Vulkan at 1280x720, presented 209 frames, and
executed 108.5M JIT block entries across 79,315 translated blocks with zero
translation refusals; the loopback control channel's `Return` was accepted by
the guest at frame 203. That device cannot produce Android performance
evidence: `simpleperf` attributes 35.7% of samples to `[anon:swiftshader_jit]`
and 17.5% to the guest `vulkan.pastel.so`, and the guest CPU itself runs under
QEMU TCG, so its 815 ms p50 frame time measures the emulator. Two
host-independent CPU costs found during that run were fixed with tests rather
than timings -- x86port's x87 binary128<->ext80 conversion churn (exact bit
reassembly, 28,372 checks) and an ungated per-upload texture-luma scan (now
armed by `X2_TEXTURE_LUMA` and reported as NOT MEASURED when off). Two further
lever was implemented and measured: `libmain.so` now builds with hidden
visibility and `-Wl,-Bsymbolic-functions`, which removes all 4,709
self-resolving PLT slots (5,290 -> 576 JUMP_SLOT), 6,564 exported dynamic
symbols and 1.07 MB from the stripped library -- and changes frame cost by
0.19%, inside noise, so it is kept for size and load, never cited as a speed
win. A 120-second thread-attributed profile explains why and bounds what this
device can show at all: SwiftShader's four rasteriser workers are 85.2% of
samples, the port's own thread 7.2%, audio 6.8%, and x86port's translated guest
code 0.63% of the whole process. The one remaining port-side lever it does
expose is emulated TLS at the API 21 floor -- `__emutls_get_address` is 1.4% of
the port's thread, concentrated in the per-thread guest call stack -- which is
open. See
[`0147`](issues/0147-arm64-android-cpu-ext80-conversion-churn-and-an.md).
The named-device performance gate stays unmet.

First physical-device profile, Huawei BKY-W09 (arm64, Vulkan, 1280x720,
2026-09-11): gameplay ran at 88.2 ms/frame. Attribution came from the
heartbeat's frame-phase line plus two instruments added for it -- the swapchain
acquisition wait (`gpu_frame_timing_note_swapchain_wait`) and a control-channel
route that arms the hot-guest-entry-point probe on a released build
(`GET /performance/probe?n=`). The swapchain wait was 2.9 ms/frame, so the frame
is host-bound, not GPU- or compositor-bound. Host uploads were 25.4 ms/frame of
which 22.9 ms was submission: each of ~34 uploads per frame recorded its own
command buffer and submitted it, against only ~550 KB of actual data. Batching
the frame's copies into one command buffer (`src/gpu/gpu_upload_batch.c`) cut
host upload to 1.6 ms/frame, host draw to 8.6 ms/frame and the swapchain wait to
1.5 ms/frame, and the frame to 81.2 ms. The GPU layer is now ~10 ms of that
frame; the remaining ~70 ms is the D3D8 import boundary and guest execution --
the probe attributes 71% of crossing time to host imports, against ~1,490
`SetVertexShaderConstant` and ~1,200 `SetTextureStageState` calls per frame.
That boundary is the next target and the gate remains unmet.

Upload staging no longer allocates per upload. Each resource kept its own
transfer buffer and every upload cycled it, which SDL must answer with a new
allocation when the open command buffer has already referenced the old one --
so a buffer uploaded twice in a frame paid for two. Measured on the Dead Zone
map: 308,362 uploads had asked the driver for 57,856 transfer buffers, about
31 new GPU allocations a frame for roughly 550 KB of data, and `perf` put
35.6% of all cycles in the driver's virtual-address allocator
(`amdgpu_vamgr_find_va`) beneath `gpu_upload_stage`. `src/gpu/gpu_staging_ring.c`
writes every upload at its own offset in a shared page, which SDL states
needs no cycling, and cycles a page once a frame at the batch submit. Same map,
same route: frame wall 88.4 -> 49.6 ms, p50 82.0 -> 48.6 ms, host upload 85.04
-> 0.09 ms/frame, host draw 5.1 -> 1.0 ms/frame, and one transfer-buffer
allocation for the whole run. The picture is unchanged -- `deadzone-render` 6
of 6 and all 15 GPU selftests pass, including the two-generation upload-order
check (issue #182). This is desktop evidence for work removed, not Android
performance evidence; the same driver path is what a device pays for too. The
remaining frame is about 62% x87 softfloat in `shared/x86port`, and the JIT
reports its x87-widening, x87-narrowing and host-SIMD lowering as `0 of 0` on
this title, so those levers are dormant rather than exhausted.

Gap: x86port now has an ARM64 emitter and runtime backend, but Android
executable-memory, ABI, instruction-cache, and representative gameplay
qualification are still incomplete; neither the test interpreter nor bounded
per-block fallback can substitute for that host evidence. A publishable APK also requires a stable physical Android test device,
full-gameplay HUD verification, the maintainer's long-lived keystore, and
measured named-device performance. The required setup, touch-zone mapping, and
device/thermal/frame-time evidence gate are specified in
[`android-release.md`](android-release.md); desktop and Apple Silicon results do
not count as Android performance evidence.

### S002 — native-overrides + x86port-JIT gameplay execution: partial, current focus

Observed subset: `x2native` maps and initializes the original PE images at
runtime, executes every non-native path through
`shared/x86port`'s x86-64 or ARM64 JIT, and supplies the reached
Win32/CRT/DirectInput/DirectSound/D3D8 boundaries. Native overrides hand back to
the shipping dispatcher by module plus address and can call their original
guest bodies through the JIT. Observed JIT runs have traversed menus, movies,
level load, gameplay, death, and return-to-menu paths. C288 and C290 record
hundreds of millions of in-game JIT block entries agreeing with the test
interpreter while native overrides were active.

Issue #140 fixed four JIT integration defects: translated blocks ran through
host interception points, a JITted thread never yielded the guest lock, the
guest-call stack was shared across threads, and the native-override hand-back
depended on a fixed-depth shadow frame. The current intrusive per-thread stack
has no separate call-context copy or depth cap; `tests/test_jit_intercept.c`
locks the override hand-back against deep nesting. The JIT reaches the title
screen and plays the intro reel through native FFmpeg. The native BehavEd
context/scheduler and title timed-event
players complete the verified tutorial control-lock cutscene synchronously,
silently, and without advancing guest frame/time (C274). Callback cleanup and
module-aware override routing are checked by C178 and C209.

The combined Clang/Ninja build links the published x86port revision
`4c23c0b86e09ba6ac47fd57c418f080c1ff6ed76` (the pinned successor `742fd04`
changes only hardware-oracle tests and their classification); source and linked-binary checks
prove that `x2native` requires `x86port_runtime` and exposes no explicit CPU
interpreter selector or verification mode. The earlier Linux startup refusal
at `PUSHFD` (`9c`, guest `0x103c30d2`) is covered by the shared runtime's new
startup instruction tests; the incoming Mac gameplay observations below are
separate from Linux validation of the combined tree.

Gap: x86port does not yet provide the permitted bounded fallback. If it is
added, the product boundary must prove that only failed/unsupported compilation
or unsafe execution can enter it and that every fallback interval is counted.
The reached startup and scene instructions are now implemented in shared
x86port (both backends; see S014 for the
driven-run evidence on the ARM64 host). The combined Linux launcher clears
the earlier startup refusal; this boot observation is not gameplay evidence. No bounded
representative interactive gameplay case has yet combined native overrides,
nonzero JIT execution, independent
CPU/memory/timing/device comparison, and the declared frame-time budget.
Apple Silicon now uses the real ARM64 JIT. Android still needs independent
host integration, packaging and runtime qualification; the Mac result does
not establish Android support.
Unreached imports remain fail-loud poison thunks; guest exception delivery,
LAN networking, and optional COM/system facilities are absent.

### S003 — reached-path rendering: partial

Observed subset: the live host D3D8 implementation feeds the guest-free SDL_GPU
backend and renders menus, FMVs, levels, skinned characters, UI, and the
measured full game loop. C142 proves non-flat native frames; C170 verifies the
observed fixed-function combiner behavior; C173 verifies the reached VS 1.1
skinning program; and the renderer frontier records zero refused draws on the
measured menu-to-gameplay-to-menu route. C273 proves that a frame without a game
colour clear initializes every untouched logical-backbuffer pixel to opaque
black instead of exposing recycled Metal/Vulkan attachment tiles. Issues #128
and #129 add 24-bit R8G8B8 actor textures and the Dead Zone's animated,
mip-filtered two-stage water material; the latter falsifies C154's earlier
single-stage generalization.

Gap: zero refusals proves reached API coverage, not pixel faithfulness.
Non-zero pixel shaders, unobserved combiner and vertex-shader forms, engine
off-screen render targets, and incomplete specular, spot-cone, and general fog
behavior remain fail-loud or unverified. The D3D8 compatibility
seam also remains beneath most engine rendering.

### S004 — native Alchemy 2D/UI rendering: partial

Observed subset: native prompt SVGs now cross above D3D8. C271 and
[`RE/text.md`](RE/text.md) establish that the port retains the executable's
text layout, brackets `igDxVisualContext::drawNonIndexed`, snapshots Alchemy's
finalized world/view/projection state from the nested context update, and
submits a port-owned RGBA atlas before the stock label. The windowless, silent,
unbounded keyboard evidence run submitted 1,188 quads in 99 prompt batches and
shows the native keycap aligned around retail `ENTER`. A separate synthetic-pad
run submitted 1,073 pure one-codepoint A icons through 1,073 matching nested
Alchemy finalizers, with zero desync, unavailable-byte, colour, capacity,
transform, cross-context, GPU, or unfinalized-boundary refusals; its capture
shows the native controller icon beside `CONTINUE...`. Issue #120 records the
font-baseline root cause and issue #121 records the pure-glyph finalizer and
atomicity root cause. Issue #133 traces the retail dialog's untextured selected
row back from D3D8 through its world-matrix ancestry to a title-side linear
scale that crosses zero at high output heights. The scoped extension preserves
the exact retail formula through 800x600 and holds its retail-relative share
above that reference; 800x600, 1280x720, and 3840x2160 cold-plus-warm live cases
each pass 15/15 with row heights of 20.04, 24.04, and 72.14 pixels (C275).

Native CHud presentation adapters intercept party draw (`0x005a43d0`),
vitals draw (`0x005a3320`), inventory draw (`0x005a5170`), and portrait draw
(`0x005a1ab0`). Within these scoped boundaries, 2D sprite submissions
(`0x0059a140`), 2D text submissions (`0x005f11b0`), and 3D scene node
matrices (`0x00570970`) are transformed into the mobile viewport layout
(`src/presentation/hud_layout.c`), relocating vitals and potions to the top-left
and character portraits to the top-right while moving the D-pad cross offscreen.
Single-player portrait positioning is natively owned by
`src/native/hud_portrait_position.c` (reproducing `0x005a1650`), verified against
the guest body under `hud.verify`. Output-pixel portrait bounds are published
to touch controls (`set_portraits`), and direct portrait tapping routes through
Win32 mouse translation to the transformed `PORTRAIT_CENTERS` (`0x00a0a0cc`),
selecting the tapped hero through the retail click handler (`0x005f9eb0`).
Port settings provide layout selection (Auto, Retail, Mobile), scale percentages
for vitals, potions, and portraits, and safe edge inset configuration. Asset root
boxes are diffed by code against `src/presentation/hud_geometry.h` across 117
retail PC assets via `tools/hud_geometry.py`.

Gap: this verifies the prompt SVG slice and the native CHud presentation
adapters. Stock ASCII, panels, non-HUD sprites, and other display-list geometry
still submit through the runtime-translated guest engine and D3D8 host. Each
semantic owner must be reverse-engineered, ported, and measured to zero old D3D8
calls before that portion of the seam can be deleted.

### S005 — native audio and SFD movies: verified

Observed capability: the native DirectSound boundary creates, fills,
duplicates, plays, and mixes secondary buffers, while the native FFmpeg-based
SFD bridge preserves the retail `libMovie` scene, image upload, timing, and
callback path. Timed silent runs preserve advancing play cursors without
opening a host playback device.

Evidence: C176 records 1,249,706 non-zero mixed samples from the game path;
C243 records decoded/displayed video and audio frames through the intro; C248
records 4,357 exact decoded-to-padded-to-upload chains and a clean capture of
the formerly corrupted story close-up. Issues #79, #95, and #109 preserve the
resolved backpressure, row-layout, and decoder-drain failures; C207 owns the
current movie-rendezvous evidence.

### S006 — SDL3 input, assignment, and hotswap: partial

Observed subset: keyboard and synthetic SDL3 pads enumerate through the game's
DirectInput 7/8 paths; axes, buttons, triggers, assignments, Start/Pause joins,
late attach, detach, reconnect, post-save-load polling, and active-source
switching have end-to-end evidence. C222 proves full-scale trigger delivery;
C224 proves RT+A reaches a gameplay power; C262 and C264 prove late attach both
before and after loading a save. The host also translates SDL pointer events
through an ordered Win32 message queue into the retained Alchemy WndProc,
mapping physical coordinates through inverse aspect fit to the active logical
backbuffer and giving focused retail content one game-drawn cursor. Pure tests
pin the message, mapping, button, coalescing, and cursor-visibility contracts.
The visible `mouse-click` case also drives a physical window click through
X11/SDL and the retained WndProc, opening the difficulty dialog.

Gap: every controller observation on this machine uses the synthetic pad. Real
hardware still must verify hotplug, stable identity, reconnect, assignment, and
full-range inputs; this cannot be promoted from synthetic evidence alone.

### S007 — Xbox defaults and prompt semantics: partial

Observed subset: the recovered Xbox controller assignments are installed
through the retained PC binding machinery, published to the binding banks the
game evaluates, and used by gameplay. The active player's last-used assigned
source selects keyboard or controller prose and prompt art. C187 and C215 pin
the recovered table and bank semantics; C227 proves the evidenced health-pack
mapping; C230 and C237 prove composed keycaps and source-sensitive prompt
selection. The native pixels themselves are covered by S004.

Gap: the retained PC action corresponding to Xbox White / Use Energy Pack is
not yet joined by sufficient PC-and-console evidence, so it remains omitted.
The entire preset and source policy also retain S006's physical-hardware gap.

### S008 — RmlUi settings and bindings: partial

Observed subset: a distinct Port Settings row opens the pinned RmlUi overlay in
the pause menu. The overlay shares the game's SDL_GPU command buffer and owns
window-mode choices, four keyboard profiles, controller assignment, rebinding,
persistence, migration, join/leave policy, and neutral input publication while
modal. A live end-to-end check configured Player 2 as Keyboard 2, rebound
Forward to `I`, and persisted `input.profile1.row0=23`; pure tests cover
ownership, migration, source switching, reconnect, and slot reuse. Resolution
selection now transactionally replaces the active logical D3D colour/depth
targets, updates the D3D8 backbuffer/depth descriptions and viewport, applies
the title's retained display dimensions/aspect/pixel scales and window
geometry, and persists only after all live presentation owners accept the
change; focused tests prove success and each rollback path. The resolution
control is a height preset -- 720p, 1080p, 1440p, 2160p -- whose width is
derived from the primary display's measured pixel aspect ratio rather than
picked from a fixed 16:9 table, so a 16:10 or 21:9 panel gets a mode that fills
it; presets taller than the display are not offered, and 720p always is. One
owner now answers "display size in pixels" for the ladder, GetDeviceCaps and
the D3D8 legality pass. The visible
800x600 -> 1280x720 case passed 8/8 and directly matched the live menu geometry
against a cold native-widescreen launch, preventing a 4:3 view from being
stretched across the 16:9 target.
Initial device creation also holds the configured mode on both retail branches:
issue #135's repeatable cold-plus-warm 3840x2160 case passed 13/13 checks,
including exact D3D device dimensions, persisted retail Resolution bytes, and
a same-profile warm capture at 3840x2160.

Gap: fullscreen transitions and real-controller identity/hotplug require
hardware/user validation, and controller UI navigation still uses focus
traversal rather than spatial navigation.

### S009 — direct initialized development boot: partial

Observed subset: `X2_BOOT_MAP=<map>` skips intro presentation and menus while
retaining BehavEd's retail `startFirstMission` party initialization. C223 and
[`RE/boot.md`](RE/boot.md) prove a resolved hero and the formerly suppressed
tutorial conversation on this path.

Gap: this is a development route, not a player-facing Start Game policy. A
normal new-game shortcut still needs explicit difficulty and save-slot policy;
Continue behavior is separately inventoried in S015.

### S010 — performance and load time: partial

Observed subset: frame intervals attribute guest versus host time. The native
movie rendezvous and upload paths have bounded-wait and retained staging fixes
with measured reductions (C207 and C233).

Issue #141 identified per-thunk unwinding of `x86p_jit_engine_run` as a
structural crossing cost. x86port `d5d3b00` added an inline between-blocks
dispatch hook, and xmen2 now services import thunks and override bodies without
unwinding the JIT slice (`x86_engine_dispatch.c`, CVar
`jit.inline_dispatch`, default on). On the same driven in-game input path,
host-import share of wall time fell from roughly 62% to 18% and frames rendered
per fixed wall-time window rose roughly 15%. Later targeted unpaced sessions
recorded roughly 60 fps after the native override work (C281-C283), superseding
the earlier roughly 30 fps diagnostic measurement.

Issue #143 adds checked bulk copies for forward, disjoint REP MOVS in the
pinned shared engine. A 100,000-copy 4 KB microbenchmark fell from 9,482.1 to
104.0 ns/copy; 11,247 copy-state checks pass on ARM64 and Rosetta x64. A fresh
tutorial window measured 17.78 ms median over 2,265 frame intervals with zero
JIT refusals in the live run. This measures a faster copy operation and
continued gameplay operation, not a proven whole-game FPS increase.

Issue #183 removed the x87 arithmetic path's memory round trips: on a host
with a real x87 unit the operation is now performed with its operands in
registers whenever the host control word already equals the guest's, which the
new mode census shows is 98.3% of this title's operations (all 98,792,279 of
them at 64-bit precision). `x86p_x87_arith_raw` fell from 15.20% to 4.64% of
the `deadzone-render` case and the case's total cycles from 56.3 G to 50.9 G.
x86port `1c30243`; both arms are held equal over 3,145,728 operand pairs in
value and in status word. The remaining x87 cost is the emission around it --
a register-operand FMUL still makes three indirect calls -- and
`x86p_jit_engine_run` is now the largest single symbol at 21.8%.

Gap: no target frame-time or load-time budget defines "fast enough." The later
unpaced results are targeted diagnostic cases, not bounded representative
product evidence; the roughly 500 ms load hitch remains visible, asset I/O has
not been profiled, and the `QueryPerformanceCounter` pacing spin remains open
(issue #141 option 2). Remaining CPU cost belongs to x86port JIT translation
quality rather than a title-local execution engine.

### S011 — oracle and differential RE workflow: partial

Observed subset: the stock PC build runs under Wine/Xvfb as a capturable oracle
(C005); proxy DLL load transparency and `__thiscall` interoperability are
verified; Ghidra exports, runtime missing-target discovery, claims,
instruments, issue catalog, and fail-loud translation checks provide durable
binary and runtime evidence. The oracle cache prevents repeated identical
control runs.

Gap: frame-level A/B is not deterministic across boot-movie timing, and broad
lockstep differential coverage does not exist. Most guest functions execute
through real paths but are not individually compared against the original;
the x87 surface in particular lacks constructed-object differential tests.

### S012 — native overrides and engine replacement: partial

Observed subset: module-qualified runtime overrides retain access to their
original guest bodies through JIT super-calls and A/B checks, and direct and
indirect calls route through the same override table (C209). Native owners now
replace selected boot, input, save, media, UI, and prompt-rendering behavior.
C270 measures the
current engine-to-D3D8 dialect, and S004 proves one semantic rendering slice can
be moved above it without inventing a lowered-D3D classifier. The in-game
cutscene player also ports the BehavEd timed-fiber and title timed-event pumps,
retains their ordinary strict-deadline behavior, and completes only causally
owned work synchronously. The visible-record and camera-only tutorial gates
pass 11/11 and 10/10 with control restored, no guest frame or clock advance,
zero dialogue-presentation leaks, and every reached cutscene-owned DirectSound
start suppressed (C274);
[`RE/cutscene_player.md`](RE/cutscene_player.md) records the binary chain.

Gap: most of the Alchemy renderer and game remain runtime-translated guest code.
Stock 2D, scene traversal, materials, lighting, shadows, render targets, and
their D3D8 call sites must be reverse-engineered and ported subsystem by
subsystem; `src/d3d8/` remains required until its last evidenced caller moves.

### S014 — Apple Silicon macOS native host: partial

Observed subset: the earlier arm64 Mach-O host kept macOS's normal 4 GB
`__PAGEZERO` and translates logical 32-bit guest addresses through a separate
4 GB arena. C272 and issues #10/#123 prove exact Win32 4 KiB mapping state over
Apple Silicon's 16 KiB host protection granule. The normal launcher discovers
Homebrew dependencies and a repository-local game directory; a driven run
cleared all six intro movies, entered playable gameplay, accepted keyboard
input, rendered through SDL_GPU/MoltenVK, and sustained world and shadow draws.

The published JIT build maps all 20 authenticated PC images, clears the
intro movies, presents the main menu and loads the tutorial through the retail
party initializer. Range-based executable-code publication removes quadratic
startup flushing; completed-thread signaling and real finite wait deadlines
remove the post-movie frame stalls. The menu and opening tutorial scene sustain
about 16.7 ms median frame intervals during menu/opening dialogue. A later
20,000-frame driven tutorial run completed dialogue and accepted hero movement,
entering 4,511,598,335 JIT blocks with zero refusals; a reset gameplay window
measured 21.24 ms median and 25.12 ms 95th percentile over 1,073 intervals. The follow-up floating-point store optimization preserves all sixteen
host/guest rounding-mode pairs and removes redundant host rounding transitions
(341 checks on ARM64 and x64); a new gameplay window measured 16.93 ms median
over 1,815 intervals. The shipping selftest reports zero
of 92 failures. Issue [#143](issues/0143-arm64-jit-startup-and-movie-thread-stalls.md)
records the samples, denominators and blind spots.

Gap: representative interactive gameplay and independent stock CPU/memory/
timing conformance are still incomplete. ARM64 x87 storage remains binary64;
the software transcendental path has tolerance-based tests, not exact x87
exception/precision conformance. Native Windows and physical controller/hotplug
plus clean-machine provisioning retain their separate validation gaps. The
combined dependency pins retain Fedora/Windows migration and CI contracts
alongside the published Mac startup and store-performance changes. The user
explicitly approved preserving playable Mac binary64 behavior; full x87
precision is not an integration prerequisite. No interpreter path or selector
was introduced to accommodate that limitation.

### S019 — shared Alchemy gameplay boundary and MUA adoption: partial

Observed subset: the integrated build resolves one `shared/alchemy` checkout,
links its SDL-free `alchemy::input` target into `x2native`, and keeps x86port as
a separately composed sibling. The title-owned `IgControllerManagerAdapter`
publishes exact button ordinals, pressure, sticks, POV, typed identity, and
stable lifecycle events from the same latched host sample written to retained
DirectInput. Its production comparison detects seeded disagreement, and link,
unit, structure, and execution-boundary binary gates pass. The optional shared SDL
transport remains independent and tested in the shared repository.

Gap: the current ARM64 JIT passes the former startup instruction refusals and
reaches the tutorial with retained DirectInput. Nonzero shipping-path A/B
counts, physical hotplug, and the recovered guest callback sink remain
unverified. Retain
DirectInput until those checks agree. Title action meanings, joining,
assignment, and prompt policy stay here. MUA remains deferred until every X-Men
2 project goal is verified; only then does MUA migrate to the proven shared
engine while preserving its gameplay source.

### S015 — transactional autosave and Continue: verified

Observed capability: successful map-load transactions publish an exact retail
autosave leaf without modifying the manual slot, and persisted Boot Continue
uses the retail mode-3 save-manager/deserializer chain to restore that map and
party without intro movies, the menu map, or user input. The retail Load Game
screen also exposes that autosave after all ten manual slots, virtualizing its
fixed ten resident rows without replacing a manual leaf; selecting autosave
retains the retail success acknowledgement.

Evidence: C246 records exact autosave size, unchanged manual-slot size/mtime,
and a second-run retail load; C261 records the 13/13 direct-Continue live case,
including the saved map, resolved player actor, active tutorial conversation,
zero movie opens, and no menu-map open. Issues #99, #113, and #119 preserve the
resolved save-authority, conversation, and first-cutscene boundary defects.
Issue #131 records the binary-grounded 11-to-10 Load Game projection and its
windowless full-capacity live proof.

### S016 — live control and diagnostics: verified

Observed capability: each product run publishes a loopback-only live session
that reports status, exact post-policy DirectInput state, frame timing, save and
subsystem counters, accepts ordered input, records it, and captures the final
composited frame at the render boundary. Refusals distinguish missing mappings,
missing frames, and absent guest polls.

Evidence: `tools/x2ctl.py`, `src/native/live_session.c`, and
`src/input/input_record.c` own the shipping path; C260 proves a screenshot
completes during a presenting run with no input polls, and issue #115 records
the render-boundary root cause. The controls include deliberately differing
input/frame cases rather than only uniform output.

### S020 — platform-neutral touch play on any touchscreen: partial

Observed capability: the on-screen pad and the mobile HUD placement are decided
by the device the player is touching, never by the platform the binary was
built for. `src/input/touch_source.c` classifies each host event into touch /
not-touch — ignoring SDL's `SDL_TOUCH_MOUSEID` synthetic pointer, treating a
resting stick below half of SDL's signed range as no answer, and ignoring every
other device kind rather than counting it as not-touch — and
`x2_touch_runtime_active()` publishes one answer that both the drawn controls
and the HUD relocation read. `input.touch_controls` forces OFF or ALWAYS on
every platform. The title's safe-area-aware action vocabulary, zone routing,
multi-touch capture, cancellation on focus/rotation/lifecycle loss, invisible
relative camera swipe, and retail-click portrait selection are all in the
platform-neutral owners named in `docs/touch-play.md`; nothing is compiled out
anywhere and no source under `src/input/` or `src/presentation/` tests
`__ANDROID__`.

Evidence: `touch_source`, `touch_controls`, `touch_layout` and
`touch_hud_layout` run in the ordinary host suite on every platform, with no
device and no window. The CMake sources list builds all four touch owners
unconditionally into `x2native`, outside the `if(ANDROID)` branches.
The revised two-thumb layout and shared action SVGs were also inspected in
the actual 1280x720 RmlUi/Vulkan game presentation;
`docs/screenshots/touch-controls.png` records that rendering. Jump uses the
right action group, the held Powers modifier uses the left, and Pause leaves
the retail center notification icons unobscured. This verifies presentation,
not physical touchscreen ergonomics or Android performance.

SDL touch-to-mouse synthesis is disabled before event sources are created, so
an overlay action cannot also enter the retail world-click handler; the
portrait-selection path remains an explicit native pointer publication. The
native build and touch regression suite cover this boundary; installed-APK
finger input verification remains open.

A contact reaches the guest by one of two routes, decided by whether a control
is drawn under it. Until 2026-09-22 only the first existed: every contact
before gameplay — the legal splash, the intro movies, the main menu, the load
and save screens, every cutscene — was counted and discarded, because routing
was gated on the gameplay HUD having drawn. With touch-to-mouse synthesis
deliberately off there was no second route either, so a phone player met an
intro no tap could skip and a menu no tap could press, reported from a real
session (issue #179). Those screens are the retail GUI, which takes a mouse,
so a contact with no drawn control under it is now that pointer at its own
position — the route the portrait tap already used. `x2::input::PointerOwner`
is retail's one-button rule, shared by both rather than copied.

Measured by `tools/live_case.py menu-touch`, 9 of 9, each half against a
control that comes out the other way: a tap ended the first intro movie 6.5s
in having been made at 6.0s, where the untouched movie runs 10.0s; a tap on
empty sky opened nothing while a tap on the OPTIONS row produced the game's
own first open of `menus/options.pkgb`; the census counted 6 contacts to the
retail pointer and 0 dropped. Neither obvious measure would have worked: a
skipped movie still reports its full 312 decoded frames, and the menu's idle
frame-to-frame difference (11–19) is larger than the change a working tap
makes (27).

Every earlier touch measurement had reached gameplay by keyboard first --
`tools/web_touch_play.py` presses Escape and Enter while waiting for the gate
-- so the harness was doing on the player's behalf the one thing the player
could not do. A host with no touchscreen also could not press its own screen;
the control channel now carries a contact through `/touch?x=&y=`, routed by
the runtime's own injector rather than a second copy of the event path.

The web target is now reachable by a finger at all, which it was not. A browser
owns scroll, pinch, long-press and overscroll on any element the page has not
claimed, and the pad lives on the canvas: measured on the shipping page at
390x844, one twelve-step drag up the middle of the canvas — a thumb on the
virtual stick — scrolled the document **551 px** and the application received
nothing (issue #170). `shared/web-port`'s `claimCanvasGestures` now takes those
gestures for the application, and the same probe over the same route reports
**0 px** scrolled with `touch-action` and `user-select` at `none`. The canvas is
also sized with `100dvh` rather than `100vh`, which had put the stick and the
action cluster under the mobile address bar.

The overlay now attaches its own pad. The on-screen controls publish through
the synthetic SDL pad, and nothing attached that pad except the
`X2_VIRTUAL_PAD` diagnostic and the Android bridge setting it by hand — which
was the only reason Android's touch worked and no other platform's did. A
browser run with the overlay live reported 48 contacts, 72 zone actions, **0
published and 36 refused** with "this run has no synthetic pad to press". The
touch runtime attaches the pad on its first publish, the Android special case
is gone, and `tests/test_touch_runtime.cpp` no longer attaches one for itself,
so it proves the product supplies it rather than proving the chain works given
one (issue #171).

A touch on a drawn control reaches the pad in a browser. Measured through the
product's own touch census on the tutorial map at 390x844: 48 contacts, 0
dropped, 62 zone actions, 4 button changes and 32 axis changes published, 0
refused, and the pad claimed for player one — which is what makes the guest
poll it. The census is reported on the periodic heartbeat as well as at an
ending, because a browser tab never exits and so never reaches the end-of-run
roll-call; without it the web product had no account of touch at all.

Reaching that evidence also required the browser to be able to load a map
whose HUD draws. `#test-play` hard-coded `act1/deadzone/deadzone1`, which
draws no retail party HUD (0 visible party draws against the tutorial map's 19
on the same route, on both the native and the browser products), and the
overlay is gated on that HUD, so no browser run could ever show a control.
`--test-map=<path>` makes the route selectable; the default is unchanged
(issue #171).

In touch play the retail footer prompts are controls. The key's glyphs are
collapsed where the emitter writes them, the action's words slide into the
space they left, and the rectangle they landed in is published as a control
that presses the DirectInput code the prompt named -- so `Esc Back` reads
`Back` and is tappable, and `[Space] Advanced Options` reads `Advanced
Options`. Measured by `tools/live_case.py prompt-touch`, 9 of 9: the Options
screen publishes both, a tap on Back returns to the menu it was opened from,
and the census counts the press with no refusal from the keyboard injector
(issue #180). The main menu draws no prompt and publishes none, which is what
proves a control does not outlive the screen that drew it.

The stick steers from the thumb, not from the ring. It measured its axes from
the ring's geometric centre, so where the thumb happened to land was itself an
input: measured in the running game with a contact landing 0.45 of a radius
right and 0.5 down, a thumb that had not moved at all published `0.431, 0.479`
-- the character walked off diagonally the moment the screen was touched --
and a full radius of travel upward reached only `-0.479` while still steering
right. Travel is now measured from the contact's own landing point, clamped to
the unit circle rather than per axis, with an 8% dead zone; the same three
probes now read `0.000, 0.000`, `0.000, -1.000` and `-1.000, 0.000`. The knob
is drawn at that deflection instead of dead centre. `tools/live_case.py
stick-travel` is the falsifier, 9 of 9, and it fails three checks against the
old arithmetic; the run publishes the ring and its deflection through the new
`GET /controls` so the case keeps no second copy of the layout (issue #181).
User-reported from their own phone.

A finger now reaches the game itself, and the last link is read out of the
guest rather than inferred. On the Dead Zone route with `input.touch_controls`
at ALWAYS, the `/input` probe reads the game's own DirectInput wrapper:
`joystick answer mask 0x00000001`, and while a contact holds the ATTACK
control, `device 0 block 0x7120c9b8 buttons down: 0(code 0x15)` — the guest's
own device state, one link before its binding table reads it. Held four
seconds on the stick at full deflection, the hero walks out of the clearing
and the camera follows; released, the block reads `none of 32`. The stick's
own arithmetic, driven through `/touch` on the published ring: landing
off-centre publishes `0.000, 0.000`, a radius of travel up publishes
`0.000, -1.000`, a radius right `1.000, 0.000`, and the corner of the box
`0.707, -0.707` rather than 1 in both.

The same run measured what issue #173 costs when the pad is late. In AUTO on
a host reporting no touch device, `prepare_for_host` attaches nothing, so the
pad arrives with the first contact — after the guest has enumerated — and the
probe reads `joystick answer mask 0x00000000`, "NO joystick device answered
this frame, so every pad binding reads 0 by construction", while the census
cheerfully reports 12 axis changes published and 0 refused. Both halves are
true and only the pair says anything: publication is not delivery.

Gap: a retail dialogue's footer is drawn as TWO strings — the key cap
`[Enter]`, and `continue...` separately — and `x2_prompt_action_label_match`
requires words after the cap in the same string, so neither is claimed and
`/prompts` answers "no action prompt is pressable" on a screen plainly
drawing one. The screen is not stuck: it draws no control, so a contact goes
to the retail pointer, and a tap anywhere advanced the Dead Zone opening
dialogue line by line. What is missing is the button on the words the game
named, not a way past the screen.

Gap: no run on a real desktop touchscreen (Windows tablet, Linux 2-in-1) has
been recorded, so "played by touch on a desktop" is not yet a claim this
repository can make — only "the path is platform-neutral by construction and
unit-verified". Measured phone evidence remains S018's gate. The browser safe
area is still the whole canvas: `SDL_GetWindowSafeArea` falls back to the window because SDL's
Emscripten backend never set the insets, so a control against the edge can sit
under a notch or the home indicator. SDL fork `70f8057` addresses it and is
deliberately not pinned, because no browser available here can emulate a
non-zero inset and a run of all zeroes cannot tell a correct reader from a
broken one (issue #170).

An Android emulator run of the same revision cannot yet exercise any of this,
and the reason is upstream of touch. On the API 35 x86_64 emulator the run
reaches D3D8 device creation and then the guest spins inside one compiled
block — 570,985,925 block entries, 98.4% of them re-entering the block just
left, while host-boundary crossings stay frozen at 25,154. SDL is pumped only
from the guest's `PeekMessageA`/`GetMessageA`, so 25 dispatched taps reached
nothing: the census reports "no contact reached the port this run ... Nothing
was dropped; nothing arrived" (issue #172). This is a boot defect, not a touch
one; touch activation has no platform conditional in `src/input/` and the same
code publishes to the pad in a browser.

That block is now named: **cg.dll + 0xe2d5**, a string-hash loop whose exit
condition subtracts a per-step bit count from 32. On the emulator that count is
zero, so it cannot terminate. The string it is hashing is `"texture unit 0"`,
so this is a Cg parameter lookup against a table sized for two entries. The
difference is in guest DATA — a heap descriptor — not in generated code; which
call built it is open, and both known constructors were watched without being
entered. The engine now
publishes its last block entry and the frozen-crossing beat prints it, because
the block-entry histogram could not: on that run it dropped 1,761,478,604 of
1,761,605,419 entries and ranked a block with 11,630 hits first.

Reading any of that on Android required fixing the heartbeat first. Its
subsystem roll-call — touch census, pad, control channel, guest clock — sat
after a `continue` taken whenever crossings were unchanged, so a stalled run,
the case whose accounts matter most, printed none of them: `grep -c "[touch]"`
over a full logcat returned **0**. The roll-call now runs before any branch
that can end the beat early, and the same `continue` had been suppressing the
boundary-ring dump, whose tail is what identified the spin. The frozen-crossing
line also asserted a cause it had not observed ("blocked inside host code or
stopped, not looping") while the engine's own counter showed it looping; it now
names all three possibilities and points at the block-entry line that
discriminates them.

A touch press now reaches the game itself, which it never had. The overlay
published to the pad, player one was claimed, and the game read a button from
that pad **0 times** — because the guest enumerates game controllers once,
about two seconds after the window exists, and a pad attached on the first
finger can only ever be later than that. The port cannot make it enumerate
again: that path resolves the game's own re-enumeration routine through the PE
export table, and XMen2.exe exports nothing, so controller hotswap has been
dead since the generated corpus and its symbol table were retired (issue #173, claims
C161 and C262 falsified). `x2::input::touch_pad` now attaches the overlay's pad
when the window arrives, before the enumeration, for any host that reports a
touch device or whose setting forces the controls on. Measured by
`tools/live_case.py touch-pad`, 7/7: the game is offered the pad by its own
enumeration, reads a button from it 27,700 times with 43 coming back DOWN, and
the presented frame moves by 9.2 under a Start press. A controller plugged in
mid-game is still never polled; that is issue #173's own gate.

In a browser the same press was still never read DOWN — 167,890 guest reads,
none of them pressed, with the overlay reporting the gamepad DOWN at the
moment it published. A browser delivers finger down and finger up in one pump
and the guest polls between pumps, so the press existed for about a
millisecond. Two causes, both fixed. The virtual pad now holds a press until a
reader has actually seen it, bounded at 0.30 s, and releases it on the first
poll after that; a withdrawn (cancelled) contact still releases immediately,
because nothing is owed to a press the player took back. And the diagnostic
that prints what is held was reading the pad through the counted entry points,
so the probe's own reads made the press look already seen and it was dropped
straight away: `dinput_pad_button_uncounted` and `dinput_pad_axis_uncounted`
now serve every diagnostic, and "the game read a button N time(s)" means the
game. `tests/test_touch_runtime` covers both, and the probe check was shown to
fail (`buttons bitmap 0`) when pointed at the counted read.

The pad sampler moved out of the inventory: `dinput_pad.c` (397 lines) owns
when a device exists, `dinput_pad_sample.c` owns what the guest reads out of
one, and they meet at `dinput_pad_handle`, which distinguishes an empty slot
from a device SDL gave no handle for — two different defects that used to
return the same "not pressed" silently.

A touch press now reaches the guest in a browser too. It never had: the
overlay publishes through an SDL virtual joystick, SDL announces every button
it sets as an ordinary joystick and gamepad event, and the input-source owner
read those as a controller arriving -- so it flipped away from touch and
cancelled every held zone about a millisecond after the press was made. Every
run had said so in its own beat, `source says not touch` printed beside
arriving contacts, while the counter next to it claimed cancellations for "a
lost window, rotation or layout change", three causes it had never observed.
The pad owner now tells the source owner which joystick id is the port's own,
the same way the SDL_TOUCH_MOUSEID checks already handled the synthetic mouse
events a touchscreen produces, and the four cancellation causes are counted
apart. Measured in `scratch/web/wasmgoal/verify18`: 30 of 31,840 guest reads
came back DOWN and 14 axis reads off centre, against 0 of 167,890 before; the
first press was held across 20 reads, 2 of which saw it down.

Three defects were fixed on the way and are covered by
`tests/test_touch_runtime` (44 checks) and `tests/test_touch_source` (18): the
diagnostic probe's reads counted as the game's, a deferred release that landed
was never latched into SDL, and a press waiting for its reader was released on
the read of a different button.

Gap: no run on a real desktop touchscreen or an Android device (issue #172
blocks Android). A press on "start" is still reported as taken back; opening
the pause menu hides the overlay, so that may be correct, and the new
per-cause counts will say.

### S021 — web (WASM + PWA) product with browser-side install: partial

Every browser measurement this port had made was headless Chrome, and the first
Firefox-family report was a black screen: Zen 1.22.2b (Firefox 156) with WebGPU
enabled never opened the setup page at all. Reproduced headless over Marionette
(`tools/marionette_client.py`) and measured in the page: `getDirectory()` and
`persisted()` answer at once, and `navigator.storage.persist()` never settles,
because it is a permission request Firefox holds until a player answers a prompt
they are never shown. `persistentStorage()` awaited it, so the application stood
still on its first status line with every button disabled (issue #175). Fixed in
`shared/web-port` c1bff1c, pinned here: the module reports `persisted()` and
hands the pending request back so the note can improve later. Verified in
headless Zen against the rendered release served WITHOUT isolation headers, the
Pages condition, so the service worker supplies them: `crossOriginIsolated=true`
and the page reaches "Choose your game ZIP or play the installation saved on
this device." on three consecutive loads. Firefox-family GAMEPLAY remains
unverified.

The browser artifact loads and rejects a malformed ZIP through its native
installer. Source run `35476098695` built the asset-free package at `8e0cabb`, which is what the live route now serves;
the central Pages route at `https://someoneisworking.github.io/xmen2/` serves
releases with `publication.json` source provenance. WebLua verified the central
setup page with `crossOriginIsolated=true`, a WASM runtime, a canvas, and no
failed network requests. Its opt-in GPU mode now obtains a WebGPU adapter.
WebLua `f031b16` keeps its isolated Chrome profile on disk-backed storage.
Pinned Lucent `f592a5c` buffers ordinary ZIP central directories and extracts
into an unpublished tree with expanded-byte progress, avoiding the duplicate
decompression pass. A complete 1.61 GiB, 13,382-entry PC-install ZIP copied
into browser-private storage, extracted to 100%, passed the title validator,
and published `/opfs/install.ready`. A fresh page reopened that saved install
without importing again. The game then loaded the actual PE images and executed
guest blocks through the WASM JIT. A previous font-loader failure came from
passing one host pointer to `fread` across separately allocated guest pages;
title-owned `guest_file_io` now copies through the guest-memory model. The
maintained SDL WebGPU fork now releases cancelled command buffers instead of
asserting. A later Dead Zone test reached the retail `startFirstMission` boot
hook and emitted 78 frames over roughly four minutes, but the canvas remained
black and frame times stayed unplayable. A 2,048-module trial was read as
raising renderer memory above 1 GiB and the pinned runtime kept its
1,024-module cap; **that reading was wrong, and the cap was the browser's
single largest CPU cost** (issue #153). Measured in Chrome 128, a live module
shaped like a real translated block -- 1,569 bytes, a shared memory and twelve
function imports -- costs 3.2 KB and 13.7 us, so 8,192 of them is 26 MB, not a
gigabyte. The cap was fixed inside x86port while the engine was being asked for
an 8,192-block cache, so seven eighths of that cache could never hold anything
and the Dead Zone route retranslated 7,244 blocks per second forever; a CPU
profile put 42.7% of the busy worker in `new WebAssembly.Module`/`Instance` and
9.8% more in the invalidation those evictions drive. x86port `059244c` makes
the cap the cache size the caller asked for. Re-measured on the same route with
the same page command line: **steady-state translations fell from 5,106/s to
0/s, live module bytes rose from 1.6 MB to 11.2 MB, and guest block entries
rose from 124,447/s to 4,308,000/s** -- which also retires #149's reading that
the browser JIT "executes 21-27x slower than native", taken when the browser
measured 0.74M blocks/s. Frame progress did **not** follow that fix: the
re-measured run held at 10 presents while executing 4.3M blocks/s. What held it
was every WebGPU wait in the SDL fork polling `wgpuInstanceWaitAny` with a zero
timeout — the one form of that call which never reaches JavaScript, so the
future it waited on could not settle during the call and the wait could only
end by luck (issue #154). SDL `89951aff5`, pinned through web-port `4407360`,
creates the instance with `TimedWaitAny` and gives the waits a real timeout, so
they unwind through Asyncify and resume with an answer. Re-measured on the same
route: **presents went from a wedge at 10 to 252 in under three minutes, draws
from frozen at 121 to 44,746, and `emscripten_futex_wait` left the profile
entirely**. `--vk-selftest`, which had hung for 17 hours after logging
"swapchain claimed on window", now runs the battery to a verdict. Browser
playability remains unproven, but two more costs have since been removed from
the route. The first was the port's own stall diagnostic: `x86_ring_dump` bound
its loop to the live crossing counter, which the guest keeps incrementing from
its own worker in the browser, so one call never returned and streamed about
6,000 blocking console posts a second on the thread that had to reach `Present`
— the stall it reported became permanent because it reported it (issue #155).
The second was path resolution: every guest open enumerated every directory on
its path, once per component, and in WASMFS each enumeration is a cross-thread
round trip, so one `fopen` cost 23.3 ms (issue #156). `src/native/host_dir_cache.c`
keeps one listing per directory with an explicit invalidation contract on every
creating, removing and renaming site. Re-measured on the same route with the
same page command line: **console output fell from 117,318 lines in 20 s to
9,551 in 300 s, `fopen` from 513.4 ms in 22 calls to 58.1 ms in 56 calls, and
the wall-time split inverted from host imports 52% / guest bodies 48% to host
imports 16% / guest bodies 84%**. What bounds the browser now is guest
execution alone: about 2.2 presents per second, with host draw at 3.9 ms and
host upload at 0.47 ms per frame, 283 million block entries over 300 s against
347,371 translated blocks, and `0 of 276,933 condition(s) lowered inline` on the
WASM backend. A profile of the guest worker alone — 98.6% of it working — says
what that execution is actually spending itself on, and it is not the guest's
code: **`x86p_sparse_span_access` is 38.61% of that worker and guest memory
access totals about 62%, against 5.29% in translated guest blocks** (issue
#157). In the browser `X86pMem` uses x86port's sparse mode, so every guest load
and store is a host import call that binary-searches a sorted mapping array,
and a checked access does it twice. The desktop has none of this: it reserves
one 4 GiB arena and a guest address is a host address plus a constant. The
named fix is a flat guest window in the WASM linear memory with a page-permission
table, so an access becomes two memory loads instead of two calls and two
searches; the footprint fits, at 1.88 GiB against a configured 4 GiB maximum.
**That fix is built and measured.** x86port `035e2f7` gives `X86pMem` an
optional byte-per-page permission table for the contiguous mode and emits the
check inline — a bounds compare, one `i32.load8_u` of the permission byte (two
when the access straddles a page), the base add, and a direct wasm load — with
the sparse mode and the desktop path untouched; `test_memory_perms` (23 checks)
and `test_wasm_perms` (9, every one through `x86p_jit_engine_run`, so the
emitted guard is what answers) fail when the guard or its second page load is
deleted. `src/native/guest_memory.c` is now the single owner for every host and
`guest_memory_sparse.c` is gone: on a host with no VM it holds one `calloc`ed
window over the packed layout in the new `src/native/guest_layout.h` — one
authority for the image, the relocated modules, the guest reservations, the
runtime heap and the file-view arena, which had been chosen separately and
overlapped, the module scan running through both the reservation and view
arenas — and projects the Win32 page table it already kept into the permission
bytes x86port reads. `tests/test_guest_memory_window.c` proves that owner on the
desktop with `X2_GUEST_ARENA_WINDOW=1` (43 checks, including that a released and
remapped page reads zero rather than what the previous mapping left, and that a
decommit and a recommit each reach the execution owner). Re-measured on the Dead
Zone route with the same page command line: **`x86p_sparse_span_access` is absent
from the guest-worker profile entirely, and guest memory access is about 5% of
that worker against 62%**; the route reaches 50 presents at 41 s rather than
111 s, 200 at 106 s rather than 206 s and 800 at 241 s rather than 456 s, its
fastest frame falls from 160.0 ms to 16.8 ms, and steady-state translation
falls from about 6,000 blocks per second eleven minutes in to a few hundred.
What bounds the browser now is translation itself: `WebAssembly.Module` /
`Instance` construction at about 33% of the guest worker and invalidation at
about 17% — but that profile was taken while the map was LOADING, and the
heartbeat's new invalidation counters found both the cause and the correction.
The JIT's 8,192-block code arena was smaller than the game's working set, so it
evicted a block for almost every one it translated: 654,779 blocks translated
over 452 seconds to hold 8,192, only 500 of those drops asked for by this port
and the rest the engine reclaiming its own arena (issue #161). Running with the
caps set past any plausible requirement measured the real working set at
**61,200 blocks and 98 MB**, reached in 75 seconds, with zero evictions — so
the browser's defaults are now that measurement plus a margin, 65,536 blocks
and 128 MB. It was read as worth about **10% more frames**, not the doubling
the loading profile implied — a figure since retired by the noise result
below, though the much faster load it also buys is not in doubt; at steady state the old arena was
translating about 1,000 blocks a second, not 3,000.

**It also moved the bottleneck rather than removing it.** With translation
quiescent the busy worker reports `x86port JIT translation` and
`wasm compile/instantiate` at 0.00% each, the game's own translated code at
12%, and **x87 emulation at about 47%** — WebAssembly has no 80-bit float, so
x86port carries an extended-precision softfloat, and that is now the largest
single cost in the browser (issue #162). The obvious fix for that — Win32 sets
x87 precision control to 53 bits, so stop computing 80-bit results the guest
has not asked for — is **dead, and was killed before any code was written**:
counted over 100,000,000 arithmetic operations of the `deadzone-render` case,
the guest asks for extended precision on 100.0% of them. The count is trusted
because the rounding field moved while the precision field did not (1.65% of
operations run at round-toward-zero, which is the guest's own `FLDCW` reaching
`f->control`), so a constant default being echoed back is excluded. What the
47% is actually spent on is moving values, not computing them: ext80 arithmetic
proper is 4.9% of the worker and the plumbing around it about 40%. Two causes,
both bit-identical to fix. One is now fixed: every bulk guest access walked the
permission structure twice, once to prove the whole range accessible and again
to copy it, where a span covering the whole range is itself the proof. x86port
`2ab56b4` gives the memory owner that fast path, so `x86p_x87_read_value` fell
from 13.59% of the guest worker to 10.95% and `backing_span` from 5.66% to
3.50%. **No frame figure is claimed for it**, and the reason is a result in its
own right: a third run of the same route, on that build plus an unrelated
change the profile puts at 0.3%, went 6.75, 6.73, 6.35, 6.76 and 5.49
presents/s across its five age windows — a spread inside one run three times
larger than the difference between the builds being compared. One
wall-clock-paced run of this route cannot resolve a change of this size, which
retires the earlier "10% more frames" and "6.7% more frames" readings alike;
profile shares are what this evidence supports. The other is that the
x87 register file holds binary128 while arithmetic is ext80, so every operation
widens both operands, computes, narrows and reclassifies; fixing it means
changing x86port's numeric type across about 170 uses in 23 files and all three
JIT backends, which is recorded in #162 rather than started from a profile.
Separately, `guest_clock_ns()` — which `guest_clock.h` names as the one source
of guest-visible time — **had no callers at all**, while QueryPerformanceCounter
on both the import and the JIT fast path, and GetTickCount, each read
CLOCK_MONOTONIC privately and so ignored the idle skew an unbounded run
applies; QPC also pumped the multimedia timers, which read the clock a second
time (issue #163). All three now take one reading from the owner and pass that
instant to the pump; `tests/test_guest_clock.c` fails on a skew-blind clock
with "the five-second skip moved the counter by 110 ns". Its browser effect is
measured and small: `_emscripten_get_now` 4.19% to 3.88%, so the pump's second
reading was a minority of the clock cost and the rest is the guest's own call
rate. The correctness half is why it stays.

**None of this is the frame rate.** The route presents **11.552 +/- 0.011 per
second**, measured over 1042 presents in a 90.2 s plateau at load average 5.0;
`tools/web_presents.py` reports that steady rate because a per-heartbeat one
quantises at 1.75% on this route and cannot resolve the changes now being made.
A later run on the inline-condition build read **11.840 +/- 0.040** over a 25 s
plateau — at load average 4.27, which is enough of a difference on its own that
the two are not comparable. It is recorded because it is the current reading,
not as a gain.

**The guest worker is not all of the frame.** Arming the heartbeat's time probe
on the same route (`--set hotep=64`) attributes 779 ms of host import stubs and
949 ms of dispatched native bodies per 5 s interval; translated blocks running
inside `x86p_jit_engine_run` are in neither span, and are most of the rest. Of
the import half, **`DrawIndexedPrimitive` is 46%** — 362 ms per interval, 6.8 ms
of a 94 ms frame, agreeing with the renderer's own "host draw 6.59 ms/frame".
The import the run crosses into most, `_ftol` at 73.6% of all crossings, is
6.5% of import time: #169 records why the count ranking is not the time ranking
and what is unexplained about the draw path.

The guest worker's own census, 25.6 s and 86,765 working samples of that
worker's 89,154, is what the remaining work inside that worker is ranked on:

| cluster | share | issue |
|---|---|---|
| x87 emulation | 29.8% | #162 |
| dispatch (`x86p_jit_engine_run` + the intercept) | 13.96% | #166 |
| flags and ALU helpers | 4.5% | — |
| SSE through a scalar C helper | not in the profile | #167 |

The SSE row is closed and the x87 row has been re-shaped rather than shrunk.
Both are described below, with the translated guest block itself now 28.30% of
that worker where it was 21.9% — the code the route is supposed to be running
has gone from a fifth of the worker to more than a quarter of it, because two
families of helper crossings were removed from around it.

Of that last row, the 0.91% that was `x86p_cond` is largely gone: x86port
`f94ad4a` derives a condition inline from the kind that wrote the flags, and
**96.2% of the conditions this route translates now take that path** (#168).
The remaining 3.8% are the Add, Inc, Dec and explicit-EFLAGS kinds, which the
census in the heartbeat counts separately so the next derivation can be ranked
rather than guessed.

Of the dispatch row, the cheapest fix is now **ruled out by measurement rather
than by argument**. x86port `4c1c5c8` counts, per block ENTRY, the times the
block entered was the one just left — exactly what lowering a self-exit as a
WebAssembly `loop` would remove. Over this route it is **23,248,130 of
454,767,532 entries, 5.1%**, so that change is worth about 0.7% of the worker
and is not the lever (#166). General chaining to a known successor is 67.2% of
exits and remains open; sizing it needs the successor actually taken, which is
not measured. At 13.96% this is now the largest cluster after x87.

**The SSE row is closed: the lowering is written, landed and in the product.**
The gate that had to be answered before writing anything passed first. A
temporary census in `x86p_wasm_simd_arithmetic` counted the guest's MXCSR per
operation rather than per `LDMXCSR`, and printed all sixteen buckets including
the empty ones. Over **317,883,827 SSE arithmetic operations** on this route the
control word is round-to-nearest with flush-to-zero and denormals-are-zero clear
on **100.00%** of them, which is the one mode WebAssembly's `f32x4` arithmetic
produces — the opposite of the answer the same question gave for x87 in #162,
where rounding control moved on 1.65% of operations and killed that plan. Four
opcodes account for every one of those operations, summing to the denominator
exactly: `MULPS` 37.91%, `ADDPS` 37.40%, `SHUFPS` 20.91%, `XORPS` 3.78%
(`ORPS` ran three times; nothing else ran at all). Each maps to one host
instruction, `SHUFPS` to `i8x16.shuffle` with the lane indices baked from its
decode-time immediate. `emit_wasm` now has the `v128` type and the `0xFD`
prefix, and the product reports `100 of 164 SIMD instruction(s) emitted as host
SIMD` with the remaining 64 naming the forms that still cross out of the module.
The scalar helper no longer appears anywhere in the guest worker's profile.
#167 holds the tables and the evidence.

x87 has come from 47% to 29.8% across seven landed x86port changes. Three of
them removed the *moving* of float values across the module boundary: the exact
ext80 widening (`70e6536`), the pop fusion (`30ad283`), the inline FLD m32/m64
widening (`0ddf304`) and now the inline FST m32/m64 narrowing (`cc924a1`). The
widening dropped `x86p_wasm_x87_load_bits` and `x86p_x87_reg_from_operand_bits`
from 5.35% of the guest worker to 2.52% together, with 3,161 of the 3,396 memory
x87 load sites (93.1%) taking the emitted form — the rest are FILD, a different
conversion. The narrowing is the mirror in placement but not in arithmetic,
because a store rounds: one shared acceptance rule
(`x87_ext80_narrow.h`) states exactly which ext80 values the ordinary
round-to-nearest-even path may narrow itself, and the C fast path and the
emitted WebAssembly are held to it by a differential test in a real engine.
Its two frames, `x86p_x87_operand_bytes_from_reg` at 4.28% and
`x86p_wasm_x87_store_at` at 3.60%, are **not in the profile at all** after it,
and the product reports **3,906 of 3,932 x87 store(s) narrowed in the block**
(99.3% of the sites this route translates).

What is left in the x87 row is the arithmetic itself, not the moving of values:
`x86p_x87_arith_raw` is 16.71% of the guest worker and the Bochs-derived
softfloat under it another 5.2%, which together are two thirds of the cluster.
That is the part #162 has not touched, and it is now the single largest frame
in the run.

No frame-rate gain is claimed for either change. The store build measured
13.042 ± 0.025 presents/s at host load average 35.67 against 12.707 ± 0.010 at
load average 5.0; the host moves this route by far more than either effect, so
the profile shares above are the evidence and presents/s is not.
Frames track that worker one for one — proved when the host took half of it
away mid-capture and block entries and presents/s both fell 47% in the same
windows — so the census is a roadmap and not just accounting. It is also not
enough on its own: zeroing every row above leaves the route short of playable.

**The retail route now reaches gameplay in a browser**, which it had never
done. Driven in Zen (Firefox 156) over Marionette with a real key press through
`WebDriver:PerformActions`, the packaged product boots from its saved
installation, renders the main menu, takes the selection and loads a level that
then renders continuously with its HUD -- 2,063 presents over 265 s, no abort.
What had ended that run with "native code called abort()" was x86port's room
check counting the wrong resource: a translation is filed in a BLOCK RECORD and
records are published through engine MODULES, compaction gathers up to
thirty-two records into one module, and `x86p_jit_storage_room()` compared the
MODULE count with the RECORD capacity. With 65,536 records the module count tops
out near 2,000, so the check answered "room" while every record was taken,
`evict_for_room()` evicted nothing, and the refusal that followed became
`kX86pRunOutOfCode` and an abort (issue #177). x86port `e2c4ca9` counts the
records, raising the count where a record is actually made live rather than
where a free slot is found; `tests/test_jit_storage_wasm.c` fills every record
against the shipping storage and reports `OutOfSlots` where the old comparison
reported `Room`. The heartbeat now shows that path working: evictions
attributed `252 the module slots, 133 the live-module ceiling`, where the record
limit had never asked before. **The frame rate on that route is still far short
of playable, but the largest single line in it has been cut.** The frame was
124.5 ms with 49.3 ms of it inside `SDL_WaitAndAcquireGPUSwapchainTexture`, on a
renderer recording 2.65 ms of host draw -- and the frame timing now counts the
acquisitions that returned in under a millisecond, which no browser turn fits
in, so a fixed per-frame cost can be told from a few stalls: **12 of 1,603
returned promptly**, so 99.3% of frames blocked. SDL's WebGPU backend allows two
frames in flight by default, and in a browser the report that a submitted frame
is done arrives about a frame after the work does, so at a depth of two it is
always still outstanding when the next frame starts. `kGpuFramesInFlight = 3`,
set where the swapchain is claimed, gives that report a frame of the guest's own
CPU work to arrive in. Measured on the same route: **122.9 ms a frame became
94.3 and 96.1 across two runs, the swapchain wait 52.1 ms became 36-40 ms, the
acquisitions that did not block went from 0.6% to 22%, and the run carried about
30% more frames.** A depth of five measured identically to three, so the depth
is no longer what bounds it; 78% of acquisitions still block for about 36 ms and
issue #178 records what was tested and retired on the way, including a
reordering of the SDL fork's wait loop that measured inside this route's own
spread and was not landed.

The frame rate is still short of playable. The route a player actually takes
used to be worse than the gameplay test, and **that is fixed**: the packaged product started from its saved
installation now boots through the legal screen, the six intro FMVs and the
main-menu load and renders the menu continuously. It had wedged instead, first
at the retail "Loading..." prompt and later one phase further on, with 99% of
every interval inside `KERNEL32!Sleep` (issue #158). The cause was this port's
scheduler, not the title: `scheduler_has_waiter()` counted any thread parked in
a condition wait as a thread the guest lock could be handed to, and the
hand-off promise in `threads_yield.c` then waits until somebody else has taken
a turn. XMen2.exe's gamepad-enumeration loop sleeps 83.3 ms an iteration
through `igPthreadThread::internalSleep`, and a thread sleeping out a deadline
cannot take a turn, so every quantum yield waited out that deadline and the
whole product advanced at the sleeper's 12 Hz. `src/native/threads_ready.c` now
owns the rule: a condition waiter is a candidate when a broadcast has reached
it or its deadline has passed, never merely because it is parked, and the
broadcast path issue #149 depends on is preserved by marking waiters ready when
they are signalled (`tests/test_threads_ready.c`, 15 checks). Measured on the
same Zen retail route: **0 presents in six minutes became 2,969 at a sustained
50 per 5 s, draws per 5 s went from about 40 to about 12,000, and the longest
lock hand-off fell from 398 ms to 2 ms.** Two instruments were needed to get
there and both stay: `KERNEL32!Sleep` was not counted anywhere, so the
heartbeat's wait line read "+0" through a stall that was almost entirely
Sleep -- it now lives in `kernel32_wait.c` with the other blocking waits and
feeds the same counters -- and a bounded census beside them names the guest
call sites that called it. The gameplay test route is unaffected by the fix
(it runs one guest thread, so it never parked) and still measures about 9.8
presents/s at 117 ms a frame, of which 22 ms is the swapchain wait. That is not the guest-memory
window: `tools/live_case.py cutscene-skip` boots the retail flow, loads a map
and runs a cutscene 11/11 on both the ordinary desktop binary and on
`build/native-window/x2native` built with `-DX2_GUEST_ARENA_WINDOW=1`.
`tools/web_console.py` is the instrument that made this measurable — it records
every console line over CDP, where WebLua's own buffer held 50 and none of them
the heartbeat. The heartbeat now also reports invalidation in both halves — the
calls x86port received, the guest bytes they named, the cached blocks they
actually dropped, and which of guest memory's three operations asked — so the
residual translation churn can be attributed rather than guessed at. Separately,
that window is 2.5 GB of **committed** memory before the first frame, and a
loaded host refuses it by name and the product will not start (issue #159); the
region sizes in `guest_layout.h` are collision-avoiding ceilings, not measured
requirements. A browser with no GPU does worse than refuse: the display-failure
path reaches for `document` from a guest worker, kills it, and wedges the run
(issue #160).
An invalid-ZIP run also emitted Emscripten's main-thread blocking warning, so
a clean browser console remains unproven.
The title CMake path compiles and links its native owners for Emscripten 4.0.16.
`tools/build_web.py` consumes the shared `web-port` dependency prefix and stages
an explicit asset-only release under `build/release/web`.
Two mechanisms recorded here have since been root-caused and fixed: the
wait-bound stall that spent 178-361 ms per `WaitForSingleObject`/`SuspendThread`
was the guest-lock hand-off starving woken waiters under Emscripten (issue
#149, fixed in `src/native/threads_yield.c`), and the args-run plateau at the
retail intro was the web entry point's `argc == 2` route test dropping the
gameplay-test request (issue #151, fixed in `src/web/web_request.cpp`). A
packaged browser run of either route now advances scenes/draws/presents
monotonically with no abort. **The black canvas is fixed (issue #152, closed
2026-09-19): the browser renders the game.** Its cause was in the pinned SDL
fork's WebGPU backend, not in this port. WebGPU makes a strip topology's index
format a property of the pipeline and validates it against the bound index
buffer on every indexed draw; the backend created every pipeline with
`IndexFormat::Undefined`, so the game's first indexed triangle strip failed
validation and **invalidated the entire command buffer**, discarding every
other draw in that frame. That is the exact shape of every reading in #152:
draws submitted, `refused 0`, real GPU time spent, nothing on screen. A strip
pipeline is now created once per index size and the draw picks the matching
variant (`SomeoneIsWorking/SDL` `78419c3f0`, pinned through `shared/web-port`
`faee8c5`). Measured on the Dead Zone route in the browser at that pin, with
`present_luma=50`:

```
before: composed mean 0.0  max 0   nonblack 0.0%  | scene mean 0.1  max 191 nonblack 0.1%
after:  composed mean 33.4 max 255 nonblack 80.2% | scene mean 33.4 max 255 nonblack 80.2%
```

with zero uncaptured WebGPU errors in a five-minute run, and `composed ==
scene` as it always was natively. Interactive visible play is now limited by
frame rate (about 14 presents/s on that run), not by the picture.

What made the defect findable was access, not a new instrument: the port now
takes `--env NAME=VALUE`, routing a diagnostic override through the same
configuration owner and whitelist as the environment, so the browser -- which
has no environment -- can arm one. `X2_FRAME_DUMP=busy:100` then answered on
the first dumped frame. An unknown or malformed name is refused, because a
diagnostic that quietly fails to arm looks exactly like one that found nothing.

Two earlier hypotheses were refuted on the way, and the checks that refuted
them stay in the battery: **every renderer self-test now passes identically on
both hosts, `gpu selftests: 15 of 15 passed, 0 skipped, 0 failed`.** Getting
the first such reading required two changes. The battery returned at its first failure, so the browser's
`gpu multistage selftest: FAILED` hid the four checks below it -- including the
two draw-path checks #152 was built to ask -- and their silence read as a pass;
it now runs every check and ends with that denominator. With them running, the
browser answered the open question: `gpu lit/mvp draw selftest: PASSED`, which
**refutes** #152's `VertexState` uniform-buffer-packing hypothesis. The one
real browser-only defect the battery could see was the mip control sample, and
its cause was in the pinned SDL fork: `WEBGPU_CreateSampler` read a sampler's
`max_lod == 0` as "no clamp" and substituted 32.0, so every sampler this port
created with mipmapping off sampled the whole chain. The Vulkan and D3D12
backends pass `max_lod` straight through; only this one reinterpreted it.
Fixed in `SomeoneIsWorking/SDL` `bc00fae6a`, pinned through `shared/web-port`
`7b66fea`.

That fix did not move the black canvas on its own, and the check that followed
it removed the renderer from suspicion entirely. Every pixel check the battery
had drove `gpu_offscreen_begin`, into a texture it made itself, so none of them
touched the scene texture the game's own probe reads. `gpu_frame_draw_selftest`
drives `gpu_frame_begin`/clear/draw/`gpu_frame_end` the way the engine does and
reads that texture; it discriminates (suppress the draw and the centre comes
back the clear colour) and it passes on both hosts. Finding out that it needs a
window was worth keeping: **headless `gpu_frame_begin` draws straight into the
headless target and never touches the scene texture**, so a headless run is a
structurally different frame path from the shipping one. With the renderer
cleared, the draw dump above named the real cause.

- **W1, runtime execution: shared boundary verified, title integration partial.**
  Pinned x86port `035e2f7f72938699299685a269394f0aed791e83` and jit-common
  `329d066cf0de17d47bae74a47880e4170c2ef39b` instantiate emitted modules, publish
  indirect-table entries, dispatch guest blocks and reclaim cache entries.
  The pinned suite passed 43 native tests (including the lowering oracle, 1097
  checks with 38 of 38 lowered blocks executed in a real WebAssembly engine and
  matching the interpreter field for field) and 44 Emscripten/Node tests with
  `test_jit_wasm` reported skipped for want of an oracle in those trees. Its
  WASM module owner now reclaims one selected cached translation instead of
  flushing every 1,024 modules; a two-worker test verifies worker-local JIT
  instances. The WebAssembly host now binds its 45 imports once per worker.

  Translation on the wasm host has a per-block floor, measured at ~26 us per
  block against ~2.2 us per instruction, so real code at ~5 instructions per
  block spent about 70% of every translation on the floor (native has none:
  1.8 us per instruction at both 4 and 64). Pinned x86port `75b2cec` makes a
  block continue past a conditional -- the taken path already carries its own
  exit, so the fall-through proceeds in the same body -- which raised the real
  title from 5.4 to 9.7 instructions per translated block. On the identical
  300 s driven browser route at equal wall duration, frames presented went
  245 to 405 and 402 (two runs of the new engine against one of the old),
  average frame wall 1063.1 to 666.6 and 671.5 ms -- within 1% of each other,
  so about -37% -- and executed guest instructions 11.87M to 20.97M. No
  refusals and no aborts on any arm. The
  real title has executed tens of millions of guest instructions with zero
  refusals or fallback, but no interactive gameplay is established and
  667 ms/frame is not playable.

  That 667 ms/frame has since come down by roughly an order of magnitude on the
  same driven browser route. Measured as medians over about fifty steady
  five-second windows -- not means over the last N windows, which span different
  parts of a route that is not uniform -- the route now presents **14.40
  frames/s (72 per five seconds, IQR 71-73), about 69 ms/frame**. The ladder is
  recorded in the issues rather than repeated here: guest memory operands got a
  flat window and a page table (#158), the frustum cull and the SSE, condition
  and x87 paths stopped crossing the import boundary per operation (#165, #167,
  #168, #162), the most-crossed import was ranked by time rather than call count
  (#169), and x87 arithmetic was answered by encoding-level rules proven against
  hardware, then fused into the register file, for +2.9% and +4.3% against the
  same baseline (#162, x86port `c38c5ad`). A multiply-only control build
  reproduced the baseline exactly, so those two gains belong to the change and
  not to the machine. **This is measured frame progress on a driven route, not
  qualified interactive gameplay**, and 69 ms/frame is still not playable; the
  next measured lever is block chaining, whose ceiling #166 puts at about 9%
  with 65.4% of dispatches going somewhere the block already knows.
- **W2, rendering: verified in the browser; the picture is correct and the
  frame rate is not (issue #152 closed 2026-09-19).** The defect below was an
  indexed-strip pipeline validation failure in the SDL fork's WebGPU backend
  that invalidated whole command buffers; the paragraph above records the fix
  and its measurement. The investigation it describes is kept because its
  refutations remain true. The maintained SDL WebGPU fork
  creates a device on a worker, renders, reads pixels back and presents a blue
  SDL canvas in an isolated browser; `--vk-selftest` genuinely PASSES in-browser
  (its apparent hang was a console-log-batching artifact, fixed in
  `src/web/browser_log.cpp`). The composite/capture/blit MECHANISM is now proven
  correct three independent ways: the passing offscreen selftest, a new sustained
  per-frame real-swapchain-composite probe (`testgpu_webgpu_sustained.c`, 100%
  nonblack), and a new cached-retained-texture-reuse probe across 40 cycles
  (`testgpu_webgpu_retained_reuse.c`, 100% nonblack every cycle) -- both new,
  diagnostic-only, in the scratch fork, not upstreamed. Boot presentation
  blackout was checked and ruled out (never arms for `--test-deadzone`/
  `X2_BOOT_MAP`). A real-title composite call was traced live (temporary
  instrumentation, reverted): clean parameters, valid textures, no SDL or WebGPU
  error. **The defect is therefore upstream of compositing, in the actual
  fixed-function draw path that fills `g_scene`.** A same-route native
  comparison (`scratch/web/native-window-luma2.log`, confirmed same
  `X2_BOOT_MAP=act1/deadzone/deadzone1` boot) refutes the earlier "still
  loading" theory: native reaches real, stable visible content (`scene mean
  38.3, nonblack 54.9%`) after only 2 draws, while the browser's own `scene`
  readback stays flat (`mean 0.1, nonblack 0.1%`, one stray bright pixel) across
  3212 real draws over 295 wall-seconds (`refused 0`) on the identical route --
  a >1500x gap no loading-speed explanation covers. **Confirmed distinct from
  the framerate problem, not subsumed by it (2026-09-18):** the same
  long-running session, checked again after 1262s / 130,178 real draws / 1601
  presents, shows draws-per-scene climb from 2-9 to 278, texture stage 1 go
  from unused to 1050 draws, combiner args go from 100% default to 2430
  non-default, and 4800 real `SetVertexShaderConstant` calls -- every marker of
  real, varied, loaded gameplay geometry -- while `scene read mean 0.1 max 191
  nonblack 0.1%` stayed byte-identical to the very first sample. Slow-but-
  correct drawing would show the reading changing as more loads; it has not
  moved once across two orders of magnitude of draw-count growth. Leading
  candidate: the fixed-function vertex shader's `VertexState` uniform block
  (`src/gpu/shaders/d3d8_fixed.vert`) hand-pads many bare `uint` scalars between
  `mat4`/`vec4` members for Vulkan/GLSL std140 layout; if the fork's WGSL
  cross-compilation does not reproduce that exact packing, the `mvp` matrix (and
  every field after the first mismatch) reads garbage, degenerating ordinary
  scene geometry off-screen. Existing self-tests only ever exercised
  D3DFVF_XYZRHW (the pretransformed branch, `vs.pretransformed`), never
  D3DFVF_XYZ + lighting (`vs.mvp`, `vs.world`, the material/light fields) --
  a real, narrower coverage gap. A new `gpu_lit_mvp_selftest()`
  (`src/gpu/gpu_selftest.c`, wired into the `--vk-selftest` battery) draws
  through exactly that branch with an identity MVP/world and a known material
  emissive colour: it PASSES natively, confirming it is a valid discriminator,
  but its in-browser result is not yet obtained -- the WebLua diagnostic route
  needs a primed install profile and an explicit `#play` click before
  `?arg=--vk-selftest` reaches the module (`web/app.mjs` only starts the
  runtime from a button handler, never from the URL alone), which the
  gameplay-observation instance above did not have configured. The game-to-GPU
  draw path, the wait convoy (#149) and arg routing (#151) remain excluded as
  unrelated.
- **W3, threading and memory: partial.** The product proxies its entry point to
  an Emscripten pthread and transfers its canvas to that worker. Guest sparse
  mappings preserve the 32-bit guest address space without a contiguous 4 GiB
  heap reservation. The current main-thread isolation service worker was observed
  working on a local host without isolation headers, including a reload after the
  HTTP server was stopped. The browser package links and stages an asset-free
  release; its malformed ZIP path reaches the native bounded reader and refuses
  the input. Title guest threads now use worker-local JIT instances and apply
  foreign-worker mapping invalidations before resuming translation. The live
  browser run reached three created guest threads without repeating the former
  worker-local module-map exception. SDK shutdown still joins the OPFS backend worker on the browser
  main thread; unmount removes the file-handle teardown but not that backend
  lifecycle gap. A clean 150 s run of this build on a real 2.37 GiB install
  boots the retail Dead Zone boot hook, loads the world and presents 59 frames
  without aborting: 113.3M guest blocks entered (1,144,722 translated, 0
  refusals, 0 cache flushes), 516 draws in 56 scenes, `frame wall avg 2125.7 ms
  min 237.5` of 55 intervals, `host draw 0.39 ms/frame`. The wait convoy that
  run's hot-entry-point probe exposed -- `WaitForSingleObject` 178 ms/call and
  `SuspendThread` 361 ms/call while guest bodies cost 4.8 ms -- was the
  guest-lock hand-off re-winning its own `sched_yield()` on an Emscripten
  worker (issue #149); with the `threads_yield.c` promise in the packaged
  build the same route fires winmm at 60/s, reports `worst oversleep 118 ms`
  and hand-offs waiting at most 15 ms, and advances to 396 scenes / 395
  presents with no abort. The heartbeat now prints how each park ended
  (`parks N signalled, M timed out`) so a future overage cannot be
  misattributed. Earlier browser runs that ended by themselves with
  `Aborted(native code called abort())` after 75 s, having entered 37.4M blocks
  and presented a single frame, were a DOM callback dispatched to a thread whose
  mailbox has closed (issue #148). A stop now reports why: the port installs
  x86port's diagnostic sink, whose default writes to a worker's standard error
  that never reaches the page, and the page's console is written in blocks
  rather than one proxied round trip per line. Interactive gameplay remains
  unqualified.
- **W4, local install and persistence: partial.** The shared web-port worker OPFS mount and
  bounded streaming staging passed actual browser read/write, duplicate-input,
  concurrent-import and failure-cleanup checks. The page requests persistent
  storage and reports when the browser refuses it. The title entry delegates ZIP
  parsing and complete-install validation to the same native owners as desktop/Android;
  the browser writes an OPFS ready marker because directory rename is unsupported.
  A full real-game import, validation, ready marker, and reload without
  reimporting were observed. Save persistence and offline gameplay remain open.
- **W5, floating point: shared software boundary verified.** The pinned runtime
  uses software x87 on WASM instead of unsupported host rounding controls; its
  software suite passed 3,140 checks, while the x87 lowering suite adds 4,518
  translated checks. SIMD, integer-tail and sparse suites are likewise shared
  runtime evidence rather than title gameplay evidence.

**The retail boot's wedge is the title refusing, not the port hanging.** The
`#play` route -- the one a player takes -- reaches "Loading..." and stops, with
one guest block, a two-byte `JMP $` at `0x00403210`, taking 83.6% of every
block entered. That block is the tail of the title's own fatal handler, which
prints "Allocation failure: Reason = ..." through
`libIGCore.dll!igOutput::toStandardOut` and then hangs on purpose; it is called
from `libIGCore.dll!igMemoryPool::allocationFailure` (issue #158). So the
browser's guest allocator runs out and the game stops itself. The reason code
and the failed size are still unknown, because this port never prints the
title's own standard output -- a diagnostic gap of its own. This supersedes the
earlier localizations of that wedge to thread suspension and to the guest
memory window, both of which were wrong.

**Firefox-family gameplay now renders, and three separate defects had to go
first.** On Zen 1.22.2b (Firefox 156) the run aborted about four seconds in
(issue #176). (1) The runtime published one WebAssembly module per translated
block, and Firefox refuses new modules long before the arena's 65,536 slots are
used; blocks are now re-lowered 32 at a time into one shared module, with each
block's indirect-table entry ADOPTED so its address never changes and the
singles released. (2) Emscripten's Dawn binding handed the whole wasm heap to
`setBindGroup`, which Firefox rejects once that heap passes its 2 GB
ArrayBufferView limit, killing the render thread; fixed in the maintained fork
`SomeoneIsWorking/emdawnwebgpu` (`500f12c`, a bounded `HEAPU32.subarray` at
three call sites) and wired through SDL and `shared/web-port` rather than
carried as a patch. (3) Eviction dropped ONE block, which frees nothing when 32
share a module, so the engine flushed the whole cache instead -- 94 flushes, a
14.4 MB working set down to 50 KB; x86port now evicts a module at a time.
With those three the game renders and presents. A fourth then bounded the frame
rate: a refusal taught a permanent live-module ceiling, and the refusal Firefox
actually gives is memory pressure, not a count -- measured, it refused a
121,950-byte module with **863** modules live, and a page that releases is
accepted again at once. x86port `9201965` retires a ceiling once the arena has
backed off below it, so the engine gets to answer again. Re-measured on the same
route: **evictions fell from 1,641 to 135 and dropped blocks from 51,664 to
3,335**, with the run reporting `2 ceiling(s) in force, 2 of which did not
survive the back-off`. Frame rate is unchanged by it at about 8.4 presents/s
(frame wall avg 119.6 ms, host draw 5.39 ms, host upload 1.30 ms, swapchain
wait 25.10 ms), so what bounds Firefox gameplay now is guest execution and the
submit, not the module arena.

Gap: build/link progress and shared synthetic tests are not browser gameplay or
performance evidence. The acceptance contracts and current build entry point are
in [web-release.md](web-release.md). A deployed artifact, explicit fallback denominators, representative interaction,
and offline save/relaunch evidence remain required before this capability is
verified.

### S022 — native Windows host package and CI release: missing

Missing capability: a runnable native Windows host binary, release package, and
MSVC/Clang-cl CI build.

The current Windows job is a policy check that records the unsupported native
host; the release workflow intentionally emits no Windows artifact. The title
now has portable VM, PE file-map, case-insensitive string, synchronization,
and CRT file-operation boundaries, each with focused Linux regression coverage
and a Windows-target syntax check where the SDK headers are available. The native runtime still
has POSIX-only directory, socket, signal, diagnostic, and dependency-link
owners, so a Windows ZIP would not be a runnable release. Issue
[#146](issues/0146-native-windows-host-boundary.md) records the remaining
host work and its falsifier. Until that work lands, the Windows comparison
baseline remains the retail executable rather than a port package.
