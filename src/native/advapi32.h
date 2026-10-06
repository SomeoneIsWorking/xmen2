/* ADVAPI32: one registry read, answered "not found" because that is true.
   See advapi32.c. */
#ifndef X2_ADVAPI32_H
#define X2_ADVAPI32_H

void advapi32_install(void);

/* How many lookups went unanswered, so "nothing asked" and "everything asked
   and got nothing" are distinguishable at exit. */
void advapi32_report(void);

/* Host-side publication into the game's own persistent store -- the same
   values, encoding and persistence as guest RegSetValueExA writes, for the
   one boot-time publisher (display_mode_seed). Not a live view: after these
   run, the value belongs to the game exactly as if it had written it. */
int advapi32_host_get_string(const char *path, const char *name, char *out,
                             int cap);
int advapi32_host_set_string(const char *path, const char *name,
                             const char *value);

#endif /* X2_ADVAPI32_H */
