# Prior art: ports that already solved this

Consult these BEFORE designing a subsystem a mature port has had to solve.
Cite what you take, in the source file that takes it.

## Dusklight — https://github.com/TwilitRealm/dusklight

A shipping PC port of *Twilight Princess* on the `zeldaret/tp` decomp, with
Borealis underneath. **CC0**, so ideas and code may both be taken freely. Same
shape as this project, several years further along, and it moves daily — pull
before consulting.

Worth reading before you invent one of these:

| Subsystem | Where |
|---|---|
| Frame interpolation | `src/dusk/frame_interpolation.{h,cpp}` |
| Game-facing UI (RmlUi) | `src/dusk/ui/` |
| Developer overlays (ImGui) | `src/dusk/imgui/` |
| Config and settings | `src/dusk/config.{cpp,hpp}`, `settings.{cpp,h}` |
| Mod loading, texture replacement | `src/dusk/mod_loader.hpp`, `texture_replacements.*` |
| Input binding | `src/dusk/action_bindings.{cpp,h}` |
| Save handling, autosave | `src/dusk/autosave.{cpp,h}` |

### What has been taken so far

**File layout, in `src/vulkan/`** (cited in `igvk_context.h`). `src/dusk/` is
one small `.cpp`/`.h` pair per concern with a narrow header each --
`presentation.hpp` is seven lines, `gfx.hpp` is ten -- and nothing accretes
into a `renderer.cpp`. Applied here that split the renderer into the host GPU
device (which knows nothing about the guest), the ARK class, and one file per
group of engine slots. It replaced a single `igvk_visualcontext.c` that was
about to grow 98 slot implementations.

Two stacks for UI, on purpose: RmlUi for shipped UI, ImGui for developer
overlays. Not needed yet here, but the decision is theirs and pre-made.

### What has NOT been taken, and why

**Frame interpolation.** Their model is worth copying exactly when the time
comes -- RECORD-AND-REPLACE rather than substitute-and-re-issue: the sim tick
runs untouched, every final matrix is recorded keyed by its own address, the
presentation frame lerps prev-to-cur into a replacement table, and draw-time
sites consult a lookup. Guest state is never mutated, so it cannot leak. The
camera is interpolated as a POSE, never as a matrix lerp.

Not copied because there is no frame to interpolate: this port has never
rendered one. Doing it now would be a design with nothing to test it against.

### The caveat that matters

Their code is written against the TP decomp's types and a different graphics
stack. Take the DESIGN DECISION and the failure mode it avoids; porting the
code itself is usually not the win.

## D3D8 implementations — DXVK/d8vk and WineD3D

`src/d3d8/` implements Direct3D 8 on the host. Two mature open implementations
of the same interface exist, and one of them is already in this project's own
loop, so the decision to write ours needs stating rather than assuming.

- **DXVK** — https://github.com/doitsujin/dxvk, **zlib**. D3D8 landed in 2.4,
  merged from **d8vk** (~5k lines) and implemented on top of DXVK's D3D9.
  `dxvk-native` builds without Wine; whether d3d8 is built in the *native*
  configuration is UNVERIFIED here — nobody has checked its meson files.
- **WineD3D** — Wine's `d3d8.dll`, **LGPL**, designed to sit on Wine internals.
- Both are further along than this will be for a long time, and DXVK's d3d8 is
  what actually renders this game today: the Wine oracle (C005) depends on it.

### The decision: read them, do not depend on them

The port exists so that rendering can eventually be *changed* — interpolation,
replaced shaders, effects the 2005 engine never had. Anything sitting behind
someone else's translation layer is capped by what that layer chooses to
expose, and the interesting work is exactly the work that has to reach inside
the pipeline. So these are REFERENCES for semantics — what D3D8 actually
promises for a given state combination, which formats behave how, how
fixed-function maps onto a programmable pipeline — and the code here is ours.

Cite them in the file that learns from them, as with Dusklight.

### What using DXVK would NOT have saved

Worth recording, because it is the part that looks like a shortcut and is not.
DXVK's objects are 64-bit host C++ objects; recompiled guest code cannot call
one. The boundary in `d3d8_com.c` -- 32-bit `__stdcall` vtables in
guest-addressable memory, synthetic callback addresses, this-pointer
resolution, who-pops-what -- would have been needed either way, and it is what
already exists. What DXVK would have replaced is the part still ahead:
resources, state-to-pipeline translation, and vs/ps 1.1 bytecode.
