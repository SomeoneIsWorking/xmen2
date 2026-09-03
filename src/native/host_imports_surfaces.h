/*
 * The published surfaces, one per DLL this host implements. Each lives in its
 * own host_imports_<dll>.c beside the list that builds it; this header is what
 * makes the set enumerable from one place, so a surface that is written and
 * never registered is a compile error rather than a silent unbound import.
 */
#ifndef HOST_IMPORTS_SURFACES_H
#define HOST_IMPORTS_SURFACES_H

void host_imports_register_d3d8(void);
void host_imports_register_kernel32(void);
void host_imports_register_user32(void);
void host_imports_register_gdi32(void);
void host_imports_register_advapi32(void);
void host_imports_register_winmm(void);
void host_imports_register_msvcr71(void);
void host_imports_register_msvcrt(void);
void host_imports_register_ole32(void);
void host_imports_register_ws2_32(void);
void host_imports_register_dinput8(void);
void host_imports_register_dinput(void);

#endif
