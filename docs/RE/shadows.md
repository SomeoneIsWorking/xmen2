# Shadows: retail owner and native enhancement boundary

## Verdict

The PC retail game does not implement the requested light-cast dynamic shadows
through its `DetailedShadow` setting. That setting is persisted but not exposed
by the retail Advanced Options page and has no gameplay or renderer consumer.
The title's actual shadow owner is
`CShadowMgr`, which produces per-entity, six-vertex floor decals as ordinary
scene geometry. The current native run reaches that producer.

A depth-map shadow pass would therefore be a new native enhancement, not a
missing D3D8 translation of the observed retail route. It must start with
explicit game policy. The resolved GPU packet cannot reliably invent caster,
receiver, or light ownership after scene semantics have been discarded.

## Evidence and denominators

| Question | Observation | Limit |
|---|---|---|
| Does `DetailedShadow` control rendering? | The backing byte at `0x00a68d40` has exactly 4 incoming references: default (`FUN_006196c0`), registry load (`FUN_00619770`), registry save (`FUN_00619440`), and an options/settings reload (`FUN_0061b030`). There are 0 gameplay, render, or `CShadowMgr` references. The retail Advanced Options page does not create a corresponding control. | A changed retail executable needs a new xref census. |
| Do the named Alchemy shadow classes run in the captured scene? | Matched OFF/ON menu and Sanctuary captures reported 0 planar, 0 projective, and 0 self-shadow probes among more than 200,000 positive probe records. Both selected gameplay frames had 4 clears, no render-target/copy/update operation, depth bias 0, stencil disabled, and colour mask `0xf`; see I058 and issue #105. | The two captures are not frame locked and do not enumerate unknown routes. |
| What title code owns shadows? | `.?AVCShadowMgr@@` / `.?AVIShadowMgr@@` RTTI leads to singleton `FUN_004b6dc0`, render callback `FUN_004b64e0`, and floor-decal helper `FUN_004b5700`. The manager owns at most 32 entries. Static direct singleton use is 23 calls in 10 functions; actor setup/update/teardown use the manager's allocate/update/release slots. | Names other than RTTI are inferred from call graph and behavior, so addresses remain the durable identifiers. |
| Does native reach the title producer? | Existing native traces provide the positive control. `scratch/logs/level5.log` records 6 entries to `FUN_004b64e0` and 16 to `FUN_004b5700`; `level6.log` records 4/10, `level7.log` 1/9, and `tf.log` 4/20. The helper is followed by normal vertex-array, geometry, sorter, and render-package construction. | Boundary execution proves producer/packet construction. Scratch traces are evidence inputs, not tracked assets. |
| What D3D draw does the floor helper produce? | Static RE gives an exact chain: `FUN_004b5700` calls builder begin `FUN_00583fc0` with mode 0, appends 6 XYZ/UV/diffuse vertices through `FUN_005840a0`, then `FUN_00584600` configures one geometry primitive. That reaches D3D as a non-indexed triangle fan with 4 primitives, FVF XYZ+DIFFUSE+TEX1 (`0x142`), stride 24, offsets colour 12/UV 16. In both selected stock frames there are 5 such fan4 draws at ordinals 158, 161, 164, 167, and 168, interleaved with the same manager's fan24 selection/status decals. | The static producer-to-primitive mapping is deterministic; the selected trace did not carry object identity or vertex bytes, so dynamic per-entry attribution remains an additional check. |
| Does native submit the same draw family? | Existing native gameplay frame dumps show 9 consecutive expanded `trilist x4` draws using one texture, modulate, alpha blend, depth test without depth write, unlit/no normals, stride 24, colour offset 12, UV offset 16, cull none, less-equal depth, zero bias, no stencil, and full colour writes. The shipping fan path expands every D3D fan to an indexed triangle list; its production-interface pixel test proves all four triangles render, and that treating the same vertices as a list does not. | These runs were not frame synchronized with the stock five-draw capture. They prove native submission and fan rasterization, not byte-for-byte equality of one actor's vertices. |
| Are shadow types serialized in model assets? | 4,885 of 4,886 installed IGB files parsed; 0 instances matched planar, projective, self-shadow, or shadow-processor type names. `Models/Weapons/tank_turret.IGB` was the one parse failure. | Runtime construction and title-specific types remain possible; there is no positive IGB control for these class names. |
| What title asset policy exists? | 8,180 script/container candidates were searched. All 43 binary containers containing plain `shadow` parsed. Parsed data has 72 `shadow` attributes on entity nodes (71 true, 1 false) and 20 `no_shadow=true` attributes (19 trigger nodes, 1 powerup). There are 0 `shadow_caster`, `shadow_receiver`, `shadow_light`, `CShadowMgr`, `IShadowMgr`, or `DetailedShadow` strings in that corpus. | The attributes prove title vocabulary, not their complete defaulting rules. Media and executable files outside the candidate set were not searched as scripts. |

## The retail route

`CShadowMgr` registers `FUN_004b64e0` as a scene/render callback during
initialisation. The callback walks the active portion of its 32-entry pool.
Flag bit zero selects `FUN_004b5700`; other bits select floor selection/status
decals owned by the same manager. Actor paths allocate, update, release, and
toggle the entries.

`FUN_004b5700` builds a six-vertex triangle fan from explicit per-entity
position and direction data, with black vertex colour and alpha derived from
entry state. `FUN_00583fc0` begins mode zero, `FUN_005840a0` appends each
position/UV/diffuse tuple, and `FUN_00584600` finalizes one four-primitive fan.
The helper configures an Alchemy geometry attribute and submits it through the
ordinary scene traversal and sorter. It has no light-camera matrix, caster
render, receiver sample, or offscreen resource operation. Calling it a decal
describes the observed geometry route; it must not be generalized into a
screen-space blob implementation.

