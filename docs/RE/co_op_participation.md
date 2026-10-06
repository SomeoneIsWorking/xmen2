# Co-op participation and the pause Players page

Device assignment and retail participation are different state. The host's
RmlUi grid decides which input source may control each player. XMen2.exe owns
whether that player is currently in cooperative play.

## Retail owner

`FUN_0048de40` at `0x0048de40` constructs and returns the participation
singleton at `0x0072c4f0`. Its vtable is `0x006898c4`. The relevant methods are:

| Vtable slot | Function | Meaning |
|---|---:|---|
| `+0x10` | `0x0048d260` | query active state for one player |
| `+0x14` | `0x0048d2d0` | request that one player join |
| `+0x18` | `0x0048d330` | request that one player leave |
| `+0x68` | `0x0048ea20` | reconcile requested transitions with game/hero state |

The compact query reads the singleton's per-player active byte after checking
the retail pad-manager player range. The two request methods set mutually
exclusive pending bytes. Reconciliation performs the dependent controller and
hero work; a direct write to the active bytes or count would skip that work.

The pause Players input handler `FUN_005cdb50` uses these same four vtable
slots. Its shipped help in `UI/menus/pda.engb` describes the list as active or
inactive cooperative controllers and prevents toggling the primary controller
or the controller that opened the page. Therefore
`src/native/player_participation.c` invokes only this API. The host never
writes the singleton's flags or count, and the retail pause page cannot drift
when opened because it reads the same owner.

## Host policy

`src/input/player_participation_policy.c` owns the pure transition rules:

- P1 becomes active by default when eligible. Persistent settings require P1
  to have at least one device because a zero-primary-player state is not a
  supported host configuration.
- P2–P4 assignment establishes eligibility only. An assigned inactive player
  remains inactive until a rising Start/Pause action from that assigned source.
- Losing the assignment requests leave. Disconnecting a still-assigned stable
  or session-only controller does not: its identity remains eligible but
  unresolved, and no other controller may roam into the slot.
- Only P1 may own keyboard plus controller. P2–P4 own one effective device,
  keyboard or controller. A process-lifetime transient controller temporarily
  suppresses their persisted source without destroying it.

### Seats are not game players in a network session

The host's grid assigns devices to local **seats**; the retail participation
singleton indexes **game players**. They coincide in single-machine play. In a
LAN session the player manager (`FUN_00551ed0`, vtable `0x0069a134`) maps each
game player to a controller at `+4 + 4*player`, flags remote controllers at
`+0x14 + controller`, and counts local controllers at `+0x30`. Observed: the
host maps `0 1 2 3` and flags controller 1 remote for the joined client; the
client maps `1 0 2 3`, so its seat 0 drives game player 1.

`x2_player_seats_to_players` translates seats through that map before any join
or leave request, and a player whose controller is remote or out of range is
never governed by the host's policy. Without the translation the eligibility
pass evicted every network player on every frame.

The pause Players page is also a writer, so transition-on-assignment-change is
not sufficient: it could join a slot whose assignment remains None. Every safe
input pump queries the retail active state and requests leave for
`active & ~eligible`, then uses retail reconciliation. It never auto-joins an
eligible secondary player and never writes active flags or counts.

Keyboard join edges are resolved through each assigned keyboard profile's
Pause row. One physical key that also names P1's or another eligible profile's
Pause action joins none of the secondary profiles, rather than turning a global
key into another player. Controller join uses the canonical row-17 Start
binding and the exact resolved pad.

The decisions are consumed at the existing once-per-frame keyboard DirectInput
pump. Final DirectInput states record edges after modal/script/physical policy;
the next safe pump invokes the retail API. `tools/x2ctl.py input` reports the
retail joined mask separately from the pad manager's binding/action state.

## Evidence boundary

Static executable structure and shipped menu assets prove ownership and ABI.
Pure and production-seam tests prove assignment exclusivity, default P1,
assigned-without-join, exact-source Start join, continuously enforced None,
disconnected reservation, transient override, and no serialization. A bounded
synthetic-pad run observed retail mask `0x1` before and after assigning P2,
`0x3` only after that exact pad's Start, and `0x1` after clear. The retail pause
Players page opened at `0x3` with P1/P2 green and P3/P4 red. This machine has
no physical gamepad event node, so physical reconnect remains a hardware
validation item. A separate None-assignment falsifier used the retail Players
page to toggle P2: the next safe pump logged `join=0x00 leave=0x02`, and the
live retail query was back to mask `0x1`. That proves continuous enforcement
can show the opposite answer rather than merely replaying the initial leave.
