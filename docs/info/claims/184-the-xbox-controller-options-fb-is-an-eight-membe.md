---
id: C184
kind: claim
status: holds
created: 2026-08-14
tags: xbox,assets,controller,re
---

## Claim

The Xbox controller-options FB is an eight-member record archive, and its controller diagram is a valid embedded IGB

## Evidence

tools/extract_fb.py --selftest passes 6/6 through the shipping parser. Listing scratch/raw/xbox-controller/packages/generated/maps/package/menus/options_controller_xbox.fb reports 8 members over 274512 bytes; --contains xboxcontroller matches exactly one 72990-byte payload, and build/igb_dump parses it as IGB version 6 with 46 objects and one 256x256 RGBA image.

## What would falsify it

A second shipped FB requires a different record layout, or an independent parser shows that any reported member boundary/path/payload is wrong
