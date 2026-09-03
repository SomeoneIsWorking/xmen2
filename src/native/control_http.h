#ifndef X2_CONTROL_HTTP_H
#define X2_CONTROL_HTTP_H

/* The control channel's transport -- see control_http.c. */
#include <stddef.h>

/* Called on the server thread with an accepted connection; it does not close
   it. */
typedef void (*ControlHttpHandler)(int fd);

/* Bind 127.0.0.1:<port> and serve it from a detached thread. Returns 0 having
   already SAID why it could not, so the caller can refuse the run. */
int control_http_listen(int port, ControlHttpHandler handler);

/* One HTTP reply. `control_reply_text` formats its body. */
void control_reply_text(int fd, int code, const char *status, const char *fmt,
                        ...);
void control_reply_json(int fd, int code, const char *status, const char *body,
                        size_t size);
void control_reply_bytes(int fd, int code, const char *status,
                         const char *ctype, const void *body, size_t size);

#endif /* X2_CONTROL_HTTP_H */
