---
id: 58
title: A FIRST RUN with no stored registry exhausted a 32-entry key table and the game gave up on its settings
status: resolved
symptom: advapi32: all 32 registry key handles are open; the guest is leaking them (RegCloseKey is not being called) -- repeated hundreds of times, and the run never reaches the menu
tags: native,advapi32,registry,first-run
created: 2026-08-12
updated: 2026-08-12
---

## Symptom

With `scratch/saves/registry.txt` deleted -- which is the state of a FRESH
clone, and therefore what a new user gets -- the run fills the log with

    advapi32: all 32 registry key handles are open; the guest is leaking them

and never reaches the main menu. With a stored registry present the same
build runs fine, because the game then walks far fewer keys.

## Cause

`src/native/advapi32.c` held open keys in a fixed 32-entry table and returned
0 from `key_open` when it filled. The game does not call `RegCloseKey` for
most of what it opens -- on Windows that is a leak nobody notices, because
there is no such limit -- so on the first run, where it CREATES the whole
settings tree rather than reading it, it goes past 32 and every subsequent
`RegOpenKeyA` fails. The game reads those failures as "no settings" and stops.

## Fix

The table grows (doubling from 32). The leak is still reported, once, with
the count, because a guest that never closes a key is worth knowing about --
it is just not this host's business to enforce a limit the platform does not
have.

## What this is NOT

It is NOT the cause of issue #54, the intermittent early exit before
`CreateDevice`. That was checked rather than assumed: five failing runs were
grepped and NONE of them had exhausted the key table. The two look alike from
a distance -- both end with the game quitting during settings -- and they are
different bugs.
