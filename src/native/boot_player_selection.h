#pragma once

struct X86pCpu;

namespace x2::native {

/* Supply the retail title-screen player-selection contract when boot skips
   that presentation. This selects the port's primary local player through
   CPadManager's own setter and verifies the manager accepted it. */
int boot_player_select_primary(struct X86pCpu *source, unsigned primary_player);

} // namespace x2::native
