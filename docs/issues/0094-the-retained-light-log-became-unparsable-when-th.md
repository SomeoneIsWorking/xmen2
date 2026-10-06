---
id: 94
title: The retained light log became unparsable when the D3D8 proxy added non-light diagnostics
status: resolved
symptom: lightlog_diff refuses a real control log at CAPS even though SETLIGHT records are present
tags: tooling,rendering,lighting,instrument,parser
created: 2026-08-21
updated: 2026-08-21
---

Root cause: the proxy and comparator share one stream, but the comparator's closed vocabulary was not extended when the proxy gained CAPS and vertex-shader diagnostics. The parser now explicitly ignores only those named non-light record kinds and still refuses arbitrary unknown records. The selftest carries known diagnostics through the real parser and proves an unknown MYSTERY record refuses. Its forked refusal checks now exit without Python unwinding, because unwinding in the child deleted the parent-owned TemporaryDirectory and made the second negative test fail for the wrong reason. I059 and docs/codemap.md record the boundary.
