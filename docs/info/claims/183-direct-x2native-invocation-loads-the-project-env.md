---
id: C183
kind: claim
status: holds
created: 2026-08-14
tags: native,launcher,configuration
---

## Claim

Direct x2native invocation loads the project .env without letting an unrelated cwd or the file override explicit launcher variables.

## Evidence

Shipping src/native/env_file.c is linked into x2native. Four CTests exercise file load, explicit-variable preservation, malformed refusal, and a discriminator with conflicting cwd/executable .env files that requires the executable project's value. Full ctest passes 38/38. Real default run logs 0 variables loaded from the repo .env and 3 launcher variables preserved, reaches 15 frames / 30 draws with 0 refused.

## What would falsify it

A direct x2native launch from outside the repo loads a different project's .env, an explicit environment variable is replaced, malformed input is accepted, or the four shipping-parser tests stop covering the linked implementation.
