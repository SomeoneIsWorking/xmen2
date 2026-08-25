---
id: C265
kind: claim
status: holds
created: 2026-08-25
tags: native,presentation,resolution,registry,d3d8,rmlui,settings
depends: src/presentation/display_mode_seed.c#x2_display_mode_seed_boot, src/native/advapi32_host_store.c#advapi32_host_set_string, src/d3d8/d3d8_d3d8.c#d3d8_GetAdapterDisplayMode
---

## Claim

The port's output size reaches game-space rendering through a one-way,
once-per-boot publication into the game's own registry -- video.width x
video.height written as retail Display\Resolution REG_SZ "<w>x<h>" before any
guest code runs -- and the engine then parses that value itself, validates it,
and builds its D3D device at that size. The d3d8 adapter boundary reports the
REAL desktop size (SDL_GetDesktopDisplayMode) so windowed legality passes, and
enumerates the published size as an extra mode so exclusive-fullscreen
validation can accept it.

## Evidence

The retail encoding was read out of a run's own store:
`...Settings\Display|Resolution|1|38303078363030` is ASCII "800x600"; XMen2.exe
formats the value with the "%dx%d" at 0x006a4b80 (pushed at 0x00619ba7, inside
FUN_00619ac0's mode-list builder) and registers the "800x600" default at
0x00619857 (FUN_00619770). Live proof on this tree: an isolated profile whose
store said "800x600" and whose x2native.conf said 1920x1080 printed
"DISPLAY SEED: published video 1920x1080 ..." during host init, then the
ENGINE logged `d3d8: CreateDevice adapter=0 hardware-vertex 1920x1080`, with no
"Display failed!" revert, and the store file ended holding "1920x1080". A
second boot over the same profile printed "no change needed" and created the
device at 1920x1080 again. Before the desktop-size fix the same run produced
the issue #22 revert path ("*** Display failed! ... reverted to default",
CreateDevice 1920x1080 followed by a stored 800x600), which is what pinned the
hardcoded 1280x1024 desktop as the cause.

This is deliberately distinct from the design rejected in issue #111 / claim
C255: there, Display\Resolution was made a live synthetic VIEW of
x2native.conf coupled into every guest read; here nothing is coupled after
boot -- publication happens once through advapi32_host_store, announced either
way, and afterwards the value belongs to the game exactly as if the player had
set it in retail Options. Known policy consequence: a resolution chosen in the
RETAIL options screen holds only until the next launch, when Port Settings'
value is published over it; runtime changes in Port Settings resize the window
immediately and reach game-space on the next launch (the overlay says so),
because the engine has no runtime re-resolve path -- IDirect3DDevice8::Reset
is unimplemented (slot 14 NULL).

## What would falsify it

A launch whose log shows `CreateDevice ... <WxH>` differing from the value the
DISPLAY SEED line published while claiming success, or a recurrence of the
"Display failed! ... reverted to default" pair after a seed, or the store file
holding a different Resolution than was published at boot end. Also: evidence
that the engine re-reads Display\Resolution mid-session (a retail-options
resolution change taking effect without any device recreation) would falsify
the "no runtime re-resolve path" half.
