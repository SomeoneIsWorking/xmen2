#pragma once

#include <cstdint>

namespace x2::native {

/* Pure policy seam for the scoped localization override. */
int dialog_prompts_use_asset_text(int player_uses_gamepad,
                                  uint32_t localization_return);

void dialog_prompts_report(void);

} // namespace x2::native
