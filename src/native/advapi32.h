/* ADVAPI32: one registry read, answered "not found" because that is true.
   See advapi32.c. */
#ifndef X2_ADVAPI32_H
#define X2_ADVAPI32_H

void advapi32_install(void);

/* How many lookups went unanswered, so "nothing asked" and "everything asked
   and got nothing" are distinguishable at exit. */
void advapi32_report(void);

#endif /* X2_ADVAPI32_H */
