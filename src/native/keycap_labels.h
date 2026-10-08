/* Keyboard key labels, lettered at runtime in the shared key typeface. */
#pragma once

#include <cstdint>

struct x2_keycap_art;

namespace x2::native {

/*
 * The game localizes its key names -- the exe's ENTER reaches a prompt as
 * "Enter" in English -- so no label can be drawn ahead of time. Each binding
 * name is lettered once, on first use, in port-assets' key typeface at the
 * keyboard set's label size and baseline, into a cell of the label sheet the
 * GPU prompt pass samples. The name's characters are the game's one-byte text
 * (Latin-1).
 */
inline constexpr unsigned kKeycapLabelSheetW = 1024u;
inline constexpr unsigned kKeycapLabelSheetH = 512u;

/* The label for a binding name, lettering it on first use; NULL, reported,
   when the typeface cannot be opened or the sheet is full. */
const struct x2_keycap_art *keycap_label_art(const uint16_t *name,
                                             unsigned length);

/* The sheet's RGBA bytes. `generation` changes whenever a label is added, so
   the GPU owner uploads only a sheet it has not seen. */
const uint8_t *keycap_label_sheet(uint64_t *generation);

void keycap_labels_report(void);

} // namespace x2::native
