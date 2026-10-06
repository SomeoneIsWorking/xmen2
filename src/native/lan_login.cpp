/*
 * LAN play needs no GameSpy login, so the port never attempts one.
 *
 * XMen2.exe's Play Online path logs in to GameSpy's Peer chat service before
 * its lobby (issue #188). Every *.gamespy.com name stopped resolving long ago,
 * so the real connect (0x0062f070) can only fail -- and its failure callback
 * (0x00605d90) raises "Online play is temporarily unavailable" whenever it
 * lands while the online menus are up. Whether it does is a race with the
 * player's own pace, which is why a quick player sometimes got through.
 *
 * Nothing the LAN path uses needs that login. Discovery (the 0x19 query
 * broadcast), the 0x18 reply, the direct join and the game's own UDP session
 * are all CNetPlayManager traffic on the game's socket, and every Peer SDK
 * call made later checks the Peer's connected flag (+0x60) and declines while
 * it is clear. So the connect is answered through the game's own callback
 * with the retail SUCCESS branch -- success set and no nick error, which only
 * moves the online manager to state 5 -- without opening a socket or asking a
 * resolver for a GameSpy name.
 */
#include "stdcall_import.h"
#include "x2_log.h"
#include "x86rt.h"
#include "x86rt_native.h"

enum {
  PEER_CONNECT = 0x0062f070u,
  CONNECT_ARG_PEER = 0u,
  CONNECT_ARG_CALLBACK = 2u,
  CONNECT_ARG_PARAM = 3u,
  CALLBACK_SUCCESS = 1u,
  CALLBACK_NO_NICK_ERROR = 0u
};

static unsigned long g_logins;

/* void peerConnect(PEER, const char *, peerConnectCallback, void *param,
   PEERBool blocking) -- cdecl, as both of its callers clean five arguments. */
static void x2_override_0062f070(CPU *C) {
  const uint32_t peer = A(CONNECT_ARG_PEER);
  const uint32_t callback = A(CONNECT_ARG_CALLBACK);
  const uint32_t param = A(CONNECT_ARG_PARAM);
  if (!g_logins++) {
    x2_log_info("lan login: GameSpy's Peer login is not attempted; the "
                "connect completes through the game's callback 0x%08x as a "
                "success, for LAN play",
                callback);
  }
  if (callback) {
    CPU call = *C;
    call.reg[kX86pEsp] -= 16u;
    WR32(call.reg[kX86pEsp] + 0u, peer);
    WR32(call.reg[kX86pEsp] + 4u, CALLBACK_SUCCESS);
    WR32(call.reg[kX86pEsp] + 8u, CALLBACK_NO_NICK_ERROR);
    WR32(call.reg[kX86pEsp] + 12u, param);
    /* The callback is cdecl; discarding the copy is the caller's cleanup. */
    x86_guest_call_args(&call, callback, 0u);
  }
  C->reg[kX86pEsp] += 4u;
}

__attribute__((constructor)) static void register_lan_login(void) {
  x86_register_override("XMen2.exe", PEER_CONNECT, x2_override_0062f070);
}
