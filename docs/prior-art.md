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
