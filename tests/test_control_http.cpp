/* control_reply_text sends exactly the body it formatted, whatever its size:
   a reply once cut at 1 KiB carried the untruncated length, and the bytes
   after its buffer, to the client. */
#include "control_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { BODY_BYTES = 3000, RECEIVE_BYTES = 8192 };

/* A connected loopback TCP pair: what the control server's clients are. */
static int socket_pair(x2::native::Socket pair[2]) {
  struct sockaddr_in at;
  memset(&at, 0, sizeof at);
  at.sin_family = AF_INET;
  at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  const x2::native::Socket listener =
      x2::native::socket_open(AF_INET, SOCK_STREAM, 0);
  if (x2::native::socket_is_invalid(listener))
    return 0;
  const int listening = x2::native::socket_bind(
                            listener, (struct sockaddr *)&at, sizeof at) == 0 &&
                        x2::native::socket_listen(listener, 1) == 0 &&
                        x2::native::socket_name(listener, &at) == 0;
  pair[1] = listening ? x2::native::socket_open(AF_INET, SOCK_STREAM, 0)
                      : x2::native::kSocketInvalid;
  const int connected = !x2::native::socket_is_invalid(pair[1]) &&
                        x2::native::socket_connect(pair[1], &at) == 0;
  pair[0] = connected ? x2::native::socket_accept(listener)
                      : x2::native::kSocketInvalid;
  x2::native::socket_close(listener);
  return !x2::native::socket_is_invalid(pair[0]);
}

static int check(int condition, const char *what) {
  if (condition)
    return 0;
  fprintf(stderr, "FAIL: %s\n", what);
  return 1;
}

int main(void) {
  char body[BODY_BYTES + 1];
  char *received = static_cast<char *>(calloc(RECEIVE_BYTES, 1));
  size_t got = 0;
  x2::native::Socket pair[2];
  int failures = 0;

  if (!received || !x2::native::socket_startup() || !socket_pair(pair)) {
    fprintf(stderr, "FAIL: no socket pair\n");
    return 1;
  }
  for (int i = 0; i < BODY_BYTES; i++)
    body[i] = static_cast<char>('a' + i % 26);
  body[BODY_BYTES] = 0;
  control_reply_text(pair[0], 200, "OK", "%s", body);
  x2::native::socket_close(pair[0]);
  for (;;) {
    x2::native::SocketSsize k = x2::native::socket_recv(
        pair[1], received + got, RECEIVE_BYTES - 1 - got);
    if (k <= 0)
      break;
    got += static_cast<size_t>(k);
  }
  x2::native::socket_close(pair[1]);

  const char *start = strstr(received, "\r\n\r\n");
  failures += check(strstr(received, "Content-Length: 3000\r\n") != NULL,
                    "the header does not declare the full body");
  failures += check(start != NULL, "the reply has no header terminator");
  if (start) {
    start += 4;
    failures += check(static_cast<size_t>(received + got - start) == BODY_BYTES,
                      "the body is not exactly the formatted text's size");
    failures += check(!memcmp(start, body, BODY_BYTES),
                      "the body differs from the formatted text");
  }
  free(received);
  printf("control http: %d of 4 checks passed\n", 4 - failures);
  return failures != 0;
}
