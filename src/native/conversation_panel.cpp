#include "conversation_panel.hpp"

#include "guest_call.hpp"
#include "guest_modules.h"
#include "x86rt_native.h"

namespace x2::native::conversation {

bool takes_accept(const CPU &cpu, std::uint32_t self) {
  guest::GuestCall call(cpu);
  const std::uint32_t owner = call.cdecl_call(exe_linked(kGameOwner));
  call.virtual_call(owner, kGameTime);
  const long double now = call.x87_result();
  return now > static_cast<long double>(RDF32(self + kAcceptAfter));
}

} // namespace x2::native::conversation
