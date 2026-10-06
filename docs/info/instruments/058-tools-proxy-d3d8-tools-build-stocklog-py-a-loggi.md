---
id: I058
kind: instrument
status: trusted
created: 2026-08-15
---

## Instrument and scope

`tools/proxy_d3d8` + `tools/build_stocklog.py` -- a logging `d3d8.dll` the STOCK game loads under Wine. It records the original engine's lighting boundary and, when both shadow-control environment inputs are present, one F9-selected frame of D3D8 resources, transforms, stage state, render state, and draws.

## Validated by

It showed the OTHER answer on the very first run: the port's own instruments said the engine hands over near-black lights (C199), and the control's log says 0 of 139,536 gameplay SetLight calls carry a black diffuse and the engine uses 44 light indices up to 51 where the port held 16. Validated as a pass-through before it was trusted for values: slot 44's generic trampoline disassembles to jmp *0xb0(%ecx) and fwddev_table[44] points at it (0xb0 = 44*4), the trampoline stride is set AND checked by .org so a body that outgrew it cannot assemble, and the build refuses if the proxy exports fewer symbols than the real d3d8, if anything imports d3d8 by ordinal, or if the one implemented export went missing -- which it did on the first attempt, because supplying a .def turns off ld's auto-export. The game ran to gameplay through it by hand, which is the end-to-end proof that the 108 forwarded slots forward correctly.

The shadow extension is mechanism-verified at the D3D8 boundary.
`test_shadow_trace` drives the shipping trace core through ordinary and
DetailedShadow-enabled frames, combined resource/state/Clear capture, and all
refusal bounds. `shadow_trace_compare.py --selftest` proves a positive
resource/draw delta plus eight refusal paths. This instrument observes only the
D3D8 boundary; it makes no claim that a selected draw came from a named
Alchemy or title function.

## DetailedShadow control workflow

Build two isolated stock run directories:

```sh
python3 tools/build_stocklog.py shadow-off
python3 tools/build_stocklog.py shadow-on
```

There is no visible retail Detailed Shadows control: the setting inventory proved it is hidden backing state. The control therefore uses two independent proxy inputs. At `Direct3DCreate8`, before the later retail display-settings load, `X2_SHADOW_FORCE` makes the proxy validate XMen2.exe's ADVAPI32 `RegQueryValueExA` import and redirect only a successful DWORD query whose value name is exactly `DetailedShadow`. The observer records the original registry result before substitution, the number of successful substituted reads, and the live backing byte after the retail loader stores the result. `X2_SHADOW_EXPECT` is not the force value's alias: it remains the independent assertion checked again at F9. The comparator refuses an unknown seam, zero successful reads, a read-back mismatch or a swapped expected value. The older live-byte write was invalid: a real OFF run proved that `FUN_00619770` later overwrote it at 0x006198c3 (issue #105). The current seam changes neither the configured Wine prefix nor its registry. Reach the same gameplay scene in each run and press F9 once. F9 selects exactly the next D3D8 frame; resource creation is recorded in a bounded catalogue before selection so a render-target texture allocated during level load is not lost.

```sh
X2_SHADOW_FORCE=0 X2_SHADOW_EXPECT=0 tools/run_shim.py shadow-off 540
X2_SHADOW_FORCE=1 X2_SHADOW_EXPECT=1 tools/run_shim.py shadow-on 540
python3 tools/shadow_trace_compare.py \
  scratch/run/shadow-off/d3d8_shadow_trace.jsonl \
  scratch/run/shadow-on/d3d8_shadow_trace.jsonl
```

The default bound is 4,096 catalogue records and 4,096 selected-frame events; `X2_SHADOW_MAX_EVENTS=<1..100000>` changes both limits. The comparator refuses any dropped event, texture-hook blind spot, swapped setting, or frame that reaches no D3D8 draw. Resource records name `CreateTexture` dimensions/levels/usage/format/pool and `GetSurfaceLevel`, default and standalone render/depth targets, `CopyRects`, and `UpdateTexture`. Ordered frame records carry target binds and their default/explicit/unknown source, all transforms and stage-state changes, relevant depth/blend/stencil/colour-write state with D3D8 defaults distinguished from explicit writes, `Clear` rectangles/flags/colour/depth/stencil/result, pixel-shader binds and all four D3D8 draw variants.

## Known failure modes

- F9 is a human-selected scene gate. Two captures from different rooms or animation moments are not comparable merely because both contain one summary.
- Holding F9 does not capture unbounded frames: the edge arms one frame. Release and press again for another capture; the comparator deliberately refuses a file containing more than one summary.
- Textures are not wrapped: the observer patches `GetSurfaceLevel` in each real texture vtable, preserving COM identity through `QueryInterface`, `GetDevice` and device `GetTexture`. The vtable catalogue is bounded to 32 distinct implementations; overflow, an unpatchable vtable or a failure to restore its page protection marks the summary incomplete, and the comparator refuses it.
- The DetailedShadow backing-byte address and registry-query IAT are build-specific. The proxy validates both the executable store instruction and the resolved ADVAPI32 import before recording a control; a different executable is UNKNOWN rather than an RVA guess.
- The proxy observes the D3D8 boundary, not named guest functions. A resource or draw delta is evidence of changed renderer traffic, not proof of the title-side shadow owner that emitted it.
