# Network play

Build with `make`. All game binaries use the same engine-provided command-line
flow. The host plays as player 1 and relays UDP traffic; every machine runs the
full lock-step simulation. This is a listen server, not a dedicated server.

Dark Colony also exposes this flow under **MULTI PLAYER WAR** in its native
main menu. Select **ACT AS SERVER**, enter a session name, choose a map, and
click the third/fourth player-type controls to reserve two to four LAN slots.
**CREATE** advertises the session and waits without blocking the menu; the
button then becomes **READY**. The match starts once every reserved slot has
connected and every player, host included, is ready.

Other players select **CONNECT TO SERVER** to browse LAN games. Select a row
and **JOIN**, or use **ADDRESS** to enter a host IP or `host:port` directly.
**REFRESH** restarts discovery; Escape cancels waiting or returns to the
previous screen. As in DC.EXE, a joiner then enters the host's lobby rather
than the game: the join path `0x405670` calls `0x401210` with a lobby argument,
exactly like the host path `0x405600`, so both reach the shared MULTI lobby
`0x40fb20`. There each player toggles only their own race gadget (DC.EXE sends
proto.c message `'f'`, `0x41e650`); slot types, map and options stay
host-only (`0x410a53` asserts `ss->player_number==0`). The lobby shows the
host's map and the current setup, and marks the joiner's own slot "You".

Each player's **READY** button (control 133) toggles their own ready check;
checks 16–23 show every slot's state. DC.EXE sends these as message `'h'`
(`0x41e3bc`) and refuses changes to a ready slot (`0x410a32`), so a ready
player's race is locked here too. The race and ready flag travel together, so
the race the host launches with is the one its player confirmed. The rule that
the match starts when everyone is ready is engine-defined: DC.EXE's own start
trigger was not traced.

The chat window (control 24) shows the lobby log above the status text; type
in the input line (control 25) and press Enter. As in DC.EXE (message `'e'`,
`0x410d35`), the sender formats the line as `<name>: <text>`. The host keeps
the last 32 lines and relays them to every joiner in order. Host cancellation releases waiting clients, and cancelled
joins release their reserved slot. Browsing sends UDP broadcasts to port 5029;
unanswered offers expire after three seconds. Direct connection works when
broadcasts are unavailable. The host configures the session name, player count,
map, colors, teams and options; each player picks their own race.

For Dark Colony, start a two-player game with a human host and an alien joiner:

```sh
# Host
build/bin/dark-colony --host --map SCENARIO/MPLAYER/D2PLAY01.MAP

# Other machine: substitute the host's IP address
build/bin/dark-colony --join 192.168.1.10

# Or a second window on the host's machine
build/bin/dark-colony --join 127.0.0.1
```

The host waits for two players by default. Joiners receive their player number,
map path, player count, game speed, `ticdup` and `extratics` settings automatically. A match
starts after all expected players connect and validate the loaded world.
The scenario supplies each slot's race. Both factions use the same native
sidebar, with seven building/module entries and nine unit entries each.
For human versus human, choose `SCENARIO/MPLAYER/J2PLAY01.MAP` (Pond Thing).

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

The setup protocol version is 3; the session protocol version is 7. JOIN
retries carry up to 8 opaque bytes of the joiner's own lobby choice
(`I_SetNetChoice`; the host reads it with `I_NetChoice`), the id of the last
chat line received and at most one outgoing chat line with a sequence number.
Until a menu host calls `I_LaunchNetGame`, it answers each JOIN with LOBBY
(slot, roster size, setup, map, the acknowledged chat sequence and the next
log line) instead of WELCOME. A joiner re-sends immediately while lines
remain, so chat arrives in order without a separate retransmit timer. Dark
Colony's 48-byte setup is four bytes per slot (race, type, color, team), eight
option bytes, then each slot's ready flag. Command-line hosts still launch as soon as the
roster is full. WELCOME carries the host's lobby settings as launched and its
game speed. A menu host's lobby does not time out; a joiner's 60-second
timeout counts host silence. Tic commands encode all three fixed-point position
components; setup uses the third for game speed. Dark
Colony purchase reservations, refunds, Build submission, movement modes,
waypoints and pause use the same delayed command path as unit orders. Shared
TC_PATH installs a complete route in one command; it snapshots unit IDs and
route cells when accepted. Its trailer after the unit IDs contains two
big-endian 32-bit values (point count and mode), followed by signed 32-bit
x/y cells. Counts 1..8, modes 0..2 and the complete packet span are validated;
simulation validates every cell before installing any route. Ordinary commands
retain their 32-byte base and used unit IDs. Route cursor, direction and points
participate in the deterministic world checksum; local drafts/saved lists do not.

