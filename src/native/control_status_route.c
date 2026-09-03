#include "control_status_route.h"

#include "control.h"
#include "control_status.h"

void control_status_route(int fd, unsigned long requests,
                          unsigned long keys_pressed,
                          unsigned long keys_refused,
                          unsigned long screenshots) {
  char body[4096];
  const size_t size = control_status_format(
      body, sizeof body, requests, keys_pressed, keys_refused, screenshots);
  if (!size) {
    control_reply_text(fd, 500, "Internal Server Error",
                       "live status exceeded its bounded response buffer\n");
    return;
  }
  control_reply_json(fd, 200, "OK", body, size);
}
