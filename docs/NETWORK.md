# Doom-style network play

Build with `make`. Each game binary supports two to four peers over IPv4 UDP.
Every peer simulates the entire level; only player commands are transmitted.
Use the same engine build, game data, map and `--dup` setting on every machine.
Player numbers are one-based on the command line and map to existing map owners
0–3. Networking does not create starting armies or rewrite a scenario's teams,
alliances, scripts or victory conditions. Choose a map with the owners you need.

For two machines on a LAN (substitute their actual addresses):

```sh
# Machine 1, address 192.168.1.10
build/bin/dark-colony --net 1 192.168.1.11
# Machine 2, address 192.168.1.11
build/bin/dark-colony --net 2 192.168.1.10
```

For two windows on one machine:

```sh
build/bin/dark-colony --port 25029 --net 1 127.0.0.1:25030
build/bin/dark-colony --port 25030 --net 2 127.0.0.1:25029
```

For three or four players, list **every other player in ascending player-number
order**, omitting yourself. Player 2 in a four-player game lists players 1, 3, 4.
UDP port 5029 is the default. `--port` changes the local port and the default
remote port; an explicit `host:port` overrides the remote port. Hostnames and
Doom's `.127.0.0.1` address syntax are accepted. Single-dash Doom switches
(`-net`, `-port`, `-dup`, `-extratic`) also work.

Put the peer list last, or terminate it with another option before map paths:

```sh
build/bin/dark-colony --net 1 192.168.1.11 --software \
    data/DCOLONY SCENARIO/HUMAN/HUMAN02.MAP SPRITES/TROOPER1.SPR
```

`--extratic` repeats one previous command tic in each packet. `--dup 2` through
`--dup 9` samples input less frequently and runs that many simulation tics per
command tic. Group orders and production clicks execute once, like Doom's
special buttons, rather than once per duplicated tic. The existing game clock
remains 30 Hz; changing it to Doom's 35 Hz would change authored animation and
movement timing.

The window remains responsive while waiting for peers. Escape quits and sends
four exit notifications. A missing peer stalls simulation until packets return;
there is no timeout that silently converts a disconnected human into an AI.
Graceful exits let the remaining peers continue. Human-owned units are excluded
from AI orders. Selection, camera, resource display and fog use `consoleplayer`;
selection itself is local and never enters a command checksum. Debug resource
and spawn cheats are disabled in network games.

## Relationship to the reference

The reference is `reference/DOOM/{d_net.c,d_net.h,i_net.c,d_ticcmd.h,g_game.c}`.
Its source provenance and hashes are recorded in [REFERENCES.md](../REFERENCES.md).

The implementation retains:

- `doomcom`/`netbuffer`, `CMD_SEND`/`CMD_GET`, OS transport separation, and the
  local-node rebound packet;
- `gametic`, `maketic`, `nettics`, `localcmds`, `netcmds`, twelve-command rings,
  the five-command lead limit and slowest-node gating;
- low-byte tic numbers and Doom's `ExpandTics` wrap rule;
- missing-tic requests, overlap/duplicate rejection, ten-packet retransmit
  throttling, `extratics`, `ticdup`, key-player clock adaptation and exit/kill
  flags;
- a consistency sample delayed by `BACKUPTICS` command tics, checked before
  executing commands in player-number order.

Concrete adaptations for this engine:

- An RTS `ticcmd_t` carries an order, fixed-point map position, target ID,
  product ID and up to 1024 unit IDs. One group order per command tic preserves
  formations and input order. A bounded 64-order input queue reports overflow
  instead of accepting an order it cannot retain. Expired/dead IDs are ignored;
  object-array indices and other players' selections are never consulted.
- Packets explicitly encode fields in network byte order and contain only the
  used unit IDs. Lengths, counts, command kinds, checksums and source IP **and
  port** are checked before copying commands. This also supports localhost
  peers, which the reference's IP-only matching cannot distinguish.
- Setup exchanges validate player numbers, map contents, game ID, state table,
  initial world checksum, protocol version, player count, tic rate and ticdup.
  Setup replies continue until the remote peer starts sending commands, so
  lost startup packets do not leave a peer behind.
- The consistency sample hashes object IDs, positions, momentum, animation
  state/tics, HP, orders, targets, production, resource amounts and RNG index.
  It excludes pointers, local selection, local fog exploration and render
  caches. This is a divergence detector, not a serialization of all mission
  state or a state-resynchronization mechanism.
- `TryRunTics` returns the available tic count to the SDL driver instead of
  busy-waiting and calling Doom's menu ticker. The driver applies commands,
  runs the existing simulation, increments `gametic`, and calls `NetUpdate`.

This is **not wire-compatible with Doom executables**: RTS orders cannot fit
Doom's FPS movement/button command. It provides Doom's peer-to-peer lockstep
architecture, not client/server snapshots, prediction, rollback, late joining,
matchmaking or NAT traversal. The existing pathing and collision code still
uses floats at planar boundaries; cross-architecture floating-point identity
has not been established. Use matching builds/platforms. No claim is made
that different native asset installations will stay synchronized merely
because their initial setup hashes match.

## Verification and headless use

```sh
make test-network
env SDL_VIDEODRIVER=dummy build/bin/dark-colony --net-check 300 \
    --port 25029 --net 1 127.0.0.1:25030
env SDL_VIDEODRIVER=dummy build/bin/dark-colony --net-check 300 \
    --port 25030 --net 2 127.0.0.1:25029
```

Launch the two `--net-check` commands in separate terminals. They print final
tic/checksum pairs and exit nonzero on a network error or bounded check timeout.
The C test harness forks real UDP peers and verifies every simulation tic:
four peers through two low-byte wraps; dropped, duplicated, reordered and
corrupted packets through a relay; ticdup; incompatible setup; intentional
desynchronization; and graceful exit. It also checks deferred group orders,
selection independence, command ownership, stable IDs and production charging.
The model API test uses a loaded campaign with explicit test units for both
owners, and verifies queued movement through `rts_game_model_command` and
`rts_game_model_tick`.

For a headless model client, call `I_InitNetwork`, load the model normally,
compute `G_NetSignature` with the resolved map path, and call `D_CheckNetGame`.
Feed `rts_game_model_command` as usual and pump `rts_game_model_tick`: it builds
and receives commands, waits for missing peers, and advances at most one fixed
tic per call. A successful call while waiting does not imply a tic advanced;
observe `gametic` or model events. Call `D_QuitNetGame` before destroying the
model. Without network initialization the model keeps its existing immediate
command/manual-tick behavior.
