/* Keyboard key labels, lettered at runtime in the shared key typeface. */
#ifndef X2_KEYCAP_LABELS_H
#define X2_KEYCAP_LABELS_H

#include <stdint.h>

struct x2_keycap_art;

/*
 * The game localizes its key names -- the exe's ENTER reaches a prompt as
 * "Enter" in English -- so no label can be drawn ahead of time. Each binding
 * name is lettered once, on first use, in port-assets' key typeface at the
 * keyboard set's label size and baseline, into a cell of the label sheet the
 * GPU prompt pass samples. The name's characters are the game's one-byte text
 * (Latin-1).
 */
#define X2_KEYCAP_LABEL_SHEET_W 1024u
#define X2_KEYCAP_LABEL_SHEET_H 512u

/* The label for a binding name, lettering it on first use; NULL, reported,
   when the typeface cannot be opened or the sheet is full. */
const struct x2_keycap_art *x2_keycap_label_art(const uint16_t *name,
                                                unsigned length);

/* The sheet's RGBA bytes. `generation` changes whenever a label is added, so
   the GPU owner uploads only a sheet it has not seen. */
const uint8_t *x2_keycap_label_sheet(uint64_t *generation);

void x2_keycap_labels_report(void);

#endif
