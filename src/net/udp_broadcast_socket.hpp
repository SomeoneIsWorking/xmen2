#ifndef X2_UDP_BROADCAST_SOCKET_HPP
#define X2_UDP_BROADCAST_SOCKET_HPP

/*
 * One non-blocking UDP socket that sends to and hears from the whole LAN on
 * one port.
 *
 * Several processes on one machine may hold the port at once (SO_REUSEADDR),
 * and every one of them hears each broadcast -- two port instances on one
 * desk see each other exactly as two machines would. Datagrams go to the
 * limited broadcast address, the same destination the game's own server
 * query uses.
 *
 * The browser has no UDP: open() refuses there, by name.
 */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace x2::net {

class BroadcastSocket {
public:
  BroadcastSocket() = default;
  ~BroadcastSocket();
  BroadcastSocket(const BroadcastSocket &) = delete;
  BroadcastSocket &operator=(const BroadcastSocket &) = delete;

  /* Bind `port` on every address. On failure, error() names the step. */
  bool open(uint16_t port);
  bool is_open() const { return fd_ >= 0; }

  /* One datagram to every machine on the LAN. False on a send error. */
  bool broadcast(std::span<const uint8_t> datagram);

  /* The next waiting datagram's size (copied into `buffer`, truncated to
     it), or none when nothing is waiting. A receive error closes nothing and
     is reported through error(). */
  std::optional<size_t> receive(std::span<uint8_t> buffer);

  const std::string &error() const { return error_; }

private:
  void fail(const char *step);

  int fd_ = -1;
  uint16_t port_ = 0u;
  std::string error_;
};

} // namespace x2::net

#endif
