---
id: 204
title: LAN play with friends anywhere, by a pasted code over Lucent's transport
status: in-progress
state_items: [S023]
tags: lan,multiplayer,online,zerotier,netcode
created: 2026-11-11
updated: 2026-11-12
---

## The request

The coordinator relayed the user's direction: xmen2 keeps its own LAN netcode,
consumes Lucent's online transport, and replaces guest broadcast discovery
with unicast to friends added by code; then the netcode is measured over a
relayed ZeroTier path (input lag, rubber-banding, drop-in) in S023. Every way
through runs inside the game process, with no install, root, VPN permission
or account.

## Shape

A friend is one `lucent::p2p::Connection`: a code pasted on one machine and
accepted on the other, carried over direct UDP or the embedded ZeroTier node,
with session keys from the code's secret.

`x2::net::Friends` (`src/net/friends.{hpp,cpp}`) runs one meeting at a time
and adds each connected friend to a `lucent::p2p::VirtualLan`, which makes the
friend look like a machine on the guest's LAN:

- Each friend has a guest-visible IPv4 address, 10.254.0.2 upward.
- A guest datagram to that address is sealed with its ports and sent over the
  path. The LAN seals under the link purpose and sends a keepalive each
  second, which completes the host's meeting and holds NAT mappings open.
- A datagram from a friend reaches the guest socket on its destination port
  from a loopback proxy kept per friend and source port. `winsock_recvfrom`
  rewrites that source to the friend's address and port, so the guest replies
  to the friend. Readiness, blocking and `select` stay those of the host
  socket.
- A broadcast also goes to every friend, so the game's LAN query and the
  port's presence beacon (UDP 5166) reach them. The Join row and the
  re-formed drop-in session then work as on a LAN.

Winsock and the presence socket reach the friends through
`x2::net::FriendRoute` (`src/net/friend_route.hpp`), so neither links
`lucent::online`. A browser page builds without it and has no friends.

## Stages

1. Done: the friend LAN and the Winsock seam, in Lucent's `VirtualLan`.
2. Done: friend codes in the product. The online lobby's Your code copies
   this machine's code to the clipboard (making one first) and Join by code
   pastes the clipboard's, by pad, keys, mouse or touch (S026). The control
   channel keeps `/lan?invite=1` and `/lan?code=<code>` for tools.
3. Done: the ZeroTier node (`lucent::zerotier`) in `x2::net::Friends`, started
   the first time the lobby's friend rows are shown, its identity in
   `<save root>/zerotier`, and handed to every meeting's connector.
   `lan.zerotier_roots` names a signed root-set file for a lab; empty uses
   ZeroTier's public roots. libzt comes from `tools/libzt_deps.py` for every
   platform (S023 has the relayed two-player run).
4. Done: input lag, correction and drop-in time over the lab relay, the
   public roots and the direct path, recorded in S023.

## Known gaps

- The pairwise model covers two players at once. With three or more, the
  game's NAT negotiation pairs clients by the addresses the host relays, and
  those are each machine's own numbering of its friends.
- Android: the node must stop when the session leaves the foreground.
