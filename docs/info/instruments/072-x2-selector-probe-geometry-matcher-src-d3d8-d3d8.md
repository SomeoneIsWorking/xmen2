---
id: I072
kind: instrument
status: trusted
created: 2026-08-27
distrusted_on: 2026-08-27
revalidated_on: 2026-08-28
---

## Instrument

`X2_SELECTOR_PROBE` draw-class and transform-provenance recorder
(`src/d3d8/d3d8_selector_probe*.c` + `tools/selector_probe.py`).

## Validated by

The synthetic writer/parser exercises both accepted and refused lowering
results. Version 13 then showed the other answer on real data: the exact
untextured eight-primitive FVF `0x42` row measured 20.04 pixels high at 800x600
and only ~14 pixels high at 3840x2160 before the repair. After the title-scale
repair, the same 4K class measures 72.14 pixels while the 800x600 control
remains 20.04 pixels. Every recorded build request has a paired result in all
three 15/15 live cases.

The provenance chain independently reached `libIGMath!igMatrix44f::multiply`,
`libIGSg!igTransform::setMatrix`, title builder `FUN_005707d0`, and exact caller
`0x005ead9b`; its captured supplied scales match the PE formula at 600, 720,
and 2160 lines. This revalidation distinguishes the actual row, both geometry
outcomes, and the recovered cause; C275 records the resulting claim.

## Known failure modes

- The v3 six-vertex geometry signature reached a real draw, but that draw was
  the main-menu NEW GAME selector behind the modal.  Its 800x600 bounds were
  `x=44.14..270.31, y=199.22..227.34`; the visible dialog row was elsewhere.
  State mutations against that candidate therefore changed the wrong scene
  instance and cannot answer why the dialog row is absent.
- `selected.png` is 608x28 on disk, but the observed runtime resources are
  three 128x32 DXT3 uploads with fingerprint `a564975d5815f611`.  Matching the
  source-file dimensions at the D3D boundary is a guaranteed no-reach query.
- Version 5 replaces the topology/asset guess with a mandatory runtime
  `X2_SELECTOR_TEXTURE=WxH` filter, committed-byte fingerprints, lossless
  geometry denominators, and paired pre-build accepted/refused records.  Its
  synthetic writer/parser has shown both answers. A corrected fresh-profile
  3840x2160 dialog run produced 9,722 requests with a result for every request
  and runtime fingerprint `a564975d5815f611`, proving the live writer path.
  These versions remain invalid for selected-row identity.
- Version 13's `untextured:N` mode identifies the row by its observed D3D draw
  class, keeps complete geometry denominators, and records the matrix ancestry
  responsible for the output.

## DISTRUSTED 2026-08-27

The first synthetic raw-asset/topology matcher did not recognize real captures.
The later six-vertex matcher did fire, but fingerprint-free geometry identified
the main-menu selector behind the modal rather than the dialog row.  Every
state conclusion gated by either matcher is unsupported. I056 remains valid
only for the broad draw-table observation.

The pre-v13 texture/topology conclusions remain suspect and must not be reused.
Only v13 records satisfying the strict parser and paired-result checks are
trusted.
