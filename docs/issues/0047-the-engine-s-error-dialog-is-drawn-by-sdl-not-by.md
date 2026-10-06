---
id: 47
title: The engine's error dialog is drawn by SDL, not by USER32's dialog manager
status: resolved
symptom: x86_missing_import: USER32.dll!DrawTextA -- the Alchemy report box (igWin32ReportBox::doModal) needs the whole USER32 dialog family to say anything
tags: pc,native,user32,dialog,sdl,libIGCore,report-box
created: 2026-08-11
updated: 2026-08-11
---

## What the game wants

Two dialogs, and they are the same thing in different clothes:

* `USER32!MessageBoxA` -- used by the exe for its own fatal messages.
* the **Alchemy report box**: `igReportHandler::defaultReportHandler` formats
  `"<SEVERITY>:\n<message>"` and calls `Gap::Core::igWin32ReportBox::doModal`
  (libIGCore `0x10069c70`), which builds a `DLGTEMPLATE` by hand and runs it
  through `DialogBoxIndirectParamA`, measuring the text with `DrawTextA` to
  size the window.

The report box is where the engine says a library would not load, an asset is
missing or an assertion failed -- so it is the dialog every stop in this port
has landed on, and DrawTextA was the abort.

## Why not implement the dialog family

`DialogBoxIndirectParamA`, `DrawTextA`, `GetDlgItem`, `SendDlgItemMessageA` and
`EndDialog` mean a dialog manager and a text renderer, for a template nothing
else in the port uses -- and then the port would still have to CHOOSE which
button was pressed, which is the actual behavioural question.

## What was done

The DIALOG is replaced; the MESSAGE is not. `win32_sdl_dialog()` shows a real
SDL modal and returns one of the ids the caller expects, so neither caller can
tell the difference:

| button | id | what the caller does |
|---|---|---|
| Exit | 3 | `defaultReportHandler` calls `exit(-1)` |
| Debug | 4 | returns 1 |
| Ignore | 5 | returns 0 -- the run continues |
| Ignore, don't tell me again | 6 | returns 2 |

Those ids are READ, not guessed: `igWin32ReportBox::ReportDlgProc`
(`0x10069a30`) handles `WM_COMMAND` for the Ignore button by reading the
checkbox with `BM_GETCHECK` and calling `EndDialog(hwnd, 5 + checked)`, and the
caller's `SUB EAX,3 / DEC / SUB EAX,2` chain at `0x1004c1fb` is what maps them.
SDL's modal has no checkbox, so "Do&n't tell me again" becomes the fourth
button -- the same two outcomes, offered directly.

`MessageBoxA` is now a real dialog too, with the button set decoded from
`uType`'s low nibble (MB_OK / OKCANCEL / YESNO / RETRYCANCEL). A style this
layer does not know ABORTS rather than answering IDOK, because the caller
branches on which button came back.

## The negative

The text always goes to stderr FIRST, before any box is shown, so a dismissed
dialog still leaves the message in the log.

A run with no screen (`--no-window`, or no video driver) cannot show a modal and
must not block on one nobody can click. It answers the caller's stated fallback
-- Ignore for the report box -- and says so, naming the button: "NOT SHOWN:
this run is --no-window. Answering \"Ignore\" -- nobody chose that, this host
did." A log can never read as though a person answered.

### Resolution (2026-08-11)
Verified by 'x2native --dialog-selftest', wired in as the report_box ctest: it drives the override with a synthetic guest frame in a page mapped LOW (a guest pointer is 32 bits and x2native is position-independent, so the address of a host static does not fit in one -- the first version of the test crashed on exactly that), and checks the three things that would be silently wrong: eax is 5 (Ignore) on a run with no screen, esp advanced by 8 (RET 0x4 pops the return address AND the one argument), and the message text reached stderr. The VISIBLE path -- a real SDL modal, on a machine with a display -- is not covered by a test, because a test that opens a modal nobody can click is a test that hangs; it will be exercised the first time a report fires in ./run.sh.
