---
id: I036
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

tools/device_slots.py -- which vtable slots of a renderer class reach the DirectX device fields, plus each slot's RET N

## Validated by

Reproduces the previously hand-generated src/vulkan/igvk_device_slots.h EXACTLY: 98 of 334 slots, and the sorted slot list diffs clean against the committed one. Before this the header was a committed artifact whose generator was never committed, so the number could not be rechecked. The tool prints its denominators (98 of 334 scanned, 0 unscanned) so a negative carries its blind spots, and it names all three ways the answer is a LOWER BOUND: direct calls only, offset-constant matching, and unscanned functions. The RET N it reports for slots 7/8/174/175/177/186/254 was each cross-checked against the disassembled body, and it correctly refuses rather than guessing on slot 8, whose body tail-jumps and has no RET of its own.

## Known failure modes

(none recorded yet)
