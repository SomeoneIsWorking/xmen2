# HUD presentation and mobile layout

## Evidence and coordinate contract

The authenticated PC executable supplies these boundaries. Addresses below are
linked `XMen2.exe` addresses, not addresses in a DLL sharing its preferred base.
The 2026-09-05 investigation used Ghidra 12.0.4 and checked argument order,
return cleanup, field writes and float constants against x86 disassembly.
Analysis lives under `build/ghidra/`; disposable decompilations are under
`scratch/hud/decomp/`. These are maintainer inputs, never player prerequisites.

The HUD uses **x/depth/z scene coordinates**, with positive z pointing upward.
Its positions are not output pixels. The viewport singleton returned by
`005f6df0` is `00a0a138`, with vtable `006a3a9c`. Its recovered methods are:

| vtable offset | implementation | result |
| --- | --- | --- |
| `+20` | `005f6000` | width = scaleX (`+48`) × aspect (`+10`) × 384 |
| `+28` | `005f6030` | height = scaleZ (`+4c`) × 384 |
| `+2c` | `005f6080` | left = −(width − 512) / 2 |
| `+34` | `005f60a0` | right = 512 + (width − 512) / 2 |
| `+30` | `005f6040` | top = 384 + (height − 384) / 2 |
| `+38` | `005f6060` | bottom = −(height − 384) / 2 |
| `+3c` | `005f60c0` | horizontal midpoint |
| `+40`, `+44` | `005f60f0`, `005f6110` | left/right inset by width × field `+58` |
| `+48`, `+4c` | `005f6130`, `005f6150` | top/bottom inset by height × field `+5c` |

Constructor `005f6d60` sets the inset fractions to 0.035 and 0.075. A live
1280×720 run had aspect 1.7777778 and both scales 1; its logical bounds were
approximately x=[−85.3333,597.3333], z=[0,384]. A layout must use the viewport
contract and the actual presentation mapping rather than treating those values
as pixels or reflecting them around the output width/height.

## Presentation owners and ABI

`005a62c0` is the CHud visibility/composition owner. It checks byte `CHud+18`
and invokes `005a43d0`, `005a5170`, `005a2c60`, `005a25b0`, `005a2850`,
`005a40d0`, and `005a5c40` under their retail conditions. The previously named
“root” `005a43d0` is only the party selector/panel part of that composition.

| boundary | responsibility / evidenced contract |
| --- | --- |
| `005a43d0` | Party selector scene object at `CHud+1c`; per-hero panel calls |
| `005a3320` | Hero vitals/XP panel, frame object `CHud+24+slot*4` |
| `005a5170` | Inventory counters/icons including health and energy potions |
| `005a2c60` | Xtreme pips and scene object `CHud+20` |
| `005a25b0` | 100×10 fill meter; distinct from potion icons |
| `005a2850` | Separate 84×10 meter and icon presentation |
| `005a40d0` | Projected world-entity health bars |
| `005a5c40` | Timer and other text notices |
| `005a1ab0` | Separate CHudCharacter portrait presenter |

For **`005a3320`**, ECX is CHud. Stack arguments are hero (`esp+4`), position
(`esp+8`), colour (`esp+12`), party slot (`esp+16`), and player label (`esp+20`).
The raw call at `005a4ad4` proves this order. The old issue #139 claim that
`esp+8` was colour was incorrect: it mistook the explicit hero argument for
an implicit `this`. The previous root-shift visual regression remains a valid
observation, but that stack explanation does not.

The GUI object at `CHud+4` has vtable `0069dca4`:

- `+4c`, `0059a140`: thiscall, eight arguments, `ret 32`. Arguments are position
  vec3, size vec2, colour vec4, rotation, fill fraction, icon index, layer, flags.
  It checks visibility bit 0 at GUI `+85c30`, layer 0..2, and the corresponding
  resource `+85c24+layer*4`, then submits to `005ee180`.
- `+94`, `004bdfa0`: **a getter**, returning GUI+12. It does not draw text or
  consume stack arguments. `005f11b0` is the text submitter: thiscall, nine
  arguments, `ret 36`: font, x, z, width, height, scale, alignment, colour, text.

