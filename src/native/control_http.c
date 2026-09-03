/*
 * The control channel's transport: an HTTP/1.1 reply, and a loopback listener
 * that hands each accepted connection to one handler.
 *
 * Split from control.c so that file owns only what the channel is FOR -- the
 * command queue and the routes. Nothing here knows what a route means; this is
 * sockets and framing.
 *
 * It REFUSES loudly rather than returning a server that is not listening: a run
 * that silently failed to bind ignores every command while looking healthy, and
 * that shape has been read as evidence before.
 */
#include "control_http.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void send_all(int fd, const void *p, size_t n) {
  const char *b = (const char *)p;
  while (n) {
    ssize_t k = write(fd, b, n);
    if (k <= 0)
      return;
    b += k;
    n -= (size_t)k;
  }
}

static void reply(int fd, int code, const char *status, const char *ctype,
                  const void *body, size_t n) {
  char head[256];
  int hn = snprintf(head, sizeof head,
                    "HTTP/1.1 %d %s\r\nContent-Type: %s\r\n"
                    "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                    code, status, ctype, n);
  send_all(fd, head, (size_t)hn);
  if (n)
    send_all(fd, body, n);
}

void control_reply_text(int fd, int code, const char *status, const char *fmt,
                        ...) {
  char body[1024];
  int n;
  va_list ap;
  va_start(ap, fmt);
  n = vsnprintf(body, sizeof body, fmt, ap);
  va_end(ap);
  reply(fd, code, status, "text/plain; charset=utf-8", body, (size_t)n);
}

void control_reply_json(int fd, int code, const char *status, const char *body,
                        size_t size) {
  reply(fd, code, status, "application/json", body, size);
}

void control_reply_bytes(int fd, int code, const char *status,
                         const char *ctype, const void *body, size_t size) {
  reply(fd, code, status, ctype, body, size);
}

static ControlHttpHandler g_handler;

static void *server_thread(void *arg) {
  int lfd = (int)(intptr_t)arg;
  for (;;) {
    int fd = accept(lfd, NULL, NULL);
    if (fd < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    g_handler(fd);
    close(fd);
  }
  return NULL;
}

int control_http_listen(int port, ControlHttpHandler handler) {
  struct sockaddr_in a;
  pthread_t th;
  int lfd, on = 1;

  g_handler = handler;
  lfd = socket(AF_INET, SOCK_STREAM, 0);
  if (lfd < 0) {
    fprintf(stderr,
            "control: socket() failed: %s. REFUSING to run without "
            "the control channel that was asked for.\n",
            strerror(errno));
    return 0;
  }
  setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* loopback ONLY */
  a.sin_port = htons((unsigned short)port);
  if (bind(lfd, (struct sockaddr *)&a, sizeof a) < 0 || listen(lfd, 8) < 0) {
    fprintf(stderr,
            "control: cannot listen on 127.0.0.1:%d: %s.\n"
            "REFUSING rather than running deaf -- a run that "
            "silently failed to bind ignores every command while "
            "looking healthy.\n",
            port, strerror(errno));
    return 0;
  }
  pthread_create(&th, NULL, server_thread, (void *)(intptr_t)lfd);
  pthread_detach(th);
  return 1;
}
