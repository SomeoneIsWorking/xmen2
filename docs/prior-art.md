# External provenance and historical references

This document preserves attribution and historical context for external code,
assets, vocabulary, and ideas already evaluated or adapted. It is not an
architecture authority, a design checklist, or a source of current project
requirements. Current ownership comes from `docs/codemap.md`; current behavior
and gaps come from `docs/project-state.md`. Keep exact provenance for material
actually reused.

## Outline typefaces for the game's text

`assets/fonts/` holds the unmodified faces `outline_glyph_atlas.cpp` draws the
retail text in (external/xmen2-decomp/docs/notes/text.md), each shipped beside its license:

| file | source | license | stands in for |
|---|---|---|---|
| `Barlow-BlackItalic.ttf`, `Barlow-Black.ttf` | [google/fonts](https://github.com/google/fonts) `ofl/barlow` at `89f5431ff0db41bd2fe3f7ba21a723a01622428b` (The Barlow Project Authors) | SIL OFL 1.1, `Barlow-OFL.txt` | the bold italic small-caps `X2F_med`/`big`/`thin` faces, and the upright `X2F_hud` capitals |
| `DSEG7Classic-BoldItalic.ttf` | [keshikan/DSEG](https://github.com/keshikan/DSEG) release v0.46 | SIL OFL 1.1, Reserved Font Name "DSEG", `DSEG-OFL.txt` | the seven-segment `font_XMEN_digital` digits |

The Reserved Font Name means a modified DSEG may not be called DSEG; ship it
unmodified.

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

Record exact provenance in the file that adapts material from them.

### What using DXVK would NOT have saved

Worth recording, because it is the part that looks like a shortcut and is not.
DXVK's objects are 64-bit host C++ objects; x86-32 guest code cannot call
one. The boundary in `d3d8_com.cpp` -- 32-bit `__stdcall` vtables in
guest-addressable memory, synthetic callback addresses, this-pointer
resolution, who-pops-what -- would have been needed either way, and it is what
already exists. What DXVK would have replaced is the part still ahead:
resources, state-to-pipeline translation, and vs/ps 1.1 bytecode.

## Title prior art

Work on X-Men Legends II itself, and the decomp survey, live in the decomp repository:
[xmen2-decomp `docs/prior-art.md`](../external/xmen2-decomp/docs/prior-art.md).
