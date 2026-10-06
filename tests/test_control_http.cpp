/* control_reply_text sends exactly the body it formatted, whatever its size:
   a reply once cut at 1 KiB carried the untruncated length, and the bytes
   after its buffer, to the client. */
#include "control_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum { BODY_BYTES = 3000, RECEIVE_BYTES = 8192 };

static int check(int condition, const char *what) {
  if (condition)
    return 0;
  fprintf(stderr, "FAIL: %s\n", what);
  return 1;
}

int main(void) {
  char body[BODY_BYTES + 1];
  char *received = calloc(RECEIVE_BYTES, 1);
  size_t got = 0;
  int pair[2];
  int failures = 0;

  if (!received || socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) {
    fprintf(stderr, "FAIL: no socket pair\n");
    return 1;
  }
  for (int i = 0; i < BODY_BYTES; i++)
    body[i] = (char)('a' + i % 26);
  body[BODY_BYTES] = 0;
  control_reply_text(pair[0], 200, "OK", "%s", body);
  close(pair[0]);
  for (;;) {
    ssize_t k = read(pair[1], received + got, RECEIVE_BYTES - 1 - got);
    if (k <= 0)
      break;
    got += (size_t)k;
  }
  close(pair[1]);

  const char *start = strstr(received, "\r\n\r\n");
  failures += check(strstr(received, "Content-Length: 3000\r\n") != NULL,
                    "the header does not declare the full body");
  failures += check(start != NULL, "the reply has no header terminator");
  if (start) {
    start += 4;
    failures += check((size_t)(received + got - start) == BODY_BYTES,
                      "the body is not exactly the formatted text's size");
    failures += check(!memcmp(start, body, BODY_BYTES),
                      "the body differs from the formatted text");
  }
  free(received);
  printf("control http: %d of 4 checks passed\n", 4 - failures);
  return failures != 0;
}
