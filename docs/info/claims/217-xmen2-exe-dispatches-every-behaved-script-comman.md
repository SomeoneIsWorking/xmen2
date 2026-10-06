---
id: C217
kind: claim
status: holds
created: 2026-08-19
tags: scripts,re
depends: tools/script_commands.py, src/native/script_trace.c
---

## Claim

XMen2.exe dispatches every BehavEd script command through a 289-entry table at 0x0068a908 whose entries are { handler, name, returnType, argSpec }. Splitting those four fields one dword later pairs each name with the PREVIOUS command's handler, which reads plausibly and makes every override registered from it silently dead.

## Evidence

Read with tools/script_commands.py and checked two ways that are not the tool marking its own work: overrides registered on the handlers it reports for lockControls (0x0049f8c0) and startConversation (0x004a5660) fire exactly where the shipped scripts call them -- lockControls once at tutorial1.py's level entry, startConversation at both tutorial conversations with matching conversation-manager flag transitions -- while overrides on the shifted-layout addresses (0x004a7220, 0x004a57b0) fired 0 times in a run where the controls were demonstrably locked. The argument specs also match the shipped scripts' arity (lockControls f, act aa, playanim sass); read one dword out they come back empty. ctest script_commands pins both.

## What would falsify it

if a command's handler read at this layout is ever observed not to run when a script calls that command
