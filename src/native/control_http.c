#include "x2_log.h"
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

#include "platform_socket.h"
#include "platform_threads.h"
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void send_all(x2_socket_t socket, const void *p, size_t n) {
  const char *b = (const char *)p;
  while (n) {
    x2_socket_ssize_t k = x2_socket_send(socket, b, n);
    if (k <= 0)
      return;
    b += k;
    n -= (size_t)k;
  }
}

static void reply(x2_socket_t socket, int code, const char *status,
                  const char *ctype, const void *body, size_t n) {
  char head[256];
  int hn = snprintf(head, sizeof head,
                    "HTTP/1.1 %d %s\r\nContent-Type: %s\r\n"
                    "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                    code, status, ctype, n);
  send_all(socket, head, (size_t)hn);
  if (n)
    send_all(socket, body, n);
}

void control_reply_text(x2_socket_t socket, int code, const char *status,
                        const char *fmt, ...) {
  char body[1024];
  int n;
  va_list ap;
  va_start(ap, fmt);
  n = vsnprintf(body, sizeof body, fmt, ap);
  va_end(ap);
  reply(socket, code, status, "text/plain; charset=utf-8", body, (size_t)n);
}

void control_reply_json(x2_socket_t socket, int code, const char *status,
                        const char *body, size_t size) {
  reply(socket, code, status, "application/json", body, size);
}

void control_reply_bytes(x2_socket_t socket, int code, const char *status,
                         const char *ctype, const void *body, size_t size) {
  reply(socket, code, status, ctype, body, size);
}

static ControlHttpHandler g_handler;

static void *server_thread(void *arg) {
  x2_socket_t lfd = (x2_socket_t)(intptr_t)arg;
  for (;;) {
    x2_socket_t socket = x2_socket_accept(lfd);
    if (x2_socket_is_invalid(socket)) {
      if (x2_socket_error_is_interrupt())
        continue;
      break;
    }
    g_handler(socket);
    x2_socket_close(socket);
  }
  return NULL;
}

int control_http_listen(int port, ControlHttpHandler handler) {
  struct sockaddr_in a;
  pthread_t th;
  x2_socket_t lfd;

  g_handler = handler;
  if (!x2_socket_startup()) {
    x2_log_error("control: socket runtime startup failed. REFUSING to run "
                 "without the control channel that was asked for.\n");
    return 0;
  }
  lfd = x2_socket_open(AF_INET, SOCK_STREAM, 0);
  if (x2_socket_is_invalid(lfd)) {
    x2_log_error("control: socket() failed: %s. REFUSING to run without "
                 "the control channel that was asked for.\n",
                 strerror(errno));
    return 0;
  }
  x2_socket_reuse_address(lfd);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* loopback ONLY */
  a.sin_port = htons((unsigned short)port);
  if (x2_socket_bind(lfd, (struct sockaddr *)&a, sizeof a) < 0 ||
      x2_socket_listen(lfd, 8) < 0) {
    x2_log_error("control: cannot listen on 127.0.0.1:%d: %s.\n"
                 "REFUSING rather than running deaf -- a run that "
                 "silently failed to bind ignores every command while "
                 "looking healthy.\n",
                 port, strerror(errno));
    x2_socket_close(lfd);
    return 0;
  }
  pthread_create(&th, NULL, server_thread, (void *)(intptr_t)lfd);
  pthread_detach(th);
  return 1;
}