## Options and requirements

- `--map PATH`: host-selected map relative to each machine's data root.
  `--join` does not accept a separate map. Network paths cannot be absolute or
  contain `..` or backslashes. Map and asset files are **not downloaded**.
- `--data DIRECTORY`: local game installation, which may differ between machines.
  Use identical engine builds and game data on matching platforms.
- `--port PORT`: local UDP port. Hosts default to **5029**; joiners use an
  OS-assigned port, so multiple clients can run on one machine. A custom server
  port goes in the join address, e.g. `--join 192.168.1.10:25029`.
- `--players 2..8`: number of players including the host; host-only.
  Dark Colony's screen still reserves two to four through its player-type
  controls. Warcraft II's lobby starts at the map's person-slot count and
  will not go above that count or this limit.
- `--software`: present the framebuffer through SDL's software renderer.
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

Interactive session rejection, setup mismatch, timeout, host departure and
consistency failure return to the main menu with a dismissible message. The
driver releases the level, thinkers, mission, HUD and asset caches, then closes
the transport and resets it to one offline player. Enter, Escape or a click
dismisses the message without activating a menu item. A new campaign or LAN
session can then start normally. Closing the window still exits the application.
`--net-check` keeps its nonzero exit status on failure for automated checks.

Joiners inherit the server's game speed before loading the level and starting
the tic clock. The host's `--speed` overrides a joiner's local setting.
The default is 100%, including Dark Colony; Makefile launch targets use that
runtime default. The previous 150% note came from stale help text, while
`games/dark-colony/info.c::game_info.game_speed` already specified 100.
Legacy manual `--net` peers still require matching speeds because they do not
use the host/join session handshake.

Choose maps with starting units for every player: the engine reports missing
slots instead of starting an unplayable match or inventing armies. Dark Colony
uses the map's existing money, units, alliances and scripts. Building, module
upgrades and unit training work for both factions; specialist unit abilities,
research, foundation placement and campaign-specific victory flows remain
incomplete. The native network screens, per-player race choice, ready checks
and lobby chat are available; DC.EXE's DirectPlay protocol remains
unimplemented. Existing synthesized
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

- Recoverable network errors return to the main menu at the user's request.
  **Confirmed reference behavior:** Doom's `d_net.c::D_ArbitrateNetStart`
  (lines 493–496) calls `I_Error` on a version mismatch; `CheckAbort`
  (lines 467–468) does the same on Escape during synchronization.
  `GetPackets` (lines 298–299) treats `NCMD_KILL` as fatal, and `g_game.c`
  calls `I_Error` on a consistency failure. `i_system.c::I_Error`
  (lines 162–184) prints to stderr, calls `D_QuitNetGame`, shuts down graphics
  and calls `exit(-1)`. It does not return to a menu. Doom has no matching
  game-speed-percent setting; that validation belongs to open-rts.
  Our control-panel message follows the `m_menu.c::M_StartMessage` /
  `M_StopMessage` lifecycle, copies the text across transport reset, and
  retains the control panel after dismissal.
  No retail-game multiplayer behavior was inferred from this change.
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
  Session version 5 appends one validated speed byte (10..200, offset 619) to
  WELCOME. Joiners apply it through `D_SetGameSpeed` before `D_CheckNetGame`
  initializes the clock. Client preferences therefore cannot reject a valid
  hosted game, while the tic setup still checks agreement.
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
ownership and Barracks purchases for all four players, human/alien base
ownership, completed modules and Trooper/Gray training on D2PLAY01, plus lossy session
startup and gameplay. `test_network --hosted` runs just these session tests.
`test_network --lan` covers broadcast/address discovery, game filtering,
duplicate offers, cancelled join slot release, host cancellation, rehosting,
and asynchronous map/player agreement, followed by 90 synchronized native-map
tics with production through the menu session API.
`build/bin/tests/dark-colony/test_menu` also
selects a discovered session through the native controls and verifies the
handoff to the host's map, recovery to the main page, a cleared offline
transport, visible error text and dismissal without activating a campaign.
`tests/shared/test_simple_menu.c` covers the same recovery for the other games.
Host/join tests start the server at 150% and clients at 70%, then verify every
client inherits 150% and every simulation checksum agrees. The session-only
test also verifies a 35% client inherits a 175% host before loading a level.
Legacy manual peers at 100% and 150% still reject incompatible setup before
advancing a simulation tic. Run all these checks
with `SDL_VIDEODRIVER=dummy`.

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
