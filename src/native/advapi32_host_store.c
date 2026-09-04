#include "x2_log.h"
/*
 * Host-side publication over the ADVAPI32 store: the same values, encoding
 * and durability as a guest RegSetValueExA write, for callers that are not
 * the guest -- at this time exactly one, the boot-time display-mode
 * publisher (display_mode_seed.c). This is NOT a live synthetic view of the
 * registry for host reads; after a publication the value belongs to the game
 * exactly as if it had written it itself.
 */
#include "advapi32.h"
#include "advapi32_internal.h"

#include <stdio.h>
#include <string.h>

/*
 * REG_SZ, strlen bytes, no terminating NUL -- byte for byte what a guest
 * RegSetValueExA stores, so no read path can tell a published value from one
 * the game saved. Durable immediately, for the same reason guest writes are.
 */
int advapi32_host_get_string(const char *path, const char *name, char *out,
                             int cap) {
  RegValue *v;
  advapi32_store_load();
  if (!path || !name || !out || cap <= 0)
    return 0;
  v = advapi32_store_find(path, name);
  if (!v || v->type != 1u /* REG_SZ */ || v->len >= (uint32_t)cap)
    return 0;
  memcpy(out, v->data, v->len);
  out[v->len] = 0;
  return 1;
}

int advapi32_host_set_string(const char *path, const char *name,
                             const char *value) {
  RegValue *v;
  size_t n;
  advapi32_store_load();
  if (!path || !name || !value)
    return 0;
  n = strlen(value);
  if (n >= MAX_DATA_) {
    x2_log_error("advapi32: host publication of \"%s|%s\" is %d bytes "
                 "and this store holds %d -- REFUSED.\n",
                 path, name, (int)n, MAX_DATA_);
    return 0;
  }
  v = advapi32_store_put(path, name);
  if (!v)
    return 0;
  v->type = 1u; /* REG_SZ */
  v->len = (uint32_t)n;
  memcpy(v->data, value, n);
  advapi32_store_note_write();
  advapi32_store_save();
  return 1;
}
