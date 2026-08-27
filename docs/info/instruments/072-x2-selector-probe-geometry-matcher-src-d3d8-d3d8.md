---
id: I072
kind: instrument
status: DISTRUSTED
created: 2026-08-27
distrusted_on: 2026-08-27
---

## Instrument

X2_SELECTOR_PROBE geometry matcher (src/d3d8/d3d8_selector_probe.c + tools/selector_probe.py)

## Validated by

A synthetic authored-topology fixture produced match=true and a one-field mutation produced false, but both real visible 1280x720 and absent 3840x2160 dialog captures produced zero exact matches despite I056 proving the submitted selector-class draw exists; this validation failed on real data.

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
  It remains distrusted until those records distinguish the actual horizontal
  selected row from same-texture scene draws.

## DISTRUSTED 2026-08-27

The first synthetic raw-asset/topology matcher did not recognize real captures.
The later six-vertex matcher did fire, but fingerprint-free geometry identified
the main-menu selector behind the modal rather than the dialog row.  Every
state conclusion gated by either matcher is unsupported. I056 remains valid
only for the broad draw-table observation.

> Every result this instrument produced is suspect until it is re-validated.
