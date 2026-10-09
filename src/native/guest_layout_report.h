/* How far the run reached into each region of guest_layout.h. */
#pragma once

namespace x2::native {

/* One line per region, and one for the space above the layout, so a mapping
   that landed somewhere the layout never placed anything is seen rather than
   folded into a neighbour. */
void guest_layout_report(void);

} // namespace x2::native
