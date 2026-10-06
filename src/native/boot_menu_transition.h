#ifndef X2_BOOT_MENU_TRANSITION_H
#define X2_BOOT_MENU_TRANSITION_H

#include <stdint.h>

struct X86pCpu;

/* Invoke the exact retail handler at the end of menus/intro_normal.py. */
int x2_boot_menu_open(const struct X86pCpu *source, uint32_t exe_base);

#endif
