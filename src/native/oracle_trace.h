/* The port half of the oracle comparison. See oracle_trace.c. */
#ifndef ORACLE_TRACE_H
#define ORACLE_TRACE_H

/* Open the stream and check the hooks bound, before the first frame. Arming
   lazily hid a harness that never installed. */
void oracle_probe_arm(void);

/* One line for the heartbeat. Prints the fired/total count even when it is
   zero, because a harness that recorded nothing must not read as agreement. */
int  oracle_probe_line(char *buf, int n);

/* Does each probe's dispatch entry actually reach its __wrap_? Returns the
   number that do NOT. Printed at arm time; a probe that did not bind can
   never fire, and its silence must not read as agreement. */
int  oracle_probe_binding_check(void);

/* Per-probe call counts and the closing stream report. */
void oracle_probe_report(void);

/* Proves a probed call reaches the stream with the guest's own bytes.
   0 on success. */
int  oracle_probe_selftest(void);

#endif /* ORACLE_TRACE_H */
