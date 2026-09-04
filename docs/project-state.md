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

**S002 — native-overrides + x86port-JIT gameplay execution.** The offline
generated corpus is gone; the active work is to close the product-only JIT
boundary, canonical x86port publication/pin reconciliation, representative
interactive gameplay conformance, and declared-host backend gaps.

## Capability inventory

| ID | Capability or outcome | State | Factual dependency | Goals |
|---|---|---|---|---|
| S001 | Fresh-clone provisioning and the default native launcher | partial | — | G001, G005 |
| S002 | Native-overrides + x86port-JIT gameplay execution | partial | S001 | G001, G002 |
| S003 | Faithful rendering of the reached game path | partial | S002 | G002 |
| S004 | Native Alchemy 2D/UI rendering above the D3D8 seam | partial | S003, S012 | G002, G004, G006 |
| S005 | Native audio mixing and SFD movie playback | verified | S002 | G002 |
| S006 | SDL3 keyboard/controller/mouse input, assignment, and hotswap | partial | S002 | G002, G004 |
| S007 | Xbox-derived controller defaults and source-sensitive prompts | partial | S006 | G002, G004 |
| S008 | Port-owned RmlUi settings and input-binding surface | partial | S003, S006 | G002, G004 |
| S009 | Direct development boot into an initialized game | partial | S002 | G002, G004 |
| S010 | Measured frame and level-load performance | partial | S002, S016 | G003 |
| S011 | Wine oracle and evidence-backed differential RE workflow | partial | — | G002, G006 |
| S012 | A/B-toggleable native overrides and incremental engine replacement | partial | S002, S011 | G001, G006 |
| S013 | Xbox evidence consolidation and obsolete static-path removal | partial | S011 | G006 |
| S014 | Apple Silicon macOS native host support | partial | S001 | G005 |
| S015 | Transactional autosave and direct retail Continue restore | verified | S002 | G002 |
| S016 | Live control, capture, input, and runtime diagnostic channel | verified | S002, S003 | G002, G006 |
| S017 | Linux AppImage packaging and no-terminal install setup | partial | S001, S008 | G005 |
| S018 | Android APK shell, touch controls, and measured mobile performance | partial | S002, S006, S010 | G005 |
| S019 | Proven shared Alchemy gameplay boundary and deferred MUA adoption | partial | S004, S006, S012 | G006 |

## State details and evidence

### S001 — fresh-clone provisioning and launcher: partial

Observed subset: earlier Linux x86-64 and Apple Silicon macOS cold-path runs
proved that zero-argument `./run.sh` enters the locked `uv` environment,
validates user-supplied game files, restores redistributable dependencies and
native assets without Ghidra, and launches the native D3D8-backed product. The
current bootstrap no longer emits guest code; it pins `jit-common` and
`x86port`, and runtime module discovery reads the user's PE images directly.

Evidence: C182 records a cold-path run after the virtual environment, generated
sources, native assets, shared dependencies, and build tree were moved aside;
the launcher recreated the then-current inputs, presented frames, and passed
its launcher and CTest controls. Issue #110 records the original cold-path
dependency defects; issue #125 records and tests the boundary that
maintainer-only `re-harness` is not a player bootstrap dependency.

Gap: C182 predates removal of the generated guest corpus and therefore does not
verify the current cold path. The canonical `shared/x86port` checkout contains
the consumer-proven revision, but that revision is not yet published to its
configured remote. A fresh clone cannot be called reproducible until it is
published, this project's immutable pin is reconciled, and the no-generated JIT
launcher path is rerun from cold.

### S017 — Linux AppImage packaging and no-terminal install setup: partial

