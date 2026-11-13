# 0212 — Guest addresses from the decomp's symbol map

## Goal

The port reads every guest address, structure size and field offset from constants generated
from `external/xmen2-decomp/config/symbols/*.txt`, never a hand copy. A finding is committed to
the decomp first; the port bumps the submodule in the change that uses it.

## Where it stands

- The decomp holds 191 names (XMen2.exe 155, libIGSg 8, libIGMath 6, libIGCore 2, libIGGfx 20),
  harvested from the port's named `constexpr`/`#define` addresses. `tools/verify.py` checks each
  against the user's install.
- About 390 distinct XMen2.exe addresses appear in `src/`; the other ~200 are unnamed literals
  (override registrations `x2_override_XXXXXXXX`, inline call targets, comments).
- Structure sizes and field offsets (`popup_dialog.hpp`, `menu_item_memory.hpp`,
  `conversation_panel.hpp` and others) are not in the map yet.
- Nine addresses the port names twice are listed in the decomp's `docs/our-names.md`.

## Stages

1. **Generator.** One generator in `shared/x86port` reads splat `symbol_addrs` files (name,
   address, `type:func|label`, optional `size:`) into a C++ header of `inline constexpr`
   constants in a title namespace under `build/`, run by CMake when a symbol file changes. It
   refuses a duplicate name or address and a symbol outside its image's sections (the rule
   `tools/verify.py` already checks). Its focused test generates from a small fixture map.
2. **Named addresses.** Replace the hand-written constants with the generated ones, owner by
   owner, highest count first: `popup_touch.cpp`, `extraction_revive.cpp`,
   `conversation_touch.cpp`, `options_menu_no_advanced.cpp`, `team_touch.cpp`, `list_touch.cpp`,
   `hud_minimap.cpp`, `vec3_override.cpp`, then the rest. The DLL overrides (`skin_override.cpp`,
   `ig_matrix*_override.cpp`, `box_cull_override.cpp`, `igvk_*`, `threads_engine_report.cpp`)
   read their image's map. Each step resolves its `our-names.md` entries to the map's name.
3. **Unnamed literals.** Name each remaining address in the decomp, then use the constant;
   override registration takes the generated function constant instead of a literal.
4. **Layouts.** Move structure sizes and field offsets into the decomp (a struct header the
   generator emits as offsets), then drop the port's hand-written memory layouts.

## Done when

`codemap.py find` over `src/` finds no guest address or field offset literal outside generated
code, and the stage-2 owners still pass their live cases (`options-pages`, `touch-menu`,
`pickup-models`).

Until stage 1 lands, a new address is added to the symbol map first and copied by name.
