#ifndef X2_LIVE_SESSION_H
#define X2_LIVE_SESSION_H

/* Publish the current product run for tools/x2ctl.py. */
int live_session_start(int control_port, const char *input_recording);
void live_session_stop(void);

/* Directory the discovery record is published in, replacing the default
 * scratch/run/. A packaged build must set the OS user-data location: the
 * default is relative to the working directory, which a package does not own
 * and which is read-only on Android. */
void live_session_set_directory(const char *directory);

/* The full path of the discovery record, for diagnostics. */
const char *live_session_record_path(void);

#endif /* X2_LIVE_SESSION_H */
