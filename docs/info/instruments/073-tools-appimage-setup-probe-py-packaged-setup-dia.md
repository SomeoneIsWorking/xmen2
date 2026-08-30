---
id: I073
kind: instrument
status: trusted
created: 2026-08-30
---

## Instrument

tools/appimage_setup_probe.py -- packaged setup dialog/persistence probe

## Validated by

The public v0.1.1 artifact, known to load project .env before --appimage parsing, failed because no Browse selection was persisted; the rebuilt artifact passed prompt, file-selection, and isolated install-path persistence through the same probe.

## Known failure modes

(none recorded yet)
