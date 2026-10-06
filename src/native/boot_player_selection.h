#ifndef X2_BOOT_PLAYER_SELECTION_H
#define X2_BOOT_PLAYER_SELECTION_H

struct X86pCpu;

/* Supply the retail title-screen player-selection contract when boot skips
   that presentation. This selects the port's primary local player through
   CPadManager's own setter and verifies the manager accepted it. */
int x2_boot_player_select_primary(struct X86pCpu *source,
                                  unsigned primary_player);

#endif
