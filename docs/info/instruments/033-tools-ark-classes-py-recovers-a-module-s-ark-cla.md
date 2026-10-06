---
id: I033
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

tools/ark_classes.py -- recovers a module's ARK class graph and its abstract->concrete substitution map

## Validated by

Three independent cross-checks, not plausibility. (1) On libIGGfx it recovers exactly 100 classes from 100 igArkRegister/11 call sites, matching the 100 distinct ^ig[A-Z] strings from an unrelated strings(1) scan. (2) It reads isAbstract from the argument list AND separately tests retrieveVTablePointer==NULL, which docs/RE/ark.md says must agree; across all 874 classes in 15 modules the disagreement count is 0. (3) It REFUSES on libIGCore, which defines igArkRegister rather than importing it, naming the mangled symbol it searched for -- so the negative path is exercised against a real module on every sweep. Unrecovered argument lists are counted and listed individually rather than dropped. Known limits: register-valued arguments are resolved by a 40-instruction backward constant walk (XOR r,r and MOV r,imm only), so a value computed any other way stays a register and is reported as such; it cannot see bindings written by code Ghidra did not decode.

## Known failure modes

(none recorded yet)
