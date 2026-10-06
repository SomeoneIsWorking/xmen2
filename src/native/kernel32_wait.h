#ifndef X2_KERNEL32_WAIT_H
#define X2_KERNEL32_WAIT_H

#include "x86rt.h"

/*
 * KERNEL32's blocking waits. WaitForSingleObject and friends are reached
 * through the import table; this header exists so the waits can also be
 * driven directly by a test, and so Sleep -- which is one of them, and the
 * one that dominates a stalled run's wall clock -- has an owning declaration
 * rather than a local extern.
 */
void imp_KERNEL32_Sleep(CPU *C);

/* The guest call sites that called Sleep, by return address, with what each
   asked for. Prints at zeroes: "nothing has called Sleep" is a finding. */
void kernel32_sleep_site_report(void);

#endif
