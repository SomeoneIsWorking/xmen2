---
id: I045
kind: instrument
status: trusted
created: 2026-08-12
---

## Instrument

tools/extract_font_igb.py -- IGB font atlas extractor

## Validated by

NOT VALIDATED -- caught LYING. It writes one PNG per MIP SIZE (<stem>_<w>x<h>.png), so an IGB holding several igImages overwrites them all under the same filenames and emits only the last. Comparing two fonts by their decoded PNGs compares one image out of several: it reported the PC and Xbox HUD fonts byte-identical at every mip level when the FILES differ in 76,452 of 93,288 bytes (82%). Any conclusion from its output about whether two IGBs hold the same art is void until it emits one PNG per image, with the image index in the name and a count printed.

## Known failure modes

(none recorded yet)
