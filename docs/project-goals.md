# Project goals

This document records the durable outcomes that define success for the port. It
does not report implementation progress or choose the next task; factual
capability coverage belongs in `project-state.md`, and atomic work belongs in
`issues/`.

## G001 — A native, buildable X-Men Legends II codebase

**Outcome.** X-Men Legends II runs as a native program built from mechanically
translated C and progressively replaced native source, without executing the
retail x86 code or requiring Wine at runtime.

**Why it matters.** A complete translated baseline makes the game runnable
before every subsystem has been rewritten, while keeping each subsystem
available for deliberate native replacement.

**Success conditions.**

- The PC executable and every shipped engine module needed by the game are
  represented in the native build.
- Unsupported instructions, unresolved calls, and missing modules refuse by
  name instead of silently falling back or producing a smaller program.
- Native replacements can take ownership one subsystem at a time while the
  unreplaced program continues through the mechanically translated path.
- The resulting executable contains no interpreter, JIT, Wine dependency, or
  runtime path that executes the original machine code.

**Constraints.** The 2005 PC release remains the base for this port. Restricted
data, relocation layouts, and other non-code PE content come from the user's
matching copy. Mechanically generated C remains reproducible rather than
hand-maintained.

**Non-goals.** Rewriting the whole game before it can run; changing this port's
base to the Xbox release; treating a hybrid or silent fallback path as the
finished native product.

**Contributing state items.** S001, S002, S012.

## G002 — A faithful, complete play experience

**Outcome.** A player can complete the authored game through the native port
with correct gameplay, rendering, audio, movies, input, saves, menus, and
transitions.

**Why it matters.** Executing every translated instruction or accepting every
draw call is only mechanism coverage. The product succeeds when the experience
those mechanisms produce remains faithful and usable.

**Success conditions.**

- A person can play ordinary game content end to end with physical input,
  including loading, cinematics, conversations, combat, saving, and returning
  through authored menus.
- Visual, audio, timing, and gameplay behavior agree with the retail PC control
  wherever the port intends no deliberate enhancement.
- Every supported render and engine path produces the intended picture rather
  than merely accepting commands without refusal.
- Player-facing enhancements preserve the game's authored content and rules
  unless their changed behavior is an explicit product decision.

**Constraints.** Faithfulness claims require observations against the real game
on real data. A scripted play-through may provide evidence but cannot replace
deterministic gates and measured invariants.

**Non-goals.** A gameplay remake, balance mod, visual redesign, or inferred
substitute for behavior that can be recovered from the shipped program and
assets.

**Contributing state items.** S002, S003, S004, S005, S006, S007, S015.

## G003 — Responsive gameplay and loading

**Outcome.** The native game maintains a defined playable frame-time budget and
loads content without disruptive stalls on a documented reference system.

**Why it matters.** Correct output is not usable when dispatch, host work, or
asset loading prevents responsive play.

**Success conditions.**

- The project defines a reference system and quantitative budgets for gameplay
  frame time, frame-time variance, and visible load stalls.
- Representative menus, cinematics, and gameplay remain within those budgets.
- Profiling can attribute slow intervals to the active guest body, host
  subsystem, synchronization point, or asset operation.
- Performance changes retain equivalent dispatch resolution and observable game
  behavior under differential checks.

**Constraints.** Optimizations must remove measured causes and preserve guest
semantics. Performance is measured in the paced player product as well as in
diagnostic runs.

**Non-goals.** Calling an unbounded diagnostic run playable; hiding waits,
dropping work, or weakening fidelity to satisfy a benchmark.

**Contributing state items.** S010.

## G004 — A coherent native-PC input and settings experience

**Outcome.** Keyboard and controller players can configure, understand, and use
the port through coherent native-PC presentation while the authored retail UI
continues to own the game's own options.

**Why it matters.** A native executable is not a usable PC port if devices,
bindings, prompts, presentation modes, or start policy require maintainer-only
knowledge.

