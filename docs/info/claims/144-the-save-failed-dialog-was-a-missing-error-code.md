---
id: C144
kind: claim
status: holds
created: 2026-08-07
tags: pc,native,kernel32,saves
---

## Claim

The 'SAVE FAILED!' dialog was a MISSING ERROR CODE, not a missing feature: CreateDirectoryA returned FALSE for an existing directory without setting ERROR_ALREADY_EXISTS, so the game read 'already there' as 'could not be made'.

## Evidence

X2_FILES=1 traced the sequence: GetFileAttributes on the Save dir (a directory), FindFirstFile saveslot*.save (matched nothing, expected on a first run), then CreateDirectory of each level returning 'File exists'. Every open in the run SUCCEEDED and settings.dat was written, so the failure could not be a file failure. After mapping EEXIST to ERROR_ALREADY_EXISTS the dialog is GONE: scratch/screenshots/after.png is the title screen with its full legal paragraph and no panel. The later libIGGfx x87 stop was independent of this file-error mapping defect.

## What would falsify it

a capture in which the SAVE FAILED panel is back, or an X2_FILES trace showing a CreateDirectory failure that is not EEXIST
