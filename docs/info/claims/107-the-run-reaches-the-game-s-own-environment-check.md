---
id: C107
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,native,host,milestone
reconfirmed: 2026-08-06
---

## Claim

The run reaches the game's own environment check, which correctly reports that DirectX is absent

## Evidence

Implemented ole32 (CoInitialize/CoUninitialize/CoCreateInstance) and USER32's MessageBoxA, LoadIconA and LoadCursorA. The exe makes exactly ONE CoCreateInstance call, for CLSID {A65B8071-3BFE-4213-9A5B-491DA4461CA7} / IID {9C6B4CB0-23F8-49CC-A3ED-45A55000A6D2} -- not Windows classes -- and at 0x00616f8a it does  immediately after, so it checks the HRESULT and branches past the feature on failure. REGDB_E_CLASSNOTREG is returned because it is literally true: this host has no COM registry and nothing implements that interface. The game skipped the feature and continued, which is the observable confirmation. Then MessageBoxA printed the game's own words: 'DirectX not found -- DirectX 9.0c or higher is not installed on this computer. Install it and try again.' from FUN_00617480, a different function from the COM caller, so it is a separate presence check rather than a consequence of the refusal. That message is TRUE: there is no DirectX here and the port has not yet provided the graphics layer that would change the answer. Pairs entered 4189 -> 4207; battery 33/33.

## What would falsify it

MessageBoxA returns IDOK for every prompt, including question styles. Nothing has established that OK is the right answer for any particular dialog -- a Yes/No meaning 'overwrite your save?' would be answered without being asked. The button style is printed so such a case is visible, but no run has yet hit one, so the risk is unmeasured rather than absent.

## Re-confirmed 2026-08-06

CORRECTION to one detail of the original evidence. I wrote that the 'DirectX not found' MessageBox came from FUN_00617480, 'a different function from the COM caller, so it is a separate presence check rather than a consequence of the refusal'. That is WRONG. FUN_00617480 CALLS FUN_00616f50 -- the CoCreateInstance function -- at 0x0061751d, having pushed 9, 0 and 0x63 ('c'), then does  on its result: success writes Settings\\DXChecked=1 and continues, failure shows the message box. So FUN_00616f50 IS the DirectX 9.0c version check, implemented as a COM query, and the refusal is the DIRECT cause of the message. Everything else in the claim stands, and the conclusion is unchanged and now better supported: the message is TRUE, because the object that would report a DirectX version genuinely is not present. Also learned: the check is CACHED -- the game writes Settings\\DXChecked=1 under HKCU\\Software\\Activision\\X-Men Legends 2 after a pass and skips the probe entirely on later runs. That value is a lever, and pulling it today would be exactly the fake issue #18 warns against.
