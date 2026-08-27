# X-Men Legends II project state

This is the authoritative inventory of what the port demonstrably does now and
what remains partial, blocked, or absent. Epic intent belongs in
[`project-goals.md`](project-goals.md), atomic work in [`issues/`](issues/),
ownership in [`codemap.md`](codemap.md), and the ordered binary-evidence chain
in [`re-frontier.md`](re-frontier.md).

States: `verified` means the stated outcome was observed with durable evidence;
`partial` names both the demonstrated subset and the exact remaining gap;
`blocked` names its blocker; and `missing` means the capability is absent.

## Current focus

**S004 — native Alchemy 2D/UI rendering.** The prompt SVG slice is verified;
the active work is to reverse-engineer and port the remaining semantic 2D/UI
draw paths above D3D8.

## Capability inventory

| ID | Capability or outcome | State | Factual dependency | Goals |
|---|---|---|---|---|
| S001 | Fresh-clone provisioning and the default native launcher | verified | — | G001, G005 |
| S002 | Wine-free native execution of the recompiled PC game | partial | S001 | G001, G002 |
| S003 | Faithful rendering of the reached game path | partial | S002 | G002 |
| S004 | Native Alchemy 2D/UI rendering above the D3D8 seam | partial | S003, S012 | G002, G004, G006 |
| S005 | Native audio mixing and SFD movie playback | verified | S002 | G002 |
| S006 | SDL3 keyboard/controller input, assignment, and hotswap | partial | S002 | G002, G004 |
| S007 | Xbox-derived controller defaults and source-sensitive prompts | partial | S006 | G002, G004 |
| S008 | Port-owned RmlUi settings and input-binding surface | partial | S003, S006 | G002, G004 |
| S009 | Direct development boot into an initialized game | partial | S002 | G002, G004 |
| S010 | Measured frame and level-load performance | partial | S002, S016 | G003 |
| S011 | Wine oracle and evidence-backed differential RE workflow | partial | — | G002, G006 |
| S012 | A/B-toggleable native overrides and incremental engine replacement | partial | S002, S011 | G001, G006 |
| S013 | Secondary native recompilation of the Xbox build | partial | S011 | G006 |
| S014 | Apple Silicon macOS native host support | partial | S001 | G005 |
| S015 | Transactional autosave and direct retail Continue restore | verified | S002 | G002 |
| S016 | Live control, capture, input, and runtime diagnostic channel | verified | S002, S003 | G002, G006 |

## State details and evidence

### S001 — fresh-clone provisioning and launcher: verified

Observed capability: on Linux x86-64 and Apple Silicon macOS, zero-argument
`./run.sh` enters the locked `uv` environment, validates user-supplied game
files, restores redistributable dependencies and generated recompilation inputs
without Ghidra, builds all twenty modules, and launches the current native
D3D8-backed product. The launcher recognizes a matching game directory placed
at the repository root and resolves Homebrew packages through their keg paths.

Evidence: C182 records a cold-path run after the virtual environment, generated
sources, native assets, shared dependencies, and build tree were moved aside;
the launcher recreated them, presented frames, and passed its launcher and CTest
controls. Issue #110 records the original cold-path dependency defects; issue
#125 records and tests the boundary that maintainer-only `re-harness` is not a
player bootstrap dependency.

### S002 — Wine-free native execution: partial

Observed subset: `x2native` maps and initializes the original PE images, runs
their locally generated C bodies in a 64-bit native host, supplies the reached
Win32/CRT/DirectInput/DirectSound/D3D8 boundaries, and has traversed menus,
movies, level load, gameplay, death, and return-to-menu paths without a hybrid
machine-code fallback. The native BehavEd context/scheduler and title timed-event
players complete the verified tutorial control-lock cutscene synchronously,
silently, and without advancing guest frame/time (C274). Callback cleanup and
module-aware override routing are checked by C178 and C209.

Gap: coverage is not completion. Unreached imports remain fail-loud poison
thunks; guest exception delivery, LAN networking, and optional COM/system
facilities are absent; and no person has yet driven a physical controller
through a representative level. The observed unattended loop is not a
playability gate.

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

### S004 — native Alchemy 2D/UI rendering: partial, current focus

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
atomicity root cause.

