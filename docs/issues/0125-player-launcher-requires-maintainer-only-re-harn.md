---
id: 125
title: Player launcher requires maintainer-only re-harness checkout
status: resolved
symptom: ./run.sh validates the game, then refuses because vendor/shared/re-harness is absent or at a different maintainer revision
state_items: S001
tags: launcher,bootstrap,fresh-clone,re-harness
created: 2026-08-27
updated: 2026-08-27
---

## Root cause

Commit `934b2fe6` added `re-harness` to `bootstrap.py`'s `SHARED_REPOS` while
moving project registry tools to their shared authority. That list is the
player provisioner's runtime dependency set, so an agent-tooling dependency
became a mandatory pinned player checkout. This contradicted the README's
explicit contract and made a valid install fail before build or launch when a
maintainer checkout was absent, dirty, or simply newer than the pin.

## What was tried / dead ends

The real shell entry point was run with an intentionally nonexistent compiler
pair so no game could launch. It validated all 20 PE images, then refused the
existing `vendor/shared/re-harness` revision before reaching compiler
selection. This isolates bootstrap dependency validation from the build and
renderer.

## Resolution

Remove `re-harness` from the player `SHARED_REPOS` authority and add a launcher
contract test that fixes the runtime set at exactly `alchemy`, `port-assets`,
and `recomp-x86`. Maintainer shims continue to resolve `re-harness` through
`tools/shared_dir.py`; player bootstrap neither fetches nor validates it.

### Resolution (2026-08-27)
bootstrap.py mistakenly put maintainer-only re-harness in the player SHARED_REPOS list. Removed it, fixed the runtime set at alchemy/port-assets/recomp-x86 in the launcher contract test, and proved real ./run.sh now validates/provisions all inputs and reaches tools/run.py despite the deliberately mismatched vendor re-harness checkout.

The normal CTest suite now applies the same boundary to `project_state`: it
runs when `RE_HARNESS_DIR` provides the maintainer checkout and reports a
concise skip otherwise. The codemap remains a document and manually callable
maintenance tool, not a build test.
