#ifndef DIALOG_PROMPTS_H
#define DIALOG_PROMPTS_H

#include <stdint.h>

/* Pure policy seam for the scoped localization override. */
int dialog_prompts_use_asset_text(int connected_pads,
                                  uint32_t localization_return);

void dialog_prompts_report(void);

#endif /* DIALOG_PROMPTS_H */
