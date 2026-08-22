---
id: I058
kind: instrument
status: trusted
created: 2026-08-15
---

## Instrument and scope

`tools/proxy_d3d8` + `tools/build_stocklog.py` -- a logging `d3d8.dll` the STOCK game loads under Wine. It records the original engine's lighting boundary and, when both shadow-control environment inputs are present, one F9-selected frame of shadow-path entry points, D3D8 resources, transforms, stage state, render state and draws.

## Validated by

It showed the OTHER answer on the very first run: the port's own instruments said the engine hands over near-black lights (C199), and the control's log says 0 of 139,536 gameplay SetLight calls carry a black diffuse and the engine uses 44 light indices up to 51 where the port held 16. Validated as a pass-through before it was trusted for values: slot 44's generic trampoline disassembles to jmp *0xb0(%ecx) and fwddev_table[44] points at it (0xb0 = 44*4), the trampoline stride is set AND checked by .org so a body that outgrew it cannot assemble, and the build refuses if the proxy exports fewer symbols than the real d3d8, if anything imports d3d8 by ordinal, or if the one implemented export went missing -- which it did on the first attempt, because supplying a .def turns off ld's auto-export. The game ran to gameplay through it by hand, which is the end-to-end proof that the 108 forwarded slots forward correctly.

The shadow extension is mechanism-verified and has produced both negative selected frames and positive title-owner coverage. `test_shadow_trace` drives the shipping trace core through `none`, the three Alchemy paths, and the title floor-decal path, plus combined resource/state/Clear capture and a draw tagged while the title custom-geometry traversal is active. Separate cases prove expected/actual DetailedShadow mismatch, event overflow and incomplete texture-vtable coverage all refuse. `shadow_trace_compare.py --selftest` proves an engine/title answer and eight refusal paths. `gen_probes.py --check` validates the three Alchemy entries plus XMen2 `FUN_004b64e0`, `FUN_004b5700`, and enclosing `FUN_006024f0`. The generated table now carries the PE program name and preferred image base from each Ghidra export: the stock hook uses the main module for `.exe`, the named loaded module for DLLs, relocates `linked-image_base`, and still compares every prologue byte before patching. A bounded stock run installed all executable probes; before level load `FUN_004b64e0` was positive while `FUN_004b5700` was zero, and after starting the Sanctuary load the latter reached 2,916 calls (2,096 recorded entry indices before the global 400,000-record bound), with zero unreadable fields and zero dropped records. This proves both answers and title-owner coverage. It does not dynamically bind one entry index to one selected D3D draw; fixed wall-clock input could not reproducibly place the single F9 edge after the load in matched OFF/ON runs, and that remains an explicit blind spot rather than inferred attribution.

2026-08-22 live controls used the untouched configured registry value `1` and a disposable reflink Wine prefix containing a retail-format Sanctuary save. The matched 3D-menu captures and matched saved-game captures both produced `path=none` for DetailedShadow 0 and 1. The gameplay OFF summary recorded 253 draws and the ON summary 256, with zero dropped events, zero dropped resource records, zero texture-hook failures and no selected-frame render-target/copy/update resource events. Every recorded draw retained ZBIAS 0, stencil disabled and the full `0xf` colour-write mask; replaying alpha-blend/Z-write state produced the same route vocabulary on both sides, with only timing-sensitive primitive counts differing. All three shadow probes remained at zero across more than 200,000 recorded calls while other probes accumulated six-figure positive counts. The three-draw difference is not attributable because the F9 frames were selected at different idle-animation instants. These are valid negative captures, not identification of the retail path; the comparator correctly refuses the ON `path=none` result.

## DetailedShadow control workflow

Build two isolated stock run directories:

```sh
python3 tools/build_stocklog.py shadow-off
python3 tools/build_stocklog.py shadow-on
```

There is no visible retail Detailed Shadows control: the setting inventory proved it is hidden backing state. The control therefore uses two independent proxy inputs. At `Direct3DCreate8`, before the later retail display-settings load, `X2_SHADOW_FORCE` makes the proxy validate XMen2.exe's ADVAPI32 `RegQueryValueExA` import and redirect only a successful DWORD query whose value name is exactly `DetailedShadow`. The observer records the original registry result before substitution, the number of successful substituted reads, and the live backing byte after the retail loader stores the result. `X2_SHADOW_EXPECT` is not the force value's alias: it remains the independent assertion checked again at F9. The comparator refuses an unknown seam, zero successful reads, a read-back mismatch or a swapped expected value. The older live-byte write was invalid: a real OFF run proved that `FUN_00619770` later overwrote it at 0x006198c3 (issue #105). The current seam changes neither the configured Wine prefix nor its registry. Reach the same gameplay scene in each run and press F9 once. F9 selects exactly the next D3D8 frame; resource creation is recorded in a bounded catalogue before selection so a render-target texture allocated during level load is not lost.

```sh
X2_SHADOW_FORCE=0 X2_SHADOW_EXPECT=0 tools/run_shim.sh shadow-off 540
X2_SHADOW_FORCE=1 X2_SHADOW_EXPECT=1 tools/run_shim.sh shadow-on 540
python3 tools/shadow_trace_compare.py \
  scratch/run/shadow-off/d3d8_shadow_trace.jsonl \
  scratch/run/shadow-on/d3d8_shadow_trace.jsonl
```

The default bound is 4,096 catalogue records and 4,096 selected-frame events; `X2_SHADOW_MAX_EVENTS=<1..100000>` changes both limits. The comparator refuses any dropped event, texture-hook blind spot, swapped setting, or on frame that reaches none of the three paths. It does not assume that off means no shadows: if DetailedShadow selects a quality tier, planar is a legitimate off answer. Resource records name `CreateTexture` dimensions/levels/usage/format/pool and `GetSurfaceLevel`, default and standalone render/depth targets, `CopyRects`, and `UpdateTexture`. Ordered frame records carry target binds and their default/explicit/unknown source, all transforms and stage-state changes, relevant depth/blend/stencil/colour-write state with D3D8 defaults distinguished from explicit writes, `Clear` rectangles/flags/colour/depth/stencil/result, pixel-shader binds and all four D3D8 draw variants.

## Known failure modes

- F9 is a human-selected scene gate. Two captures from different rooms or animation moments are not comparable merely because both contain one summary.
- Holding F9 does not capture unbounded frames: the edge arms one frame. Release and press again for another capture; the comparator deliberately refuses a file containing more than one summary.
- Textures are not wrapped: the observer patches `GetSurfaceLevel` in each real texture vtable, preserving COM identity through `QueryInterface`, `GetDevice` and device `GetTexture`. The vtable catalogue is bounded to 32 distinct implementations; overflow, an unpatchable vtable or a failure to restore its page protection marks the summary incomplete, and the comparator refuses it.
- The DetailedShadow backing-byte address and registry-query IAT are build-specific. The proxy validates both the executable store instruction and the resolved ADVAPI32 import before recording a control; a different executable is UNKNOWN rather than an RVA guess.
- `path=none` is a valid negative observation but not a shadow implementation answer. The comparator refuses an enabled capture that reaches no engine or title emission probe. Whole-run title counts do not repair a selected frame that captured before the save transition.