**Success conditions.**

- Physical controllers can attach, detach, reconnect by stable identity, and
  drive the game alongside independently assignable keyboard profiles.
- Controller defaults reflect the recovered console control semantics rather
  than a mapping invented from modern convention.
- Prompts follow the active input source and binding. Port-owned SVG glyphs are
  laid out at the recovered Alchemy text boundary without modifying the game's
  font pixels or baking binding labels into the art.
- A native settings surface owns port-specific presentation, device assignment,
  and binding policy while retaining both authored retail Options flows.
- A player-facing direct-start option, if exposed, defines save-slot and
  difficulty policy while retaining the retail new-game initialization.

**Constraints.** The port uses its own redistributable prompt art; no Xbox asset
is shipped. RmlUi owns only coherent port settings, not a replacement for the
retail interface. Persistent assignment never lets a transient device or reused
slot impersonate another controller.

**Non-goals.** Replacing all retail menus; baking a label per key; distributing
console art; using an Xbox executable merely to obtain a different settings UI.

**Contributing state items.** S004, S006, S007, S008, S009.

## G005 — A lawful, portable fresh-clone experience

**Outcome.** A user with a supported host toolchain and a legally obtained
matching game install can build and launch the intended native product from a
fresh clone through one stable command.

**Why it matters.** The port should be reproducible and approachable without
depending on a maintainer's machine, private analysis checkout, or modified game
directory.

**Success conditions.**

- `./run.sh` with no arguments enters one locked environment, validates the
  supplied game, obtains pinned redistributable dependencies, reconstructs
  generated inputs, builds, and launches the intended product.
- The player path requires no Ghidra, Wine, sibling repository, or system Python
  environment beyond the documented native prerequisites and `uv`.
- Every documented supported platform and compiler receives the same product;
  an unsupported platform or missing package receives an actionable refusal by
  exact cause.
- The user's install is read-only, all generated output stays in the ignored
  workspace, and tracked files contain no machine-specific paths.

**Constraints.** The repository distributes only its own source, notes, and
encoding-free metadata. Game executables, libraries, instruction bytes, art,
audio, and data remain user-supplied and untracked.

**Non-goals.** Redistributing copyrighted game content; modifying the retail
install; making maintainer RE tools player prerequisites; silently launching a
legacy, diagnostic, or incomplete target when the intended product cannot be
built.

**Contributing state items.** S001, S014.

## G006 — Evidence-grounded native engine ownership

**Outcome.** Retained game and Alchemy engine behavior is progressively
reimplemented as clean native subsystems whose ownership, ABI, and fidelity are
recoverable and independently testable.

**Why it matters.** Native replacement is sustainable only when it preserves
the recovered contract, keeps one authority for shared engine behavior, and
leaves evidence strong enough to distinguish a faithful port from a plausible
hack.

**Success conditions.**

- Each native override is grounded in binary or asset evidence, reproduces the
  complete call-site contract, retains a differential super path where
  applicable, and has a test at its shipping boundary.
- Game-specific policy remains in this port while generally reusable Alchemy
  readers, formats, controllers, and engine behavior have one shared authority
  consumed by sibling ports.
- Host, game, and engine responsibilities live in cohesive modules with narrow
  interfaces and mechanically enforced structure limits.
- Claims identify falsifiers, instruments demonstrate both positive and
  negative answers, and the RE dependency chain contains no unacknowledged
  shortcut debt.

**Constraints.** Reverse engineering precedes replacement. Exact module
identity, addresses, return values, stack effects, transforms, timing, and data
ownership are part of the contract. Generated translation output is never the
place for a hand-written fix.

**Non-goals.** Permanent renderer-layer pattern matching for behavior owned by
the engine; duplicate engine implementations in individual game repositories;
magic constants, asset edits, or test-only reimplementations that bypass the
shipping path.

**Contributing state items.** S004, S011, S012, S013.
