---
id: C235
kind: claim
status: falsified
created: 2026-08-22
tags: input,cutscene,movie
depends: tools/binding_rows.py#action_rows_from_exe, src/input/player_input.c#publish_player, src/native/input_binding_sets.c#input_binding_sets_for_player, src/native/cutscene_skip_publication.c#cutscene_skip_publication_classify, src/native/cutscene_skip_probe.c#cutscene_skip_probe_report
falsified_on: 2026-08-22
---

## Claim

Retail cutscene skipping is already routed through Pause row 17: FMV action 19 and scripted-cinematic action 20 map there, while the host publishes the applicable Escape or Start binding into every evaluated master, working and menu bank.

## Evidence

tools/binding_rows.py decoded the shipped XMen2.exe keyboard-default constructor at 0x0061b030 and action jump table at 0x00619c40: Pause row 17 = kind/code 0x00010001 (DIK_ESCAPE), actions 19 and 20 = row 17. Focused Clang tests player_input, dinput_system, dinput_pad, xbox_defaults and binding_rows passed on 2026-08-22; player_input exercises the shipping publication-set owner and xbox_defaults table across banks 0/4/12, dinput_system exercises the shipping SDL scancode translator, and dinput_pad drives virtual Start through the shipping SDL-to-DirectInput button map. The production cutscene publication classifier passed 12 checks, including regressions that remove Escape or Start independently from all three banks and require the missing device's count to reach zero while the other remains complete.

## What would falsify it

If the shipped executable no longer maps actions 19 and 20 to row 17 or defaults row 17 to DIK_ESCAPE, or a focused publication test/live probe shows the applicable Escape or Start binding missing from a master, working, or menu bank.

## FALSIFIED 2026-08-22

User clarified the requested target is gameplay-authored in-engine camera/conversation cutscenes. C235 proves only host publication and retail action-row mapping; it does not prove those authored scenes consume action 20 or execute their cleanup/advance path.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