The repository now has a game-file-free AppImage staging path, portable UI
resource lookup, and an SDL3 first-run prompt that validates and remembers the
user's `XMen2.exe` directory in the OS configuration directory. It also accepts
a ZIP containing exactly one `XMen2.exe` at any nested path and extracts it
under that same user-data root through Lucent's shared safe ZIP implementation.
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
[`v0.1.3` AppImage release](https://github.com/SomeoneIsWorking/xmen2-recomp/releases/tag/v0.1.3).

Gap: the interactive Browse flow has not been exercised on a clean Linux
desktop in this state record. The Android shell now has a native target and
setup/touch implementation, but the APK still lacks installed-device and
performance evidence.

### S018 — Android APK shell, touch controls, and measured mobile performance: partial

The title-specific safe-area-aware touch action owner exists in
`src/input/touch_controls.cpp`, with SDL contact acquisition in
`src/input/touch_runtime.cpp`, Android setup/SAF staging in `android/`, and a
real ARM64 shared native target/Gradle assembly path. Android selection uses the
same generated validator before Lucent promotes staged files: it requires every
loader PE image and title-owned content sentinels spanning each boot-time asset
family, so a loader-only selection cannot become the retained install.
The touch feedback layer
shows authored action labels rather than misleading internal controller names,
publishes signed axes once per contact update, holds buttons until finger
release, cancels on focus/rotation/lifecycle loss, and has a persistent hide
setting. Camera movement is an invisible relative swipe, portrait taps use the
retail click handler, and scoped CHud overrides relocate the party cross and
health/energy panels only while Android touch mode is active. An NDK 28
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
as a release. The user reported that the Android setup and game path runs on a
device. An API 35 x86-64 emulator installed the debug APK, and its install-folder
and ZIP controls each opened Android's DocumentsUI picker. The former
loader-image-only DocumentsUI fixture exposed an install-validation defect: it
could reach SDL without gameplay content. The revised validator now refuses
that retained incomplete source and keeps `XMen2SetupActivity` resumed after
the rebuilt APK is installed. Canonical private-path containment prevents
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
a real APK. An earlier Android run reached the recompiled executable and
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
maps the PC images, and reaches the retail difficulty menu. Its ordinary folder
and ZIP selections still receive persisted read grants (the tree grant has the
required prefix scope), but this image's external-storage `MediaProvider` copy
path fails its AppOps package check after accepting the grant. The earlier
API-35 emulator import remains the complete production-setup proof.

The Android RmlUi overlay originally disabled its font engine despite the shared
prefix supplying FreeType. It now consumes that prefix, renders the circular
analog/action overlay, and a held Light touch visibly advances the real game to
the difficulty menu. This proves the Android contact-to-title-input route and
menu-state overlay feedback, not full gameplay HUD relocation. Waydroid's
roughly 700 ms frames are an emulator diagnostic only, not Android performance
evidence.

Gap: the current x86port JIT has no ARM64 backend, so the Android gameplay
product cannot yet satisfy S002 and must not ship by selecting the test
interpreter. A publishable APK also requires a stable physical Android test device,
full-gameplay HUD verification, the maintainer's long-lived keystore, and
measured named-device performance. The required setup, touch-zone mapping, and
device/thermal/frame-time evidence gate are specified in
[`android-release.md`](android-release.md); desktop and Apple Silicon results do
not count as Android performance evidence.

### S002 — native-overrides + x86port-JIT gameplay execution: partial, current focus

Observed subset: commit `27f0a7b` removed the roughly 307 MB generated guest C
corpus, its generator, generated dispatch tables, and the analysis inputs whose
only consumer was that offline pipeline. `x2native` now maps and initializes the
original PE images at runtime, executes every non-native path through
`shared/x86port`'s x86-64 JIT, and supplies the reached
Win32/CRT/DirectInput/DirectSound/D3D8 boundaries. Native overrides hand back to
the shipping dispatcher by module plus address and can call their original
guest bodies through the JIT. Observed JIT runs have traversed menus, movies,
level load, gameplay, death, and return-to-menu paths. C288 and C290 record
hundreds of millions of in-game JIT block entries agreeing with the test
interpreter while native overrides were active.

The JIT was black-screen-broken from its first landing (`775712c`) until issue
#140: translated blocks ran through host interception points, a JITted thread
never yielded the guest lock, the engine's call-frame stack was shared across
threads, and `jit_intercept` gated the native-override hand-back on an engine
frame that goes NULL once boot nesting passes `ENGINE_FRAMES_MAX`. With all
four fixed, the JIT reaches the title screen and plays the intro reel through
native FFmpeg; `tests/test_jit_intercept.c` locks the override hand-back against
the frame depth. The native BehavEd context/scheduler and title timed-event
players complete the verified tutorial control-lock cutscene synchronously,
silently, and without advancing guest frame/time (C274). Callback cleanup and
module-aware override routing are checked by C178 and C209.

Gap: the interpreter still has product-facing selection/configuration
vocabulary and has not yet been isolated in a separately linked test target;
the gameplay link/selector audit is therefore open. The consumer-proven
x86port work in the canonical checkout still needs publication to its remote
and reconciliation with this project's immutable pin. No bounded
representative interactive gameplay case has yet combined native overrides,
nonzero JIT execution, independent
CPU/memory/timing/device comparison, and the declared frame-time budget.
Finally, only the x86-64 backend is implemented; Apple Silicon and Android
ARM64 require a real JIT backend and may not fall back to interpretation.
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

Gap: this verifies only the prompt SVG slice. Stock ASCII, panels, sprites,
batching, and other display-list geometry still submit through the
runtime-translated guest engine and D3D8 host. Each semantic owner must be reverse-engineered, ported,
and measured to zero old D3D8 calls before that portion of the seam can be
deleted.

### S005 — native audio and SFD movies: verified

Observed capability: the native DirectSound boundary creates, fills,
duplicates, plays, and mixes secondary buffers, while the native FFmpeg-based
SFD bridge preserves the retail `libMovie` scene, image upload, timing, and
callback path. Timed silent runs preserve advancing play cursors without
opening a host playback device.

Evidence: C176 records 1,249,706 non-zero mixed samples from the game path;
C243 records decoded/displayed video and audio frames through the intro; C248
records 4,357 exact decoded-to-padded-to-upload chains and a clean capture of
the formerly corrupted story close-up. Issues #57, #79, #95, and #109 preserve
the resolved threading, backpressure, row-layout, and decoder-drain failures.

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
change; focused tests prove success and each rollback path. The visible
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

Observed subset: frame intervals attribute guest versus host time, and the
sampling profiler in C211 identified linear dispatch lookup as the level-load
hotspot. Replacing it with validated sorted-table binary search reduced the
measured load frame from 4,592 ms to roughly 500 ms, a 9.2x improvement (C210).
The native movie rendezvous and upload paths also have bounded-wait and retained
staging fixes with measured reductions (C207 and C233).

Issue #141 identified per-thunk unwinding of `x86p_jit_engine_run` as a
structural crossing cost. x86port `d5d3b00` added an inline between-blocks
dispatch hook, and xmen2 now services import thunks and override bodies without
unwinding the JIT slice (`x86_engine_dispatch.c`, CVar
`jit.inline_dispatch`, default on). On the same driven in-game input path,
host-import share of wall time fell from roughly 62% to 18% and frames rendered
per fixed wall-time window rose roughly 15%. Later targeted unpaced sessions
recorded roughly 60 fps after the native override work (C281-C283), superseding
the earlier roughly 30 fps diagnostic measurement.

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

### S013 — Xbox evidence consolidation and static-path removal: partial

Observed subset: the Xbox investigation established durable controller,
ordinal-table, XBE-section, and runtime-boundary facts that remain useful to the
PC port. Those observations are preserved in the claims and RE documentation;
the Xbox executable is not the product conformance target. The retired static
experiment lifted and built all 24,663 detected functions across eleven
executable XBE sections, exercised positive controls for boundary,
deferred-flag, ordinal-table, and runtime-discovery mechanisms, and reached the
game's main thread. These are historical evidence facts, not authorization to
retain that execution path.

Gap: the repository still contains an obsolete Xbox static-recompiler build and
run path. It must not be generated, built, run, or presented as a second
product. Consolidate every still-useful binary/behavioral fact into its living
authority, then remove the static product machinery instead of retaining it as
a compatibility or oracle path.

### S014 — Apple Silicon macOS native host: partial

Observed subset: the earlier arm64 Mach-O host kept macOS's normal 4 GB
`__PAGEZERO` and translates logical 32-bit guest addresses through a separate
4 GB arena. C272 and issues #10/#123 prove exact Win32 4 KiB mapping state over
Apple Silicon's 16 KiB host protection granule. The normal launcher discovers
Homebrew dependencies and a repository-local game directory; a driven run
cleared all six intro movies, entered playable gameplay, accepted keyboard
input, rendered through SDL_GPU/MoltenVK, and sustained world and shadow draws.

Gap: this evidence predates the product-only JIT boundary. `shared/x86port` has
no ARM64 JIT backend, so Apple Silicon is not a qualified current product host
and cannot use the test interpreter as a shipping fallback. Native Windows
remains absent, Intel macOS is not a supported target, and
physical-controller/hotplug plus clean-machine provisioning still retain the
hardware-validation gaps described by S006 and G005.

### S019 — shared Alchemy gameplay boundary and MUA adoption: partial

Observed subset: pinned `shared/alchemy` revision `f95e093` provides the native
`alchemy` library for IGB/image/mesh/raster/Enbaya foundations,
`alchemy_input`, and optional `alchemy_input_sdl`. X-Men 2 provisions that
checkout and uses its XMLB/ARK tooling from maintainer and native-asset paths.
The shared input tests exercise stable controller slots, startup enumeration,
late attach, snapshot updates, shutdown, and rumble through the production SDL
backend described in `shared/alchemy/docs/input.md`.

Gap: `x2native` links none of the shared Alchemy libraries, and no shared
runtime header or function is present in the gameplay call path. The first
candidate is a narrow X-Men 2 guest `igControllerManager` adapter over
`alchemy_input`, A/B-verified against the existing DirectInput path for button
bits, pressure, axes, identity, lifecycle, and callbacks. That proof must keep
title action meanings, joining, assignment, and prompt policy here. MUA remains
deferred until every X-Men 2 project goal is verified; only then does MUA
migrate to the proven shared engine while preserving its gameplay source.

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
