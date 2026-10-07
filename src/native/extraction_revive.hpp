#ifndef X2_EXTRACTION_REVIVE_HPP
#define X2_EXTRACTION_REVIVE_HPP

/* `gameplay.extraction_revive`: free and paid party revive near an extraction
 * pad. */

#include "guest_memory_view.hpp"
#include "retail_entities.hpp"
#include "x86rt.h"

#include <cstdint>
#include <mutex>
#include <vector>

namespace x2::native {

/* Where every extraction pad of the loaded map stands. */
std::vector<Position> extraction_pad_positions(const GuestMemoryView &memory);

class ExtractionRevive {
public:
  /* The guest input thread, once per poll. */
  void poll(CPU *cpu, double now);
  /* The get-up timer end (0x004220d0), for actors this revived. */
  void get_up_end(CPU *cpu);

private:
  double next_scan_ = 0.0;
  bool inside_ = false;
  bool is_getting_up(std::uint32_t actor);
  void set_getting_up(std::uint32_t actor, bool getting_up);
  void forget_getting_up();

  std::mutex mutex_;
  std::vector<std::uint32_t> getting_up_;
};

/* The process's one instance, polled from the control pump. */
void extraction_revive_poll(CPU *cpu, double now);

} // namespace x2::native

#endif
