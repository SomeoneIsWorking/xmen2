---
id: I007
kind: instrument
status: trusted
created: 2026-08-04
---

## Instrument

tools/run_shim.py (multi-sample, muted) -- headless game runner for A/B

## Validated by

Takes N samples across the run, prints every sample's colour count, and keeps
the most varied as the representative; a genuinely dead run still shows
uniform at every sample and says ALL SAMPLES UNIFORM. Demonstrated on a real
run: samples of 1206 / 588 / 1 colours, where a single sample would have
reported the run as black. Also muted: Wine's audio driver DLLs are disabled
per-run via the environment (never the user's prefix registry) because the
game was playing FMV audio out of the real speakers during headless runs.
Verified silent by watching pactl sink-inputs across a run -- no Wine client
appeared.

## Known failure modes

(none recorded yet)
