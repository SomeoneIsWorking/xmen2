# 0212 — Guest addresses from the decomp's symbol map

## Goal

The port reads every guest address, structure size and field offset from constants generated
from `external/xmen2-decomp/config/symbols/*.txt`, never a hand copy. A finding is committed to
the decomp first; the port bumps the submodule in the change that uses it.

## Where it stands

- Stage 1 is built: x86port `tools/symbol_constants.py` and `x86port_symbol_constants()` generate
  `x2::symbols::<image>` into `build/native/x2_symbols/include/` (target `x2_symbols`), checked
  against `config/images.toml` on every build.
- Stage 2's ordered owners use the generated names: `popup_touch.cpp`, `extraction_revive.cpp`,
  `conversation_touch.cpp`, `options_menu_no_advanced.cpp`, `team_touch.cpp`, `list_touch.cpp`,
  `hud_minimap.cpp`, `vec3_override.cpp`. Their map entries were renamed from port-file prefixes
  to guest concepts (`MenuMgr_Get`, `Hud_Get`, `PopupDialog_Draw`, `ListBox_EntryText`, ...).
- The DLL owners read their image's map: `skin_override.cpp`, `ig_matrix_override.cpp`,
  `ig_matrix_invert_override.cpp`, `box_cull_override.cpp`, `threads_engine_report.cpp` and
  `igvk_context.cpp`, `igvk_slots_lifecycle.cpp`, `igvk_slots_frame.cpp`, with the DLL maps renamed
  to guest concepts (`igMatrix44f_invert`, `FrustCull_TestBox`, `igDxVisualContext_setViewport`)
  and igDxVisualContext's construction helpers named.
- XMen2.exe map names that still carry a port-file prefix (`StickAxisOverride_FN_PADS`) are
  used by the remaining exe owners as a literal, `#define` or RVA.
- Structure sizes and field offsets are not in the map yet.

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

A new address is named in the symbol map first and used through the generated constant.
