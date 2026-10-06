/*
 * MSVCRT.dll -- what this host implements of it.
 *
 * What the DLLs import instead of MSVCR71: the same functions under a
 * different DLL name, forwarded one to one by CRT_ALIAS in crt.c.
 *
 * ONE list, three expansions: the declarations, the table, and (for a name
 * this host spells differently from the DLL) the string the binder matches.
 * A stub renamed on one side and not the other fails to link.
 */
#include "host_imports.h"
#include "host_imports_surfaces.h"

#include "x86rt.h"

#define MSVCRT_IMPORTS(X, XN, XO)                                              \
  X(_CIacos)                                                                   \
  X(_CIasin)                                                                   \
  X(_CIfmod)                                                                   \
  X(_CIpow)                                                                    \
  XN("??2@YAPAXI@Z", __2_YAPAXI_Z)                                             \
  XN("??3@YAXPAX@Z", __3_YAXPAX_Z)                                             \
  XN("??_V@YAXPAX@Z", ___V_YAXPAX_Z)                                           \
  X(__dllonexit)                                                               \
  X(_beginthreadex)                                                            \
  X(_endthreadex)                                                              \
  X(_errno)                                                                    \
  X(_exit)                                                                     \
  X(_finite)                                                                   \
  X(_ftol)                                                                     \
  X(_initterm)                                                                 \
  X(_itoa)                                                                     \
  X(_onexit)                                                                   \
  X(_purecall)                                                                 \
  X(_snprintf)                                                                 \
  X(_strdup)                                                                   \
  X(_stricmp)                                                                  \
  X(_strlwr)                                                                   \
  X(_strnicmp)                                                                 \
  X(_strupr)                                                                   \
  X(_vsnprintf)                                                                \
  X(abort)                                                                     \
  X(atan2)                                                                     \
  X(atof)                                                                      \
  X(atoi)                                                                      \
  X(calloc)                                                                    \
  X(ceil)                                                                      \
  X(exit)                                                                      \
  X(exp)                                                                       \
  X(fabs)                                                                      \
  X(fclose)                                                                    \
  X(fflush)                                                                    \
  X(fgetc)                                                                     \
  X(fgets)                                                                     \
  X(floor)                                                                     \
  X(fopen)                                                                     \
  X(fprintf)                                                                   \
  X(fputc)                                                                     \
  X(fputs)                                                                     \
  X(fread)                                                                     \
  X(free)                                                                      \
  X(fscanf)                                                                    \
  X(fseek)                                                                     \
  X(ftell)                                                                     \
  X(fwrite)                                                                    \
  X(getenv)                                                                    \
  X(isalnum)                                                                   \
  X(isalpha)                                                                   \
  X(isdigit)                                                                   \
  X(isspace)                                                                   \
  X(log)                                                                       \
  X(malloc)                                                                    \
  X(memcpy)                                                                    \
  X(memmove)                                                                   \
  X(memset)                                                                    \
  X(pow)                                                                       \
  X(printf)                                                                    \
  X(qsort)                                                                     \
  X(rand)                                                                      \
  X(realloc)                                                                   \
  X(setlocale)                                                                 \
  X(sprintf)                                                                   \
  X(sqrt)                                                                      \
  X(srand)                                                                     \
  X(sscanf)                                                                    \
  X(strcat)                                                                    \
  X(strchr)                                                                    \
  X(strcmp)                                                                    \
  X(strcpy)                                                                    \
  X(strlen)                                                                    \
  X(strncat)                                                                   \
  X(strncmp)                                                                   \
  X(strncpy)                                                                   \
  X(strrchr)                                                                   \
  X(strstr)                                                                    \
  X(strtod)                                                                    \
  X(strtok)                                                                    \
  X(tolower)                                                                   \
  X(ungetc)                                                                    \
  X(vfprintf)                                                                  \
  X(vprintf)                                                                   \
  X(vsprintf)

#define DECL(n) void imp_MSVCRT_##n(CPU *C);
#define DECL_N(s, n) void imp_MSVCRT_##n(CPU *C);
#define DECL_O(o, n) void imp_MSVCRT_##n(CPU *C);
MSVCRT_IMPORTS(DECL, DECL_N, DECL_O)

#define ENTRY(n) {#n, 0, imp_MSVCRT_##n},
#define ENTRY_N(s, n) {s, 0, imp_MSVCRT_##n},
#define ENTRY_O(o, n) {"#" #o, o, imp_MSVCRT_##n},
static const HostImport g_table[] = {MSVCRT_IMPORTS(ENTRY, ENTRY_N, ENTRY_O)};

void host_imports_register_msvcrt(void) {
  host_imports_register("MSVCRT.dll", g_table,
                        sizeof g_table / sizeof g_table[0]);
}
