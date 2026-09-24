/* How far the run reached into each region of guest_layout.h. */
#ifndef GUEST_LAYOUT_REPORT_H
#define GUEST_LAYOUT_REPORT_H

/* One line per region, and one for the space above the layout, so a mapping
   that landed somewhere the layout never placed anything is seen rather than
   folded into a neighbour. */
void guest_layout_report(void);

#endif
