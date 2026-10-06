---
id: I059
kind: instrument
status: trusted
created: 2026-08-15
---

## Instrument

tools/lightlog_diff.py + X2_LIGHTLOG (src/d3d8/d3d8_device.c) -- the port's light path in the control's exact format, compared

## Validated by

--selftest runs a pair that MUST differ (a black diffuse that is also never enabled -> exactly 2 differences), a log compared against ITSELF (0 differences), and logs with no SETLIGHT or an unknown record, both of which it must REFUSE. The refusal checks run in forked children and exit without unwinding the parent's temporary-directory owner. The positive fixture includes the proxy's capability and vertex-shader diagnostics in the same stream, proving those named non-light records neither poison nor enter the comparison. On real data it found the port and control byte-identical at the menu (13 indices, same values, same enable pattern), which is the negative it had to be able to give. On 2026-08-21 it parsed the 4,311,945-line cached control log after the proxy vocabulary had grown, then reported scene-dependent differences against a shorter port run rather than false agreement.

## Known failure modes

The proxy's log vocabulary can grow independently of this parser. Before 2026-08-21, later `CAPS`, `CREATEVERTEXSHADER` and `SETVERTEXSHADER` records made the retained control log unusable even though they carry no light state. The parser now has a closed allowlist for known proxy diagnostics and its selftest proves an arbitrary unknown record still refuses. Adding a new proxy record therefore requires an explicit parser decision; it cannot silently disappear.

The comparator is deliberately not frame-locked. A longer run can reach extra scenes, light indices and values, so its differences are defects only after an operator establishes that both logs cover the same scene. The report states this limitation; the 2026-08-21 control-vs-short-port comparison demonstrated it with indices present only in the longer control route.
