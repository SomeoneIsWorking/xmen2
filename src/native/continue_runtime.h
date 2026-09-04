#ifndef X2_CONTINUE_RUNTIME_H
#define X2_CONTINUE_RUNTIME_H

struct X86pCpu;

/* Run the authoritative retail mode-3 Continue chain directly from the
   intercepted boot intro command: catalog leaf pickup, save-manager mode 3,
   header/device/file selection, state 0x1c, exact-leaf redirect. Consumes
   the cached boot request on success. Returns 0 unchanged when the retail
   manager refuses anything -- the caller falls back to the retail menu. */
int x2_continue_boot_dispatch(struct X86pCpu *cpu);

#endif /* X2_CONTINUE_RUNTIME_H */