Ghidra's untyped indirect calls produced stack drift in larger HUD functions,
including apparent vector/colour confusion. Imported igVec setters and matrix
methods already have correct thiscall prototypes and stack purges; those imports
were not the cause. Treat arguments inferred from those untyped decompilations
as provisional until checked against their raw call sites.

## Portrait position and pointer selection

`005a1650` is CHudCharacter's position getter, called through virtual `+18`.
ECX is the portrait object; its sole stack argument is a writable vec3. Both
branches return that pointer in EAX and use `ret 4`.

Single-player placement composes the local anchor (`+8,+c,+10`), local scale
(`+14`), parent pointer (`+2c`), and parent's anchor/scale at the same offsets.
`src/native/hud_portrait_position.c` preserves the distinct x87 spill boundaries
at `005a16d0..005a1725`; algebraically rearranging the components can change
rounding. The runtime wrapper owns this native branch and retains the original
multiplayer branch. With `hud.verify`, the native output and return/stack
contract are compared against the original body before a presentation mapper
can run. This comparison is evidence only when its live denominator is nonzero.

The multiplayer branch obtains a per-slot layout from GUI vmethod `+dc` with
(slot, output, 0, 0), then adjusts x by −20 on the left or +100 on the right,
and z by −10. Its native replacement is not yet implemented.

`005a1ab0` calls the position getter and publishes the returned centre to
`00a0a0cc + slot*12`, where the slot is byte `portrait+4c`. The second coordinate
is reduced by 50 for scene depth. It obtains the position again for the head
scene transform. Thus remapping this getter moves the rendering and the retail
selection centre together. Do not apply a second affine transform to the same
portrait later in the scene-submit path.

The retail mouse handler `005f9eb0` checks x/z within ±20 of that centre,
sets hover selection `00a09f98` to 5..8, and emits the existing party-selection
action on `WM_LBUTTONDOWN`. `005fc100` also reads those centres for presentation.
Moving the centres preserves ordinary-size portrait selection; changing visual
portrait scale requires a corresponding, explicitly owned hit-region policy.

## Retained scene transforms and authored art

Model vmethod `+8`, `00573110`, resolves the named transform wrapper through
its model-resource table. Wrapper vtable `0069c108` provides:

- `+18`, `00570720(position, EulerRotation, uniformScale)`, `ret 12`: constructs
  scale/rotation, sets translation, then calls its own `+c`.
- `+c`, `00570970(matrix)`: tail-calls the underlying scene node's virtual
  `+8c`. A scoped matrix adapter at this boundary retains the original scene
  hierarchy, animation, resource ownership, and draw traversal.

The executable requests `ui/hud/m_healthpanel`, `m_multihealthpanel`,
`m_playercross`, and `m_xtremepanel`; the install also contains their PC variants
such as `UI/hud/m_healthpanel_pc.IGB` and `m_playercross_pc.IGB`. Character heads
come from `HUD/hud_head_*.IGB` and related `UI/hud/characters/` assets. These
remain user-supplied assets and must not enter source control or packages.

The panel's immediate 102×24 and 28×24 colour overlays are not proven bounds
of its frame asset. Its first HP fill has a 73×6 maximum extent. Authored model
inspection found `m_healthpanel_pc.IGB` has 52 objects, one vertex array and 60
vertices. Its raw vertex bounds are x=[−58,64], y=[−15,15], z=[0,7.99999].
Transform 33 translates by (−3,0,0); transform 36 maps local y to scene z and
local z to negative depth. These raw bounds require the scene hierarchy and
runtime root-matrix replacement to obtain final extents; they are not yet a
complete visible-frame measurement. The non-PC panel differs: 83 objects, two
arrays and 40 vertices, x=[−56.9721,57.2721], y=[−9,15], z≈[0,8].

The shared `igb_dump` confirms the object/transform inventory. Vertex extraction
used igblib's existing `_extract_vertex_data` with its documented XML2-PC default
slots and refused missing/empty arrays. The higher-level `extract_geometry`
entry currently has an absent `igblib.game_profiles` dependency; no empty
output from that broken entry was accepted as evidence.
