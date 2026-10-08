#include "udp_broadcast_socket.hpp"

#ifndef __EMSCRIPTEN__
#include "platform_socket.h"
#include <cstring>
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

namespace {

static_assert(sizeof(x2::native::Socket) <= sizeof(std::uintptr_t));

x2::native::Socket host_socket(std::uintptr_t stored) {
  return static_cast<x2::native::Socket>(stored);
}

} // namespace

BroadcastSocket::~BroadcastSocket() {
  if (open_) {
    x2::native::socket_close(host_socket(socket_));
  }
}

void BroadcastSocket::fail(const char *step) {
  const int code = x2::native::socket_error();
#if defined(_WIN32)
  error_ = std::string(step) + ": Winsock error " + std::to_string(code);
#else
  error_ = std::string(step) + ": " + std::strerror(code);
#endif
}

bool BroadcastSocket::open(uint16_t port) {
  port_ = port;
  if (!x2::native::socket_startup()) {
    fail("WSAStartup");
    return false;
  }
  const x2::native::Socket socket =
      x2::native::socket_open(AF_INET, SOCK_DGRAM, 0);
  if (x2::native::socket_is_invalid(socket)) {
    fail("socket");
    return false;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  const char *step = nullptr;
  if (x2::native::socket_reuse_address(socket)) {
    step = "SO_REUSEADDR";
  } else if (x2::native::socket_set_int_option(socket, SOL_SOCKET, SO_BROADCAST,
                                               1)) {
    step = "SO_BROADCAST";
  } else if (x2::native::socket_set_nonblocking(socket, 1)) {
    step = "O_NONBLOCK";
  } else if (x2::native::socket_bind(
                 socket, reinterpret_cast<const sockaddr *>(&address),
                 sizeof address)) {
    step = "bind";
  }
  if (step) {
    fail(step);
    x2::native::socket_close(socket);
    return false;
  }
  socket_ = static_cast<std::uintptr_t>(socket);
  open_ = true;
  return true;
}

bool BroadcastSocket::broadcast(std::span<const uint8_t> datagram) {
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(port_);
  to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
  const x2::native::SocketSsize sent = x2::native::socket_sendto(
      host_socket(socket_), datagram.data(), datagram.size(), 0, &to);
  if (sent != static_cast<x2::native::SocketSsize>(datagram.size())) {
    fail("sendto");
    return false;
  }
  return true;
}

std::optional<size_t> BroadcastSocket::receive(std::span<uint8_t> buffer) {
  const x2::native::SocketSsize size = x2::native::socket_recv_datagram(
      host_socket(socket_), buffer.data(), buffer.size());
  if (size >= 0) {
    return static_cast<size_t>(size);
  }
  const int code = x2::native::socket_error();
  if (!x2::native::socket_error_would_block(code) &&
      !x2::native::socket_error_is_interrupt()) {
    fail("recv");
  }
  return std::nullopt;
}

#endif

} // namespace x2::net
