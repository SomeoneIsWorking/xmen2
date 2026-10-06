# Shadows: retail owner and native enhancement boundary

## Verdict

The PC retail game does not implement the requested light-cast dynamic shadows
through its `DetailedShadow` setting. That setting is persisted but not exposed
by the retail Advanced Options page and has no gameplay or renderer consumer.
The title's actual shadow owner is
`CShadowMgr`, which produces per-entity, six-vertex floor decals as ordinary
scene geometry. The current native run reaches that producer.

A depth-map shadow pass is therefore a new native enhancement, not a missing
D3D8 translation of the observed retail route. The implemented first pass uses
explicit generic GPU-packet policy and says where that falls short of future
authored scene semantics.

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
| What title asset policy exists? | 8,180 script/container candidates were searched. All 43 binary containers containing plain `shadow` parsed. All 72 `shadow` attributes are on `world/entity` nodes whose classname is `monsterspawnerent`; the executable's sole literal is team/alliance enum value 6, not a renderer property. All 20 `no_shadow=true` values are power-style powerup/effect metadata, stored by `CPowerup` at `+0x5d`. There are 0 `shadow_caster`, `shadow_receiver`, `shadow_light`, `CShadowMgr`, `IShadowMgr`, or `DetailedShadow` strings in the asset corpus. | These names cannot be generalized into global caster/receiver policy. Media and executable files outside the candidate set were not searched as scripts. |

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

## Native light-cast enhancement

The enhancement is deliberately generic packet policy, not a claim about retail
`DetailedShadow` or authored entity tags. Asset/script RE found no defensible
global caster/receiver semantics: all 72 `shadow` attributes are on
`world/entity` nodes with `classname=monsterspawnerent`, while the
executable's sole `shadow` literal is team/alliance enum value 6. The 20
`no_shadow=true` attributes belong to power-style powerup/effect metadata;
`CPowerup` stores that bit at `+0x5d`. Neither vocabulary is consumed as
render policy here.

The explicit current policy is:

- caster: non-pretransformed triangle list/strip with world position, depth test
  and depth write, no alpha blending. Alpha-tested packets cast through their
  real UV, texture alpha, and D3D-normalized alpha reference; packets without a
  UV cannot be classified and do not cast;
- receiver: the same solid geometry, including programmable packets, plus lit
  fixed-function packets whose material is not emissive;
- light: the first enabled, non-black directional light in the title's ordered
  light array. Point and spot lights do not silently become shadow lights;
- projection/update: each frame recovers the camera view-projection from the
  first eligible fixed draw, unprojects its eight D3D clip corners, fits one
  orthographic directional map with 5% XY padding, and records every eligible
  caster in the same frame;
- quality: one sampleable depth target at the configured 512/1024/2048/4096
  resolution (1024 default), raster bias 1.25 plus slope bias 1.75, receiver
  depth bias 0.0015, 3x3 manual PCF, and 0.55 maximum darkening.

These numeric values are visible enhancement policy, not reconstructed retail
constants. The RmlUi Video page exposes an On/Off switch and resolution cycle,
and persistence lives in `video.dynamic_shadows` /
`video.shadow_resolution`.

### Renderer ownership and skinned geometry

`shadow_policy.{c,h}` owns classification and matrices without SDL resources.
`gpu_shadow.{c,h}` owns the sampleable depth target, depth pipelines, separate
command buffer, counters, and complete pass lifetime. `gpu_draw.c` publishes
the exact draw-time SDL buffer generation, texture, sampler, and resolved packet
state; the shadow owner never retains a mutable guest buffer handle for replay.

Programmable VS 1.1 draws already contain the exact post-skinning homogeneous
clip position produced by the title shader. For these packets the enhancement
multiplies that value by the inverse of the same frame's camera view-projection
and then the light view-projection. It therefore reconstructs deformed world
position from the exact shipping shader output rather than reimplementing the
bone shader or casting from undeformed source vertices.

The shadow command buffer is submitted before the logical-scene command buffer.
The receiver shader samples the resulting depth map in the ordinary scene pass;
only afterward does `gpu_present_composite` aspect-fit the logical image and
RmlUi draw over it. Guest D3D state is not mutated, and resolution changes
recreate only the shadow target between frames.

### Limits

This first generic policy does not honor authored `shadow` / `no_shadow`
tags, select local lights, cascade a sun map, apply normal bias, or cast blended
particles. Fixed emissive surfaces are excluded as receivers. Programmable
packets no longer expose an authored material identity after VS execution, so
their receiver admission is geometric rather than tag-aware. Adding authored
classification belongs at Alchemy scene traversal while `igObject*` / node
identity exists; guessing it from resolved GPU packets would be a second,
conflicting authority.

## Validation

The production `gpu_shadow_selftest` renders the same light/caster/receiver
through `gpu_draw` three ways. With shadow sampling enabled, 182 pixels
darkened; disabling the pass removed those 182; removing the caster while
leaving the pass enabled also removed all 182; 3,914 control pixels remained
bit-identical. The test therefore fails if either caster depth or receiver
sampling is bypassed, and proves the other answer rather than accepting any
changed image.

A bounded live `act0/tutorial/tutorial1` run reached 295,017 submitted draws,
including 11,836 VS 1.1 draws / 5,886,064 vertex invocations. The shadow pass
selected a directional light in 2,685 of 3,803 frames and submitted 179,631
casters plus 123,420 receivers; all 11,836 programmable draws appeared in both
caster and receiver denominators. It reported zero renderer refusals and zero
shadow resource/pass failures. The 800x600 captured frame contains 81,795
colours. This is positive title-scene coverage; it is not evidence that the
generic policy matches unobserved authored shadow intent.

Retail decal parity still needs same-scene object-to-draw identity plus matching
six-vertex bytes. That check remains separate from this enhancement.
