---
id: 151
title: Page command-line runs stayed in a non-presenting phase while plain runs did not
status: investigating
symptom: two browser runs launched with ?arg= presented only 8 frames in 300 s at 614M blocks entered, where two plain runs on the same route reached 405 and 367 frames
tags: web,harness,browser,measurement
created: 2026-09-14
updated: 2026-09-14
---

OBSERVED, mechanism NOT isolated. On the same 300 s driven route (#test-play), two runs whose page URL carried the documented command line (?arg=--set&arg=quantum=N) plateaued at 8 presented frames with 614,615,480 JIT blocks entered, while two runs without args reached 405 and 367 frames. The heartbeat kept reprinting the identical 'frame wall avg ... (of 8 intervals)' line, i.e. no further presentations.

What it is NOT: the quantum. The two arg runs used 4000 and 20000 and were indistinguishable (6700.3 and 6406.1 ms over 8 intervals), so the value passed is not the cause -- that is a clean falsification for issue #150's risk.

What it might be, including the possibility that nothing is wrong: the early phase is slow in every run (plain runs pass through 8886 ms / 5 intervals and 11012 ms / 4 intervals before improving), and a guest sitting on a screen that does not present anything would also show no new frames while still executing. 'No new presentations' is therefore not by itself evidence of a stall, and the alternatives are not yet separated.

Next discriminating steps: (1) read the guest's own phase/scene counters at the plateau rather than inferring from presentations, and compare against a plain run's plateau at the same point; (2) test whether a plain run kept going only because it was further along the same route; (3) check whether the query string changes the service-worker cache key, since the harness unregisters the SW but the URL differs. If a plain run reproduces the same 8-frame plateau under the same conditions, this is a non-finding about the args path.
