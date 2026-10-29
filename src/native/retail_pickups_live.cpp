#include "retail_pickups.hpp"

#include "guest_modules.h"

namespace x2::native {

PickupTables live_pickup_tables() {
  return {live_entity_tables(), exe_linked(0u)};
}

} // namespace x2::native
