---
id: 45
title: The run stops at USER32!DrawTextA -- which is the Alchemy report box saying cg.dll would not load
status: open
symptom: x86_missing_import: USER32.dll!DrawTextA, from Gap::Core::igWin32ReportBox::doModal
tags: pc,native,cg,shaders,libIGGfx,user32,root-cause
created: 2026-08-07
updated: 2026-08-07
---

## What the stop actually is

`x86_missing_import: USER32.dll!DrawTextA` aborts the native run. DrawTextA is
NOT the problem. The boundary ring gives the whole chain:

```
igLibraryList::_instantiateFromPool
  igWin32LibraryLoader::load          <- 0x10049c32
    igReportWarning                   <- 0x10068e9f
      igReportHandler::reportVaList
        igOutput::toStandardError
        igWin32ReportBox::doModal     <- 0x1004c1fb
          USER32!DrawTextA            (missing -> abort)
```

The argument watch (I027, extended in this session to decode strings) reads the
arguments directly and settles it with no inference:

```
[ARGS] -> 0x10068da0 Gap::Core::igWin32LibraryLoader::load
[ARGS]      arg[2] 0x00abfdb8 -> "cg.dll"
[ARGS] -> 0x1004c0c0 igReportWarning
[ARGS]      arg[1] -> "Library %s could not be loaded. Check its consistency
                       with the Alchemy dlls in use.  Windows error message: %s"
[ARGS]      arg[2] 0x00abfdb8 -> "cg.dll"
```

## Root cause

`cg.dll` is the NVIDIA Cg runtime, shipped in the install next to `cgD3D8.dll`,
and it is not one of the modules this host recompiles and maps -- so
`LoadLibraryA("cg.dll")` truthfully returns NULL (C112: libIGGfx shades through
Cg and loads it dynamically).

Note there are TWO cg.dll loads in a run and they are not the same thing:

* libIGGfx `initCg` (0x1002fa60) loads it, checks the result, and SKIPS the
  whole Cg setup on failure. The engine is written to run without Cg -- this is
  why LoadLibraryA returns NULL rather than aborting.
* `igLibraryList::_instantiateFromPool` -> `igWin32LibraryLoader::load` does
  NOT tolerate it, and warns through the modal report box. That is this stop.

## What NOT to do

Implementing `DrawTextA` (and `DialogBoxIndirectParamA`, `EndDialog`,
`GetDlgItem`, `SendDlgItemMessageA`, which the same dialog family needs) would
make the report box drawable and then require CHOOSING which button the user
pressed. That is a bandaid on a warning dialog: it dismisses the message
instead of fixing what the message is about, and the button choice would be a
behavioural decision with nothing behind it.

## The real fix

Bring `cg.dll` (and `cgD3D8.dll`, which it pairs with) through the same
pipeline as every other guest module: `tools/ghidra_export.sh`, then
`recomp.py emit`/`native`, then add them to `X2_MODULES`. They are ordinary
x86-32 PEs. `cg.dll` is 864 KB and `cgD3D8.dll` 299 KB, so this is a large but
mechanical piece of work, not a design question.

Known consequence to plan for, not a reason to stop: once Cg loads, cgD3D8 will
compile shaders and hand them to `IDirect3DDevice8::CreateVertexShader` /
`SetPixelShader`, which the host D3D8 does not implement (issue #27). That is
the next real subsystem, and it is on the road to the renderer regardless.

## Unimplemented USER32 imports, for the record

Across all modules, 49 USER32 imports, 9 of them not implemented:
DialogBoxIndirectParamA, DrawTextA, EndDialog, GetDlgItem, GetKeyState,
GetWindowTextA, GetWindowTextLengthA, SendDlgItemMessageA, SendMessageA.
Eight of the nine are the dialog family above. GetKeyState (libIGDisplay) is
unrelated and is real input work.
