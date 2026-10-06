/*
 * The host's Win32/CRT import surface, in one registry.
 *
 * The guest's IAT is bound at load time by name: for each (DLL, symbol) slot
 * the binder asks "does this host implement that?" and, when it does, points
 * the slot at a synthetic address that calls the C function. What the host
 * implements is a property of the host, not of the player's images.
 *
 * So each implemented DLL surface publishes ONE table, in one file, from one
 * macro list -- the same list that declares the stubs, so a symbol that is
 * renamed on one side fails to link rather than silently binding to nothing.
 *
 * An entry is by name or by ordinal, never both: WS2_32 is imported entirely
 * by ordinal, and a slot that says #115 has no name to match.
 */
#ifndef HOST_IMPORTS_H
#define HOST_IMPORTS_H

#include <stddef.h>
#include <stdint.h>

struct X86pCpu;

typedef struct HostImport {
  /* The name the binder matches, for a by-name entry. For a by-ordinal one
     it is a LABEL ("#115") and never matches: a slot imported by ordinal
     carries no name to compare against. `ordinal` non-zero is what makes an
     entry ordinal. */
  const char *sym;
  uint32_t ordinal;
  void (*stub)(struct X86pCpu *);
} HostImport;

/* Publish one DLL's surface. `dll` and `tab` must outlive the process. */
void host_imports_register(const char *dll, const HostImport *tab, size_t n);

/* Every surface this host implements. Called once, before the IAT is bound. */
void host_imports_register_all(void);

/*
 * The entry for one import, or NULL if this host does not implement it.
 * `sym` NULL means look the ordinal up instead. `dll_name` (optional) receives
 * the registry's own spelling of the DLL, which outlives the caller's.
 */
const HostImport *host_import_find(const char *dll, const char *sym,
                                   uint32_t ordinal, const char **dll_name);

/* What is published, for the shutdown report: surfaces and total entries. */
void host_imports_report(unsigned *surfaces, unsigned *entries);

#endif