Gap: this verifies only the prompt SVG slice. Stock ASCII, panels, sprites,
batching, and other display-list geometry still submit through the recompiled
engine and D3D8 host. Each semantic owner must be reverse-engineered, ported,
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
before and after loading a save.

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
ownership, migration, source switching, reconnect, and slot reuse.

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

Gap: no target frame-time or load-time budget defines "fast enough"; current
headless performance remains roughly 30 fps with the game cap removed, the
roughly 500 ms load hitch remains visible, and asset I/O has not been profiled.

### S011 — oracle and differential RE workflow: partial

Observed subset: the stock PC build runs under Wine/Xvfb as a capturable oracle
(C005); proxy DLL load transparency and `__thiscall` interoperability are
verified; Ghidra exports, runtime missing-target discovery, claims,
instruments, issue catalog, and fail-loud translation checks provide durable
binary and runtime evidence. The oracle cache prevents repeated identical
control runs.

Gap: frame-level A/B is not deterministic across boot-movie timing, and broad
lockstep differential coverage does not exist. Most generated functions are
executed through real paths but not individually compared against the original;
the x87 corpus in particular lacks constructed-object differential tests.

### S012 — native overrides and engine replacement: partial

Observed subset: module-qualified runtime overrides keep their recompiled
bodies alive for super-calls and A/B checks, and direct and indirect calls route
through the same override table (C209). Native owners now replace selected
boot, input, save, media, UI, and prompt-rendering behavior. C270 measures the
current engine-to-D3D8 dialect, and S004 proves one semantic rendering slice can
be moved above it without inventing a lowered-D3D classifier. The in-game
cutscene player also ports the BehavEd timed-fiber and title timed-event pumps,
retains their ordinary strict-deadline behavior, and completes only causally
owned work synchronously. The visible-record and camera-only tutorial gates
pass 11/11 and 10/10 with control restored, no guest frame or clock advance,
zero dialogue-presentation leaks, and every reached cutscene-owned DirectSound
start suppressed (C274);
[`RE/cutscene_player.md`](RE/cutscene_player.md) records the binary chain.

Gap: most of the Alchemy renderer and game remain mechanically recompiled.
Stock 2D, scene traversal, materials, lighting, shadows, render targets, and
their D3D8 call sites must be reverse-engineered and ported subsystem by
subsystem; `src/d3d8/` remains required until its last evidenced caller moves.

### S013 — secondary Xbox recompilation: partial

Observed subset: all 24,663 detected functions across eleven executable XBE
sections lift to C and build; boundary, deferred-flag, ordinal-table, and
runtime-discovery mechanisms have positive controls. The native Xbox build
executes the game's main thread.

Gap: the path still reaches unresolved indirect targets, its native CRT heap is
explicit hack debt, kernel bodies remain unaudited, 239 deleted jumps in two
functions remain diagnosed-but-unfixed, and nothing renders. This is a
secondary evidence path, not the live PC product.

### S014 — Apple Silicon macOS native host: partial

Observed subset: the default arm64 Mach-O keeps macOS's normal 4 GB
`__PAGEZERO` and translates logical 32-bit guest addresses through a separate
4 GB arena. C272 and issues #10/#123 prove exact Win32 4 KiB mapping state over
Apple Silicon's 16 KiB host protection granule. The normal launcher discovers
Homebrew dependencies and a repository-local game directory; a driven run
cleared all six intro movies, entered playable gameplay, accepted keyboard
input, rendered through SDL_GPU/MoltenVK, and sustained world and shadow draws.

Gap: native Windows remains absent, Intel macOS is not a supported target, and
physical-controller/hotplug plus clean-machine provisioning still retain the
hardware-validation gaps described by S006 and G005.

### S015 — transactional autosave and Continue: verified

Observed capability: successful map-load transactions publish an exact retail
autosave leaf without modifying the manual slot, and persisted Boot Continue
uses the retail mode-3 save-manager/deserializer chain to restore that map and
party without intro movies, the menu map, or user input.

Evidence: C246 records exact autosave size, unchanged manual-slot size/mtime,
and a second-run retail load; C261 records the 13/13 direct-Continue live case,
including the saved map, resolved player actor, active tutorial conversation,
zero movie opens, and no menu-map open. Issues #99, #113, and #119 preserve the
resolved save-authority, conversation, and first-cutscene boundary defects.

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
