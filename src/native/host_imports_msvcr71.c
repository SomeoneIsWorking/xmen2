/*
 * MSVCR71.dll -- what this host implements of it.
 *
 * The C runtime XMen2.exe imports, on libc (crt.c, crt_setjmp.c).
 *
 * ONE list, three expansions: the declarations, the table, and (for a name
 * this host spells differently from the DLL) the string the binder matches.
 * A stub renamed on one side and not the other fails to link.
 */
#include "host_imports.h"
#include "host_imports_surfaces.h"

#include "x86rt.h"

#define MSVCR71_IMPORTS(X, XN, XO)                                             \
  X(_CIacos)                                                                   \
  X(_CIasin)                                                                   \
  X(_CIfmod)                                                                   \
  X(_CIpow)                                                                    \
  X(__RTDynamicCast)                                                           \
  X(__dllonexit)                                                               \
  X(__p__commode)                                                              \
  X(__p__fmode)                                                                \
  X(__security_error_handler)                                                  \
  X(__set_app_type)                                                            \
  X(__setusermatherr)                                                          \
  X(_amsg_exit)                                                                \
  X(_beginthreadex)                                                            \
  X(_callnewh)                                                                 \
  X(_cexit)                                                                    \
  X(_controlfp)                                                                \
  X(_endthreadex)                                                              \
  X(_errno)                                                                    \
  X(_exit)                                                                     \
  X(_finite)                                                                   \
  X(_ftol)                                                                     \
  X(_initterm)                                                                 \
  X(_ismbblead)                                                                \
  X(_mkdir)                                                                    \
  X(_onexit)                                                                   \
  X(_purecall)                                                                 \
  X(_setjmp3)                                                                  \
  X(_snprintf)                                                                 \
  X(_strcmpi)                                                                  \
  X(_strdup)                                                                   \
  X(_stricmp)                                                                  \
  X(_strlwr)                                                                   \
  X(_strnset)                                                                  \
  X(_strupr)                                                                   \
  X(_vsnprintf)                                                                \
  X(abort)                                                                     \
  X(atan2)                                                                     \
  X(atof)                                                                      \
  X(atoi)                                                                      \
  X(calloc)                                                                    \
  X(ceil)                                                                      \
  X(clock)                                                                     \
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
  X(fread)                                                                     \
  X(free)                                                                      \
  X(fscanf)                                                                    \
  X(fseek)                                                                     \
  X(ftell)                                                                     \
  X(fwrite)                                                                    \
  X(getenv)                                                                    \
  X(isalnum)                                                                   \
  X(isalpha)                                                                   \
  X(islower)                                                                   \
  X(ispunct)                                                                   \
  X(isspace)                                                                   \
  X(log)                                                                       \
  X(longjmp)                                                                   \
  X(malloc)                                                                    \
  X(memcpy)                                                                    \
  X(memmove)                                                                   \
  X(pow)                                                                       \
  X(printf)                                                                    \
  X(qsort)                                                                     \
  X(rand)                                                                      \
  X(realloc)                                                                   \
  X(setlocale)                                                                 \
  X(sprintf)                                                                   \
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
  X(strtod)                                                                    \
  X(strtok)                                                                    \
  X(time)                                                                      \
  X(tolower)                                                                   \
  X(toupper)                                                                   \
  X(vfprintf)                                                                  \
  X(vprintf)                                                                   \
  X(vsprintf)

#define DECL(n) void imp_MSVCR71_##n(CPU *C);
#define DECL_N(s, n) void imp_MSVCR71_##n(CPU *C);
#define DECL_O(o, n) void imp_MSVCR71_##n(CPU *C);
MSVCR71_IMPORTS(DECL, DECL_N, DECL_O)

#define ENTRY(n) {#n, 0, imp_MSVCR71_##n},
#define ENTRY_N(s, n) {s, 0, imp_MSVCR71_##n},
#define ENTRY_O(o, n) {"#" #o, o, imp_MSVCR71_##n},
static const HostImport g_table[] = {MSVCR71_IMPORTS(ENTRY, ENTRY_N, ENTRY_O)};

void host_imports_register_msvcr71(void) {
  host_imports_register("MSVCR71.dll", g_table,
                        sizeof g_table / sizeof g_table[0]);
}
