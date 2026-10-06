#ifndef X2_STICK_AXIS_OVERRIDE_H
#define X2_STICK_AXIS_OVERRIDE_H

/* The native owner of the gameplay stick axes' threshold (retail 0x0061a4c0);
   see stick_axis_override.c. */

/* One line: how many axis resolutions ran natively, how many of those carried
   a value retail's per-axis 0.75 would have dropped, and how many button and
   trigger resolutions ran the retail body. */
void x2_stick_axis_report(void);

#endif
