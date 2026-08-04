---
id: I002
kind: instrument
status: trusted
created: 2026-08-04
---

## Instrument

tools/run_shim.sh -- headless A/B runner for the game with one libIG*.dll swapped

## Validated by

Caught lying twice and fixed both times: (1) it reported 'alive_at_60s=yes' from the wine explorer WRAPPER pid while XMen2.exe had never loaded at all -- now reports game_image_loaded= from the log; (2) its frame check called a run good on a 100%-black frame -- now prints distinct_colors/dominant and flags any frame >99.5% one colour as proving nothing. Positive case confirmed: produces a 1713-colour splash frame from a working run.

## Known failure modes

(none recorded yet)
