#include "udp_broadcast_socket.hpp"

#ifndef __EMSCRIPTEN__
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace x2::net {

#ifdef __EMSCRIPTEN__

BroadcastSocket::~BroadcastSocket() = default;

bool BroadcastSocket::open(uint16_t port) {
  port_ = port;
  error_ = "a browser page cannot open a UDP socket";
  return false;
}

bool BroadcastSocket::broadcast(std::span<const uint8_t>) { return false; }

std::optional<size_t> BroadcastSocket::receive(std::span<uint8_t>) {
  return std::nullopt;
}

void BroadcastSocket::fail(const char *step) { error_ = step; }

#else

BroadcastSocket::~BroadcastSocket() {
  if (fd_ >= 0) {
    ::close(fd_);
  }
}

void BroadcastSocket::fail(const char *step) {
  error_ = std::string(step) + ": " + std::strerror(errno);
}

bool BroadcastSocket::open(uint16_t port) {
  port_ = port;
  const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    fail("socket");
    return false;
  }
  const int enabled = 1;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  const int flags = ::fcntl(fd, F_GETFL, 0);
  const char *step = nullptr;
  if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof enabled)) {
    step = "SO_REUSEADDR";
  } else if (::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &enabled,
                          sizeof enabled)) {
    step = "SO_BROADCAST";
  } else if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
    step = "O_NONBLOCK";
  } else if (::bind(fd, reinterpret_cast<const sockaddr *>(&address),
                    sizeof address)) {
    step = "bind";
  }
  if (step) {
    fail(step);
    ::close(fd);
    return false;
  }
  fd_ = fd;
  return true;
}

bool BroadcastSocket::broadcast(std::span<const uint8_t> datagram) {
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(port_);
  to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
  const ssize_t sent =
      ::sendto(fd_, datagram.data(), datagram.size(), 0,
               reinterpret_cast<const sockaddr *>(&to), sizeof to);
  if (sent != static_cast<ssize_t>(datagram.size())) {
    fail("sendto");
    return false;
  }
  return true;
}

std::optional<size_t> BroadcastSocket::receive(std::span<uint8_t> buffer) {
  const ssize_t size = ::recv(fd_, buffer.data(), buffer.size(), MSG_TRUNC);
  if (size >= 0) {
    return static_cast<size_t>(size);
  }
  if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
    fail("recv");
  }
  return std::nullopt;
}

#endif

} // namespace x2::net
