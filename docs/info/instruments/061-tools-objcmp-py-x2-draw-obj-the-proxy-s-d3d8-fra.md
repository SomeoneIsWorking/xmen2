---
id: I061
kind: instrument
status: trusted
created: 2026-08-15
---

## Instrument

tools/objcmp.py + X2_DRAW_OBJ + the proxy's d3d8_frame.obj

## Validated by

Selftest passes on four cases it must separate: identical meshes report 0 differences; a mesh flattened on Z is reported with its 50%-of-size rms and its flatness (0.000 vs 1.000 for the round one); a draw signature present on one side only is reported as unmatched rather than silently paired; an empty file and a missing file are both REFUSED with a non-zero exit rather than reading as 'no differences'. Both dumps walk the draw's declared vertex range (BaseVertexIndex + MinIndex, NumVertices) so neither side compares a different subset of a shared buffer.

## Known failure modes

(none recorded yet)
