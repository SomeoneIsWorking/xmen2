/*
 * The registry behind host_imports.h. See that header for why it exists.
 */
#include "host_imports.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define SURFACE_MAX 16

static struct {
  const char *dll;
  const HostImport *tab;
  size_t n;
} g_surface[SURFACE_MAX];
static unsigned g_nsurface;

void host_imports_register(const char *dll, const HostImport *tab, size_t n) {
  unsigned i;
  if (!dll || !tab || !n) {
    fprintf(stderr,
            "host_imports_register: refusing an empty surface "
            "(dll=%s tab=%p n=%zu)\n",
            dll ? dll : "(null)", (const void *)tab, n);
    abort();
  }
  for (i = 0; i < g_nsurface; i++)
    if (strcasecmp(g_surface[i].dll, dll) == 0) {
      fprintf(stderr,
              "host_imports_register: %s is already published; "
              "one DLL has one table.\n",
              dll);
      abort();
    }
  if (g_nsurface == SURFACE_MAX) {
    fprintf(stderr,
            "host_imports_register: the %d-surface table is full; "
            "%s cannot be published. Raise SURFACE_MAX.\n",
            SURFACE_MAX, dll);
    abort();
  }
  g_surface[g_nsurface].dll = dll;
  g_surface[g_nsurface].tab = tab;
  g_surface[g_nsurface].n = n;
  g_nsurface++;
}

const HostImport *host_import_find(const char *dll, const char *sym,
                                   uint32_t ordinal, const char **dll_name) {
  unsigned i;
  size_t j;
  if (!dll)
    return NULL;
  for (i = 0; i < g_nsurface; i++) {
    if (strcasecmp(g_surface[i].dll, dll) != 0)
      continue;
    for (j = 0; j < g_surface[i].n; j++) {
      const HostImport *e = &g_surface[i].tab[j];
      int hit = sym ? (e->ordinal == 0 && strcmp(e->sym, sym) == 0)
                    : (e->ordinal != 0 && e->ordinal == ordinal);
      if (!hit)
        continue;
      if (dll_name)
        *dll_name = g_surface[i].dll;
      return e;
    }
    return NULL; /* right surface, not implemented */
  }
  return NULL; /* no such surface here */
}

void host_imports_report(unsigned *surfaces, unsigned *entries) {
  unsigned i, n = 0;
  for (i = 0; i < g_nsurface; i++)
    n += (unsigned)g_surface[i].n;
  if (surfaces)
    *surfaces = g_nsurface;
  if (entries)
    *entries = n;
}
