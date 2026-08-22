---
id: I065
kind: instrument
status: trusted
created: 2026-08-22
---

## Instrument

automatic input JSONL recording + scratch/run/live.json discovery + tools/x2ctl.py probe

## Validated by

A real headless product run published pid 2804444/port 8420, probe captured a 1440773-byte live PNG and the guest input report, and the JSONL showed both answers: initial no keys, DIK 28 down at frame 273, then released at frame 275. After the exact process stopped, running=false was published and probe refused to treat it as live.

## Known failure modes

(none recorded yet)