The independent byte `DAT_006d54ed` gates manager registration/visibility in
actor paths. It defaults to one, has three reads and no writes, and has no
relationship to `DetailedShadow`. It behaves as a fixed title feature gate,
not a user quality setting.

## Current native ownership

`src/d3d8/d3d8_drawcall.c` resolves each ordinary D3D draw into `GpuDraw` using
the title's row-vector `world * view * projection` convention.
`src/gpu/gpu_draw.c` owns fixed-function pipeline translation and execution.
The title decal arrives there as ordinary geometry, just as it does in retail.
The exact fixed-function signature is:

- D3D non-indexed triangle fan, four primitives from six vertices; FVF `0x142`
  (XYZ, diffuse, one UV), stride 24, colour at byte 12 and UV at byte 16;
- pixel shader zero; one texture; stage zero colour and alpha both MODULATE
  texture by diffuse, linear min/mag filtering and no mip filtering;
- alpha blend enabled with SRCALPHA/INVSRCALPHA, alpha test disabled;
- depth test less-equal enabled, depth write disabled, cull none, lighting off;
- zero depth bias, stencil disabled, full colour writes, default scene targets.

Both selected stock controls contain the same five fan draws, at the same
ordinals and with the same texture and recorded state. Native gameplay frame
dumps show the corresponding packet family as `trilist x4`, because the
shipping renderer expands unsupported GPU triangle fans exactly. Those packets
reach `gpu_draw`, and the production fan selftest proves all four expanded
triangles rasterize. No missing renderer state or refused draw is evidenced.

This distinction matters. The GPU layer retains raw depth-bias, stencil, and
colour-write values, but its pipeline key does not yet consume all of them.
That is a general state-translation gap, not evidence that it breaks this draw:
the matched retail frames used zero bias, no stencil, and full colour writes.
Likewise, unimplemented D3D8 render-target/copy methods are not on the observed
decal route. The next parity test is dynamic per-entry attribution plus matched
vertex bytes, not construction of a shadow map.

The presentation boundary is already correct for a future native pass. The
game renders into the logical scene from `gpu_present_scene`; only afterward
does `gpu_present_composite` aspect-fit that scene into the physical window and
RmlUi render over it. A world-space shadow must be complete before the logical
scene is composited, so letterboxing and UI never alter its light projection.

## Policy required for a light-cast enhancement

The retail route supplies no shadow-map light-selection or receiver semantics.
Before implementation, the project needs explicit answers for:

- **Casters:** actor categories, transparent/alpha-tested geometry, equipment,
  particles, and the exact role/defaults of map `shadow` and `no_shadow` data.
- **Receivers:** static terrain only, all opaque world geometry, characters, or
  an authored subset; plus exclusions for water, effects, and UI-world meshes.
- **Lights:** one authored/map directional light, a global sun, or a selected
  local light per caster. “Strongest enabled light” is an unevidenced heuristic.
- **Projection:** a global directional map/cascades or bounded per-character
  projections, with explicit fit, near/far, distance, and update rules.
- **Quality:** map count and resolution, update budget, depth bias, normal bias,
  comparison/filter kernel, softness, and fade policy.

These are visible game-design choices. They cannot be hidden in the renderer as
constants inferred from whatever draw happened to be available.

## Smallest correct enhancement architecture

Once policy is chosen, keep the boundary narrow:

1. A scene-side shadow owner selects stable caster and receiver identities and
   one evidenced/configured light. It computes light view/projection and target
   descriptors while object semantics and bounds still exist.
2. It publishes an immutable per-frame `ShadowFrameInput`: caster draw
   generations, receiver draws, light matrices, target description, bias, and
   filter policy. Delayed replay must retain the exact draw-time buffer
   generation or immutable data; retaining a mutable buffer handle repeats the
   previously fixed dynamic-upload failure.
3. A GPU shadow-resource owner allocates and resizes first-class sampleable
   depth targets. The diagnostic `gpu_offscreen_*` readback helper is not this
   resource API, and the new native feature should not masquerade as D3D8
   `CreateRenderTarget` unless a retail caller actually needs that interface.
4. The caster pass writes depth from the light matrices. The receiver path
   samples it with the declared comparison/filter policy while preserving the
   material's existing depth, blend, cull, viewport, scissor, and bindings.
5. Pass entry/exit owns complete state isolation. No guest D3D state is mutated
   to smuggle native state between draws, and target restoration is structural,
   not dependent on a successful draw.
6. The pass executes inside the logical scene before
   `gpu_present_composite`. Physical aspect fitting and RmlUi stay downstream.

This follows Dusklight's ownership lesson: its scene code chooses shadow image,
casters, receivers, and matrices, while the renderer owns target setup and
restoration. Its Twilight Princess selection rules, constants, and pass format
are not evidence for X-Men Legends II and are not copied.

## Validation gates

Retail decals need a positive same-scene stock/native draw signature, matching
six-vertex data and complete texture/blend/depth state, followed by a pixel
capture. A light-cast enhancement additionally needs production-interface tests
that prove a non-symmetric light matrix, caster motion/removal, receiver
exclusion, target/state restoration after failure, and identical world result
under wide and tall physical aspect fits. A test-only renderer or hard-coded
expected image would not validate the shipping pass.
