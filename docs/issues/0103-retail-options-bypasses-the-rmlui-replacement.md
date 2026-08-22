---
id: 103
title: Retail Options bypasses the RmlUi replacement
status: resolved
symptom: Selecting Options from an authored menu still opens the retail options page while RmlUi is reachable only through F1
tags: pc,native,menu,rmlui,options,architecture
created: 2026-08-22
updated: 2026-08-22
---

Root cause: the host overlay and the guest BehavEd command table had no bridge. The shipped main menu emits options_main, registered at XMen2.exe 0x005f4900 to callback 0x005f1fa0; generic and pause entries emit options, registered to 0x005f1c50. Both callbacks still pushed the retail options menu. Resolution: subsystem-owned overrides route only those two callbacks into the shared RmlUi visibility state, leaving the underlying guest menu active. The production-seam test pins both registrations, the plain RET stack effect, and the shared state used by F1 and input capture.
