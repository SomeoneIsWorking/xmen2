#ifndef X2_LIVE_SESSION_H
#define X2_LIVE_SESSION_H

/* Publish the current product run for tools/x2ctl.py. */
int live_session_start(int control_port, const char *input_recording);
void live_session_stop(void);

#endif /* X2_LIVE_SESSION_H */
