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

### Note (2026-09-14)
EVIDENCE ADDED, from the guest's own counters rather than from presentations, which excludes the alternative explanation this issue was careful to leave open. At the end of their runs the two arg-carrying runs both sit at exactly scenes 9 (+0), clears 42 (+0), draws 119 (+0), presents 10 (+0) -- identical to each other -- while the two plain runs on the same route reached scenes 112 (+12)/125 (+10) with roughly 1000 draws. Since DRAWS and SCENES also stopped, this is not 'a screen that presents nothing': the title's scene system is not advancing at all, and a static screen would keep presenting. Both arg runs stopping at the SAME numbers means a deterministic stop point, and the page reported 'Starting X-Men Legends II...' throughout, so the guest did begin. The remaining explanation to test is that the page command line changes the boot flow (which boot mode the port enters) rather than breaking anything, and that the harness's fixed driving does not carry an args-launched run past that point. That is a harness/boot-flow question, not evidence of a regression in the title.
