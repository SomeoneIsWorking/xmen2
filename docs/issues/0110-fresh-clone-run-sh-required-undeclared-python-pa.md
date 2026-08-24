---
id: 110
title: Fresh-clone run.sh required undeclared Python packages and Ghidra-generated outputs
status: resolved
symptom: A fresh ./run.sh failed with ModuleNotFoundError for resvg_py or refused because src/recomp was absent and instructed the player to run Ghidra
tags: launcher,fresh-clone,bootstrap,uv,ghidra,resvg
created: 2026-08-24
updated: 2026-08-24
---

Root cause: run.sh owned a second shell implementation, called system python3, left Python requirements undeclared, and had no redistributable analysis metadata from which user-owned instruction bytes could be restored. Resolution: run.sh is a no-argument uv shim; pyproject.toml/uv.lock own Python/CMake/Ninja dependencies; bootstrap.py validates 20 exact PE hashes, clones three pinned shared sources, combines encoding-free committed exports with bytes read from the user files, regenerates all module C, builds, and launches. tools/provision.py is the separate non-launching check. A true cold X2_MAX_FRAMES=10 ./run.sh recreated .venv, shared checkouts, generated C, asset pack and Clang build, presented 18 frames, and exited; CTest passed 100/101 with one explicit optional-fixture skip.
