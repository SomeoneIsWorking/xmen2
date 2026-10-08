#pragma once

#include <cstdint>

struct X86pCpu;

namespace x2::native {

using BehavedPlayerOwnsContext = int (*)(std::uint32_t context, void *opaque);

enum class BehavedPlayerStep { Refused = -1, None = 0, Ran = 1, Completed = 2 };

/* Read-only selection. Returns -1 for corrupt/unreadable scheduler state,
 * zero when no accepted context is scheduled, and one with `context` filled
 * for the minimum-deadline accepted entry. */
int behaved_player_next_owned(struct X86pCpu *cpu,
                              BehavedPlayerOwnsContext owns, void *opaque,
                              std::uint32_t *context);

/* Resume exactly this scheduled context, independent of its deadline. */
BehavedPlayerStep behaved_player_step_context(struct X86pCpu *cpu,
                                              std::uint32_t context);

/* Resume the earliest scheduled BehavEd context accepted by `owns`, without
 * consulting or changing its guest deadline. Exactly one context is resumed;
 * callers repeat until their authored ownership boundary is complete. */
BehavedPlayerStep behaved_player_step_owned(struct X86pCpu *cpu,
                                            BehavedPlayerOwnsContext owns,
                                            void *opaque);

/* Native thiscall replacement for XMen2.exe FUN_004d9640. */
void override_004d9640(struct X86pCpu *cpu);

} // namespace x2::native
