#ifndef X2_DIALOG_SELECTION_SCALE_POLICY_H
#define X2_DIALOG_SELECTION_SCALE_POLICY_H

#include <stdint.h>

/* Exact retail formula observed at XMen2.exe FUN_005ea9e0. */
float x2_dialog_selection_retail_scale(uint32_t output_height);

/* Extends that formula beyond its 800x600 UI reference without allowing the
   original linear approximation to cross zero. */
float x2_dialog_selection_scale(uint32_t output_height);

#endif /* X2_DIALOG_SELECTION_SCALE_POLICY_H */
