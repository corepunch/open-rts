# Network play

Build with `make`. All game binaries use the same engine-provided command-line
flow. The host plays as player 1 and relays UDP traffic; every machine runs the
full lock-step simulation. This is a listen server, not a dedicated server.

For Dark Colony, start a two-player game with this verified human-versus-human
map (Pond Thing):

```sh
# Host
build/bin/dark-colony --host --map SCENARIO/MPLAYER/J2PLAY01.MAP

# Other machine: substitute the host's IP address
build/bin/dark-colony --join 192.168.1.10

# Or a second window on the host's machine
build/bin/dark-colony --join 127.0.0.1
```

The host waits for two players by default. Joiners receive their player number,
map path, player count, `ticdup` and `extratics` settings automatically. A match
starts after all expected players connect and validate the loaded world.

For four players, use the verified all-human 4 Kingdoms map:

```sh
build/bin/dark-colony --host --players 4 --map SCENARIO/MPLAYER/J4PLAY01.MAP
```

Each of the other three players runs `--join HOST_IP`. Slots are assigned in
connection order. Dark Colony preserves the scenario team as the network owner;
players 1–4 control teams 0–3. Camera, selection, fog and resources follow the
local player. Combat uses distinct owners and the map's team alliances, so
players 2–4 are not treated as one campaign enemy faction.

The same options work in `build/bin/dark-reign`, `build/bin/7legion` and
`build/bin/kknd`; choose a map appropriate to that game. The executable selects
the game. A client running a different game is rejected.

## Options and requirements

- `--map PATH`: host-selected map relative to each machine's data root.
  `--join` does not accept a separate map. Network paths cannot be absolute or
  contain `..` or backslashes. Map and asset files are **not downloaded**.
- `--data DIRECTORY`: local game installation, which may differ between machines.
  Use identical engine builds and game data on matching platforms.
- `--port PORT`: local UDP port. Hosts default to **5029**; joiners use an
  OS-assigned port, so multiple clients can run on one machine. A custom server
  port goes in the join address, e.g. `--join 192.168.1.10:25029`.
- `--players 2..4`: number of players including the host; host-only.
- `--software`: software renderer. Dark Colony already defaults to it.
- `--dup 1..9`, `--extratic`: Doom input sampling/redundancy options. The host
  distributes them to clients. Default `ticdup` is 1 at 30 simulation Hz.
- `--help`: shared engine usage. Named options can appear before or after the
  existing positional `data-root map sprite` arguments, without defining a
  path twice. `--map` also works for offline play and smoke checks.

Allow inbound UDP on the host port. For Internet play, forward that UDP port to
the host or use a VPN that provides reachability. There is no automatic NAT
traversal, matchmaking, late joining or reconnection. Clients communicate only
with the host; no client port forwarding or peer list is needed.

Escape or closing the window cancels startup. The lobby times out after 60
seconds, and missing gameplay/setup traffic fails after 30 seconds. A departing
client leaves its units idle; remaining players continue. The host must remain
running: leaving ends the hosted game. Debug resource/spawn cheats are disabled.

Choose maps with starting units for every player: the engine reports missing
slots instead of starting an unplayable match or inventing armies. Dark Colony
uses the map's existing money, units, alliances and scripts. Alien production
and campaign-specific victory flows remain incomplete; the two maps above
provide human bases with working production. This does not reproduce DC.EXE's
multiplayer menu, race selection or lobby configuration. Existing synthesized
multiplayer starter units remain as documented in `DC_EXE_FINDINGS.md`.

## Legacy manual peer setup

The original Doom-style `--net` flow remains available for two to four peers.
List every other player in ascending player-number order, omitting yourself:

```sh
build/bin/dark-colony --port 25029 --net 1 127.0.0.1:25030 \
    --map SCENARIO/MPLAYER/J2PLAY01.MAP
build/bin/dark-colony --port 25030 --net 2 127.0.0.1:25029 \
    --map SCENARIO/MPLAYER/J2PLAY01.MAP
```

Here each player supplies the same map and timing settings; `--port` also sets
the default remote port. A subsequent option terminates the peer list. Single
hyphens (`-net`, `-host`, `-join`, `-port`, `-dup`, `-extratic`) and Doom's
`.127.0.0.1` address syntax are accepted by the network parser. `--host`, `--join`
and `--net` are mutually exclusive.

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
- Host/join adds a versioned `ORTS` session envelope in `i_net.c`. Repeated
  join requests are assigned the same slot by source IP and port. Once the
  roster is full, welcome replies distribute the game/map and timing. The
  host keeps replying during level loading and gameplay. Addressed data
  envelopes carry the original Doom tic packets; the host validates their
  sender and relays them to the destination. Joiners accept traffic only
  from the host. `d_net.c` retains its logical peer nodes, command histories,
  retransmissions and simulation ordering. Doom's `D_ArbitrateNetStart`
  similarly lets its key player distribute the map before starting play.
- The consistency sample hashes object IDs, positions, momentum, animation
  state/tics, HP, orders, targets, production, resource amounts and RNG index.
  It excludes pointers, local selection, local fog exploration and render
  caches. This is a divergence detector, not a serialization of all mission
  state or a state-resynchronization mechanism.
- `TryRunTics` returns the available tic count to the SDL driver instead of
  busy-waiting and calling Doom's menu ticker. The driver applies commands,
  runs the existing simulation, increments `gametic`, and calls `NetUpdate`.

This is **not wire-compatible with Doom executables**: RTS orders cannot fit
Doom's FPS movement/button command. It preserves Doom's logical peer lockstep
with an optional host relay, not client/server snapshots, prediction, rollback, late joining,
matchmaking or NAT traversal. The existing pathing and collision code still
uses floats at planar boundaries; cross-architecture floating-point identity
has not been established. Use matching builds/platforms. No claim is made
that different native asset installations will stay synchronized merely
because their initial setup hashes match.

## Verification and headless use

```sh
make test-network
env SDL_VIDEODRIVER=dummy build/bin/dark-colony --net-check 300 \
    --host --map SCENARIO/MPLAYER/J2PLAY01.MAP
env SDL_VIDEODRIVER=dummy build/bin/dark-colony --net-check 300 \
    --join 127.0.0.1
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
Host/join tests additionally verify game rejection, automatic slots, map and
timing distribution, four-player relayed commands, native J4PLAY01 base
ownership and Barracks purchases for all four players, plus lossy session
startup and gameplay. `test_network --hosted` runs just these session tests.

For a headless model client, initialize SDL's timer/events, call `I_InitNetwork`,
then `I_StartNetGame(game_id, map_buffer, capacity)` before loading the model
with the resulting relative map path. For legacy/offline mode the latter is
a no-op. After loading,
compute `G_NetSignature` with the resolved map path, and call `D_CheckNetGame`.
Feed `rts_game_model_command` as usual and pump `rts_game_model_tick`: it builds
and receives commands, waits for missing peers, and advances at most one fixed
tic per call. A successful call while waiting does not imply a tic advanced;
observe `gametic` or model events. Call `D_QuitNetGame` before destroying the
model. Without network initialization the model keeps its existing immediate
command/manual-tick behavior.
