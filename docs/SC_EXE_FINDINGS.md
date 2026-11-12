# StarCraft native data and UI findings

## Cloaked Ghost acquisition

Pinned OpenBW `unit_target_acquisition_range` returns zero for a cloaked
Ghost unless its order is HoldPosition. `unit_is_ghost` includes Sarah
Kerrigan and the Brood War Ghost heroes; this classic catalog names the
Ghost and Sarah Kerrigan. `order_PlayerGuard` handles an existing
auto-target before scanning acquisition range, so explicit and retaliatory
targets remain usable. `play/p_mobj.c` suppresses only the new-target scan
for these cloaked classic types and preserves Hold, explicit targets and
retaliation. `test_orders` exercises each case and checks that a cloaked
Wraith still acquires. This is pinned OpenBW behavior, not an independent
retail-executable trace. It narrows Stargus/Stratagus's broader rule that
invisible units are not aggressive.

## Queued player orders

Pinned OpenBW `bwgame.h` `issue_order` permits eight
highlighted pending orders per unit, starts orders immediately from several
idle states and Patrol, and honors each order's queueable flag. Its global
1800-order allocation pool is not reproduced. Native classic `arr/orders.dat`
SHA-256 `b7e90406d9d7193d9294050d08d0e788b5bdb8e17882b3bc36d75c4d4093e815`
contains 176 records; PyMS's property layout puts interruptibility at
`7*176+id` and queueability at `9*176+id`. Stop (1), Move (6), AttackUnit (10),
AttackMove (14), Repair (34), Follow (49), EnterTransport (92), MoveUnload
(112), HoldPosition (107), and Patrol (152) are queueable. PlaceBuilding (30),
PlaceProtossBuilding (31), Unload (111), and classic cast orders inspected
at 113–148 are not. Inspect with e.g. `od -An -tu1 -j $((9*176+92)) -N 1
data/STARCRAFT/native/arr/orders.dat`. Shift is captured at player input and
the append bit travels in lockstep commands. Per-unit pending orders are
saved and checksummed; death and owner changes discard them. Internal
continuations do not travel on the wire.

OpenBW `order_HarvestGas` starts pending player work after
the current extraction rather than automatically returning gas. In contrast,
`order_MiningMinerals` issues ReturnMinerals through
`set_queued_order(true)`, which removes interruptible pending tail orders;
the native Move, AttackUnit, AttackMove, Repair and Follow entries all have
interruptible byte 1. ReturnMinerals then puts the next mineral approach at
the front. The current phase-based approximation and targeted tests cover
the ordinary move/attack/repair/follow queue and gas/mineral completion
boundaries; other internal worker orders and exact queued-order interruption
remain a separate fidelity question. `test_orders`, `test_mining`, and
`test_save` exercise the implemented behavior. Eighteen shared multiprocess
network cases passed, including validation of the queue bit and rejection of
internal state on the wire (`/tmp/open-rts-shift-network.log`).
**Confirmed OpenBW reference behavior; retail executable not independently
traced.** `set_unit_owner` clears an active target when its order has
`targets_enemies` and the new relationship is friendly; it does not inspect
pending orders. `activate_next_order` subsequently installs a pending order's
live target without checking allegiance. `order_AttackUnit` uses
`unit_can_attack_target`, which checks weapon eligibility, visibility and
other conditions but does not itself reject allies. Its reacquisition path
can still select another enemy according to target priority. Thus a queued
explicit attack can begin against a target transferred to an ally while the
order was pending; this differs from clearing the active target during the
transfer. See pinned OpenBW `bwgame.h` functions `set_unit_owner`,
`activate_next_order`, `attack_unit_reacquire_target`, and
`attack_unit_move_in_range` (search those names in the pinned source linked in
`REFERENCES.md`).

The engine records the target's owner at order activation and allows that
specific target to be attacked when a pending order starts after its transfer.
Automatic acquisition remains hostile. A subsequent owner change to an ally
invalidates the active order. `test_orders` covers both an active transfer and
a pending attack that damages the transferred unit; `test_save` exercises the
saved object representation. Invisibility, reacquisition, and retail
behavior remain unverified. The owner comparison is tick-based, so multiple
owner changes returning to the original owner between ticks are not detected.

## Simulation terrain and random rolls (2026-11-12)

**Confirmed native asset layout and reference behavior, not an executable trace.**
OpenBW at the pinned commit uses CV5 group word `+2` bit `0x10` for cover.
Its `get_ground_height_at` samples the unit's 8-pixel VF4 minitile: bit 4
means height 2, otherwise bit 2 means height 1. A megatile has sixteen
little-endian VF4 words. The CV5 megatile index is at `+20 + 2*(tile&15)`
in its 52-byte group. The Badlands asset SHA-256 fingerprints are
`b417ddd4523461159fce5357462bb2c9fe5c6f4536b10e0ddd1bdc84c56ae3ee` (CV5)
and `7bfa246f2bc9e8d9cc1323c968fb0a74c9ab7c7abe41daf110ad82fe05090d89` (VF4).

`G_LoadLevelData` now loads this simulation metadata independently of the
renderer. This also corrects headless levels previously missing the renderer's
wholly-unwalkable-cell mask. The mission owns the checked native flags; save
restoration retains the freshly loaded allocation. `sc_random` uses OpenBW's
LCG `state = state*22695477+1`, returning `(state>>16)&0x7fff`; triggers and
projectile launches share the saved, checksummed state. This replaces the
previous arbitrary trigger generator without claiming retail initial-seed parity.

Reproduce with `make build/bin/tests/starcraft/test_map` and
`SDL_VIDEODRIVER=dummy build/bin/tests/starcraft/test_map`: it compares every
minitile's height and cover against raw CV5/VF4 across six campaign maps.

## Moving-weapon investigation (2026-11-12)

**Confirmed asset records, executable behavior not independently verified.**
The classic-disc `arr/weapons.dat` SHA-256 is
`03551df8800a01ec1e99e3648ece6deb83c054588f52967ba76c1d85c92be461`,
`arr/flingy.dat` is `74042e8a1f529117b79a93d8092bf41a0ad7eeee6df0a9743ddd399886d79baa`,
`arr/sprites.dat` is `ffa606093737b71d1eaaf30f05695b9de8beb9314f44e0db3dfbb11611c162a8`,
`arr/images.dat` is `03f596ed97f47ec7278af3633d73717ecbc59f729d99456a0a6df46be220e7e0`,
and `scripts/iscript.bin` is `e62ecef8b5f7785d937c3125b90d1164fdc09db6c0cd6ef5d0e5d0761bafd0ca`.
PyMS's DAT layouts give 100 weapon and 184 flingy records. Graphics is the
weapons column at `2*N + 4*row`; behavior is at `19*N + row`; flingy speed,
acceleration, turn and movement-control are at `2*F + 4*id`, `6*F + 2*id`,
`12*F + id` and `14*F + id` respectively. For example,
`od -An -tu4 -j 216 -N 4 data/STARCRAFT/native/arr/weapons.dat` prints 145,
the flingy for weapon 4, the Vulture grenade. Its speed is 8533/256 pixels
per 24 Hz frame, acceleration 667/256, turn 40/256 circle, behavior 0,
flingy 145 → sprite 343 → image 532 → IScript 242. Weapons 4 and 5 share it.
Wraith Burst Lasers 16/18 use flingy 149, speed 17067, acceleration 8533,
turn 127, sprite 348, image 535 and script 243; Battlecruiser lasers 19–24
use flingy 148, speed 25600, acceleration 8533, turn 127, sprite 347, image
534 and script 243. These speed/acceleration integers are 8.8 pixel units.
Mutalisk Glaive Wurm 48/49 is behavior 7, flingy 165, speed 8533,
acceleration 667, turn 127, lifetime 60 frames, sprite 358, image 511,
script 264. Firebat Flame Thrower 25/26 is behavior 5, flingy 172, zero DAT
speed and IScript-controlled movement, sprite 370, image 503, script 292;
the flame overlay is separately image 421. Stargus's moving Firebat flame is
therefore not a verified native weapon path.

**Confirmed in the pinned OpenBW source and Stargus IScript listing.**
`bwgame.h` `initialize_bullet`, `bullet_state_init` and `bullet_state_move`
use the weapon's initial target position for behavior 0; behavior 1 follows
the unit, and behavior 7 retargets up to three bounce hits. Spawn uses the
weapon's native forward/up offsets; a flingy starts with next speed zero and
uses its acceleration/turn settings. Init waits for IScript `sigorder 1`.
`reference/stargus/doc/iscript.txt` script 242 signals after one `wait 1` and
script 243 does likewise; their Death animations execute `domissiledmg`.
Script 264 executes that opcode in both Death and the intermediate bounce
state. `bwgame.h` dispatches `domissiledmg` to `bullet_hit` and does not
implement these shots as first-body collision. The corresponding weapon
graphics and damage timing are not represented by the generic `P_SpawnMissile`,
which uses physical first-body collision. The StarCraft path now uses ordinary
saved nonblocking mobjs for normal-damage behaviors 0, 1 and 7; it retains
owner/target references, native flingy speed and turn data, and the shield,
armor and 1/3/9 bounce damage rules. Homing missile IScript headers 232, 234,
235, 240, 260 and 266 likewise wait one frame before the signal and dispatch
damage in Death. Heading-offset column `35*N + row` alternates twin missile
headings per source. OpenBW rolls the synchronized random stream at launch,
including a one-in-256 miss on level ground; cover and uphill ground attacks
use threshold 119, Dark Swarm 255. The saved mission records the RNG and
previous heading choice. `test_projectiles` exercises launch, terrain miss,
flight, moving targets, twin headings, bounce and save/reload.

Movement uses the engine's fixed-point angle and distance helpers, so it is
an approximation of OpenBW's flingy math. Bounce candidates use thinker order
inside the native square rather than OpenBW's spatial lookup order.
OpenBW `attack_with_weapon` dispatches both projectiles consecutively when the
weapon bullet count is two; Goliath and Scout IScript each use one `attackwith 2`.
Trail and Death overlays and their sounds are not yet reproduced. These are
source-derived findings, not independently traced retail executable behavior.
For normal and splash weapons with behavior 2, the same roll is made at attack
dispatch. A normal miss deals no damage; radial splash still lands at the
30-pixel-shifted aim point. OpenBW chooses the intersection toward the attacker
on the target's quarter-inset inner bounds for the initial point. Splash
distance uses the nearest point on the native unit bounds and OpenBW's integer
`xy_length`. Behavior 5 appears at the source
and does not use the miss roll, including Firebat splash under Dark Swarm.
The instant implementation does not reproduce its separate impact animation.

**Spider Mine movement.** `units.dat` row 13 has the normal order field zero,
but flingy speed 4096 and turn 127. OpenBW `order_SpiderMine` sends an awakened
mine after its target, so the actor catalog must mark it mobile despite the
zero generic-order field. `test_abilities` checks the catalog speed and follows
an actual mine from burrow through pursuit and detonation.

## Options and saved-game dialogs (2026-11-12)

The retail `.bin` controls decoded by `test_glue` confirm that
`rez/spd_dlg.bin` contains sliders 1–3 for Game Speed, Mouse Scroll and Key
Scroll, and `rez/snd_dlg.bin` contains sliders 1–2 for Music Volume and Digital
Volume. Both use 65534 for OK and 65533 for Cancel. Their SHA-256 hashes are
`9d7cc3b5287d8b33ab4604d6c07227246b5c8b58202b286e46caa49b28acc932` and
`7bc6cf141979d8657af1e80c737511af530866030299d85ce97621e0a7e61b01`, respectively.
Stargus `scripts/menus/options.lua` applies speed and effects-volume changes
from slider callbacks. The engine uses its own 10–200% speed and 0–10 sound
settings; those ranges and rollback on Cancel are engine choices, not verified
retail formulas. Multiplayer speed changes are disabled to preserve lockstep.
Scroll speed and sound checkboxes remain unsupported and disabled.

`rez/savegame.bin` contains name field 2, list 1, Save 65534, Delete 3 and
Cancel 65533; `rez/loadgame.bin` has list 1 and Load 65534. Their SHA-256
hashes are `dbf0933fb22c4392016bab939b0c907b27cf217292443194d498705d720b4f94`
and `fa9f979ae8f4cecf0e0e5a31013647ec6d2d9045c24b0bb6d64d065efedb22bb`.
The controls now use the engine's `.sav` format; retail `.snx` files are still
listed but cannot be loaded. The engine save hooks preserve CHK trigger cursors
and waits, switches, death and kill counters, timers, AI enablement, objectives,
locations, statistics and RNG. The newly loaded level retains ownership of its
CHK blob and pointers; these are never restored from saved addresses. The hooks
follow the lifecycle in `game/g_save.c` and Warcraft II's `G_SaveExtra`.
`test_save` saves a full blob, frees and reloads the level, restores the blob,
and rejects malformed trigger counts and truncated extra data. The saved local
player and mission side are restored before HUD creation, including when the
custom lobby is absent in a new session; multiplayer resynchronization retains
the receiving player's identity. No executable
was examined, and retail save
format and exact option behavior remain unknown. Reproduce the control catalog
with `SDL_VIDEODRIVER=dummy build/bin/tests/starcraft/test_glue` and verify the
menu actions with `SDL_VIDEODRIVER=dummy build/bin/tests/starcraft/test_gameplay`.

**Music and shortcuts (Stargus behavior).** `scripts/sound.lua` plays title
music in the menu and selects a random track from three race tracks during
play; `scripts/commands.lua` maps F1/F5/F7/F8/F10/F11/F12 to Help, Options,
Sound, Preferences, Game Menu, Save and Load. These are Stargus behaviors,
not independently verified retail executable behavior. The native install has
`music/title.wav` and `music/{terran,zerg,protoss}{1,2,3}.wav`, 16-bit stereo
PCM at 22050 Hz. For example title and `terran1` SHA-256 are
`289d139f6d920a334f0dd1127be2d0a2cc00902565b008bbc9b7972add0fe64c` and
`c4aee48492dff7da213ff0afae28ae225d6a470828f69187e41084981806b45e`.
One decoded music sample is retained at a time, using independent music and
effects volume settings. A missing chosen track leaves that music mode silent
until the next mode change; music streaming and exact retail track sequence
are not established. The in-game shortcuts open the existing dialogs; F9
opens the diplomacy dialog described below.
`test_sounds`, `test_gameplay` and shared `test_sound` check track selection,
settings, shortcuts and mixer ownership. Dark Colony's ambience callback skips
front-end frames where no map is loaded.

**Diplomacy (native controls and Stargus behavior).** Native
`rez/allyfltr.bin`, SHA-256
`ddf79f900279113e732e921cb82a3ab03bdd2ee93bdcb5d7e1d438a0fbd343a7`, has
player names 65526–65518, alliance checks 1–9, shared-vision checks 13–21,
Allied Victory 25 and Accept/Cancel 65534/65533. Stargus
`scripts/menus/diplomacy.lua` applies each player's outgoing alliance and
shared-vision choices separately; its shared-vision checkbox initialization
has an upstream FIXME. `reference/PyMS/PyMS/FileFormats/CHK/Sections/CHKSectionFORC.py`
identifies force flags 0x02 as Allies and 0x10 as Shared Vision. This corrects
the old implementation, which used the Allies bit for both combat and sight.
The native dialog now submits directional `TC_ALLY` and `TC_SHARE_SIGHT`
commands; combat alliances live in the level-owned mission, sight grants in
the receiving player's sight mask. Both are saved and included in lockstep
consistency checks. The Allied Victory control remains disabled because that
rule is not implemented (Stargus comments out that checkbox, although its
help describes it). Blizzard's [StarEdit FAQ](https://classic.battle.net/scc/faq/triggers.shtml)
states that Set Alliance Status affects the current player's direction only;
this corrects our prior symmetric trigger implementation. `test_mission`
checks both directions and the two force bits separately;
`test_save` checks asymmetric changes, player validation, command queuing and
round-trip persistence; `test_gameplay` checks F9, Cancel and Accept. No
retail executable behavior was inferred from the dialog artwork.

**Placement sounds.** Native `arr/sfxdata.dat` and `arr/sfxdata.tbl` SHA-256
are `e9d28f58c9b78d933280d5534ab207e7f284dfd5a9b2c964cb529edddd298baa`
and `5d0aa1af9d8dfc3f2213639e8c60de5250758758f0b7a0871e26d8aab09b1b69`.
Rows 2 and 4–6 name `Misc\Buzz.wav` and the Zerg, Terran and Protoss
`*BldgPlc.wav` sounds. Stargus's `scripts/sound.lua` assigns placement-success
and placement-error sounds but uses the Terran placement sound for every race.
The engine plays the native race-named sound when a placement order is accepted
and Buzz when placement remains invalid; the race selection is inferred from
native names, not independently verified against the executable. `test_sounds`
checks the native rows and `test_gameplay` checks both placement paths with
dummy audio.

**Briefing sound observation.** Terran 01's extracted CHK (SHA-256
`4b0e5009c4ae80cfb4cb13ede30842db0aa05bb3be2eedad695fb093fa5786f6`)
contains the string `staredit\wav\T1B00tad.wav`. The extracted WAV exists
relative to that campaign mission directory as `staredit/wav/t1b00tad.wav`
(SHA-256 `8fa86646edde24a841f9295fffae61254ee7373c88c87cb70388b00830a5a8ec`).
Stargus `scripts/menus/campaign.lua` starts briefing voices and stops the
channel on Continue. The MBRF player now resolves the WAV relative to the
mission folder, normalizes native backslashes and case, and plays it for both
standalone WAV and transmission actions. One reusable engine sound slot bounds
sample ownership; Replay, Continue, Cancel, Escape and menu shutdown stop it.
Missing files leave the briefing running silently. Terran 02's CHK (SHA-256
`d8a7f6b70422ff32e4fd3f6293de4013f883e0761814d4496ffd18b7eb13dfce`)
contains standalone WAV actions 9 and 12; `test_gameplay` exercises action 9
and the transmission in Terran 01. `test_sounds` checks repeated reuse, missing
files and sound reinitialization; the shared `test_sound` checks unloading a
playing sample and loading another into the same slot. MBRF scheduling follows
the existing script player; exact retail audio overlap timing remains unknown.

## Repair bounds, Follow, flight and gathering waits (2026-10-11)

This follow-up supersedes the one-tile repair approximation and the missing
Follow/lift/land statements below. It inspected native DAT data and the pinned
Stargus, Stratagus, PyMS and OpenBW sources; **no retail executable was run or
disassembled**, so there are no new EXE addresses or fingerprint claims.

**Confirmed asset geometry.** Original-disc `arr/units.dat`, SHA-256
`dc7b8b59bb14a3ba67829926ad50e8d363032a351299e5087f790ed09616619d`,
column 38 starts at byte **12124** and contains 228 eight-byte records of
unsigned little-endian left/top/right/bottom extents. The C catalog now retains
all four extents. They are distinct from column 36 placement dimensions:
SCV is 11/11/11/11, Vulture 16/16/15/15, Command Center 58/41/58/41,
Barracks 48/40/56/32 and Bunker 32/24/32/16. The collision rectangle is
`position - (left,top)` through `position + (right,bottom) + (1,1)`.

**Confirmed in OpenBW, not independently EXE-verified.** `bwgame.h` at
`4b046d5f65302b10cb0a745f0fecd37ec85b20a8`, SHA-256
`5ee316ca2c008c9252f7fde7444ca5dc7f35440549be2576dc37fa228fb31ae6`:

- `unit_sprite_bounding_box`, `rect_difference`, `xy_length`,
  `unit_target_in_range` and `order_Repair` establish a five-pixel gap between
  these rectangles. For sorted nonnegative gaps x >= y, distance is x when
  x/4 >= y; otherwise it is `x-x/16+y*3/8-x/64+y*3/256`, with integer
  division at each term. At aligned SCV/Vulture centres, separation 33 pixels
  gives gap 5 and allows repair; 34 gives gap 6 and requires approach.
- `order_Follow` retains the target, checks distance periodically with a
  seven-frame timer, and stops within 96 pixels of its bounds. It remains
  active for a mobile target, resuming when that target moves away; a grounded
  building ends the order. Our friendly right-click and Move-on-unit use the
  existing stable target ID, thinker and Stop lifecycle. Following suppresses
  autonomous combat. Transport boarding and worker repair/drop-off take priority.
- `order_BuildingLiftoff` releases ground occupancy and enables flying movement.
  `order_LiftingOff` uses one pixel per 24 Hz frame and a 42-pixel rise for
  buildings; its building speed remains one pixel/frame. `order_BuildingLand`
  validates placement and restores occupancy on completion. `units.dat` flag
  0x20 identifies liftable types; no building-name whitelist is needed.
- `building_abandon_addon` calls `make_unit_neutral`, which transfers an add-on
  to owner 11 and cancels production/research through ownership transfer.
  `find_connecting_addon` and `connect_addon` reconnect a compatible neutral
  add-on at the parent's placement origin plus the add-on's DAT offset, and
  transfer ownership to the landing building. Our parent/child IDs, owner,
  team and allegiance follow this lifecycle. Detached neutral add-ons cannot
  accept production. Tests include reconnection after lift and landing.

**Implementation boundaries.** Repair's short final approach checks native
rectangles and authored terrain, allowing movement through empty placement
margins. Long paths still use the engine's existing grid planner. Trying native
rectangles for all movement exposed a mismatch with its circular clearance;
enclosing them in larger discs also disturbed repair separation. Both broad
changes were rejected. General pathfinding is not claimed to match OpenBW.
Repair startup/turning frames remain approximate; the HP and debit rules from
the first implementation are unchanged.

Flight uses the ordinary mobj's z for the vertical rise, keeps its ground
position stable during liftoff, and moves horizontally through shared flying
movement. Lift/land animations use native iscript entries 18/17. PyMS's icon
list names native command frames 282/283 as Lift off/Land; both use L in the
last command slot here. A flying building exposes Move, Stop and Land, cannot
produce or receive returned cargo, and regains those functions on landing.
A busy producer cannot lift. Invalid sites preserve the current order; a site
obstructed during travel aborts landing. **Remaining fidelity limits:** retail
iscript order-signal synchronization, dust/shadow subimages, landing-site
reservation during descent, and exact placement preview/UI details are not
reproduced. Engine vertical projection substitutes for OpenBW's image-offset
and sprite-position choreography. Follow's enemy/rescue/special-building
transitions and shift queues remain outside this implementation.

**Confirmed Stargus gathering waits.** SCV, Drone and Probe scripts all specify
capacity 8, `wait-at-resource` 150 for minerals and 50 for gas, and
`wait-at-depot` 50. Stratagus `settings.h` defines 30 cycles/second;
`action_resource.cpp` initializes and decrements `TimeToHarvest`, transfers
when it becomes negative, and waits for an unbreakable work animation to finish.
Thus the configured waits are five seconds at minerals and 1 2/3 seconds at
gas/depot, with extra cycle/animation-boundary delay in that reference.
Our former one-second shared collection tick was too short. StarCraft now
uses 5000/1667 ms gathering waits and a 1667 ms depot wait, rounded up to an
engine tick. These are an explicit **Stargus timing choice**, not a claim of
retail timing equivalence. Other games retain their previous default waits.
OpenBW's `order_MiningMinerals` instead installs a 75-frame timer at 24 Hz
(3.125 seconds before its order-dispatch details); this is further evidence
that Stargus's five seconds must not be described as a recovered retail timer.
The timer starts only after arrival/turning, resets on each visit and does not
accumulate during travel. Animation repetition remains independently authored.
Temporary logging measured 5016 ms for minerals and 1683 ms for gas at the
transfer tick; it was removed after tests passed.

**Retail gathering (2026-10-14), superseding the Stargus waits above.**
StarCraft now uses the OpenBW order timers: 75 frames at a mineral field
(3125 ms) and 37 frames inside a refinery (1542 ms) at 24 frames a second,
and cargo drops the tick the worker reaches the depot. The OpenBW source is
not vendored here; the 75 comes from the citation above, the 37 and the
instant drop-off from the OpenBW order functions as recalled, unverified
against an executable trace. Two retail rules came with it as shared data:
`harvestresource_t.exclusive` lets one worker work a field or refinery at a
time (others wait beside it or move to a free field within four cells), and
`resourcevent_t.depleted_rate` keeps an exhausted geyser yielding 2 gas a
trip. `test_mining` checks the waits, the queue and the depleted geyser.

The timing change exposed an existing AI placement assumption on `(2)road war`:
the first Pylon, Gateway and Cybernetics Core could exhaust available powered
sites. Logged Stargate purchases repeatedly reported no site. An untouched
HEAD archive passed with the former worker timing, proving this was an exposed
placement dependency, not a general pre-existing test failure. The AI now treats
exhausted powered building space as a development need and constructs one
additional Pylon, waiting for any pending Pylon before starting another.
This is engine AI behavior, not an original-game finding.
The AI soak keeps all its advanced-unit and level-two upgrade assertions,
but runs for 30 simulated minutes instead of 14 to accommodate the slower
economy. The old duration reached only level-one upgrades for all three races.

**Reproduce.** `build/sc_catalog data/STARCRAFT/native` emits the extents.
Run `test_orders`, `test_mining`, `test_gameplay`, and `test_tech_path_protoss`
under `SDL_VIDEODRIVER=dummy`. They check the 5/6-pixel repair boundary,
approach inside placement margins, Follow stop/resume/removal, flight and
ground occupancy, production/drop-off eligibility, add-on reconnection,
blocked landing, and two full gathering visits for each worker/resource pair.
The gameplay test exercises the native flight buttons and saves
`/private/tmp/starcraft-lifted-building.bmp`. Protocol version 6 prevents
older peers from interpreting the extended command enum as a resync packet.
The Follow click fixture supplies the native sprite cache and refreshes sight
after spawning its target; without that sight update the picker correctly
rejects a unit in unrevealed terrain. Catalog comparison after stripping the
four new bounds fields confirms all previous emitted fields are unchanged.

**Verification outcome.** `make -j4 all` passed without new warnings;
all 16 StarCraft and 41 Warcraft II quick tests passed with local socket access
for multiplayer. The 30-minute three-race AI soak and 15-minute duel passed,
including advanced rosters, all races reaching level-three upgrades, and
hold/retreat behavior. Dark Colony's `test_combat_and_harvest` and StarCraft/
Warcraft II `--check` passed. Every test used `SDL_VIDEODRIVER=dummy`.
The flight command-card BMP was rendered and visually inspected. Temporary
diagnostics are removed. Repair startup and the flight presentation limits
listed above remain explicitly unverified rather than implied complete.

## Core orders, first implementation (2026-10-11)

**Implemented and regression-tested:** Hold Position (H), attack-move through
Attack (A) on empty ground, Patrol (P), Return Cargo (C), and SCV Repair (R).
The audit below predates this implementation. Terran lift/land and Follow
remain absent; these changes do not establish complete retail order fidelity.

**Evidence.** No original executable was examined in this pass, so there is
no new EXE fingerprint or instruction-address claim. Blizzard's Unit Commands
and SCV guides establish hold without pursuit, movement without retaliation,
patrol resumption and ground patrols' air-target exception, and repair eligibility.
Stargus's pinned race `units.lua`, SCV script and `icons.lua` establish the
command icons, hotkeys and a one-tile repair range. PyMS `Data/Icons.txt` row
234 names zero-based native command-icon frame 233 as Return Resources.
Exact source URLs and revisions are in `REFERENCES.md`.

**Confirmed in the reference implementation, not independently EXE-verified:**
OpenBW `bwgame.h` at `4b046d5f65302b10cb0a745f0fecd37ec85b20a8`
(SHA-256 `5ee316ca2c008c9252f7fde7444ca5dc7f35440549be2576dc37fa228fb31ae6`),
`order_Repair`, `order_ReturnMinerals`, `order_Patrol`, and unit initialization:

- Repair requires a completed Terran mechanical target. Repair HP uses the
  construction rate at 24 Hz. With max HP expressed in 1/256 points, the rate
  is `(max - max/10 + build_time - 256) / build_time`; zero rate or zero build
  time uses 256. The target's mineral/gas costs determine a periodic debit
  using a factor of three, rather than a flat one-resource-per-HP charge.
  The timer is debited at zero and decremented on subsequent frames.
- Our Vulture fixture (80 HP, 450 frames, 75 minerals) gains 41/256 HP per
  reference frame, pays a mineral on frame one and every 20 frames thereafter,
  and repairing from 40 to 80 HP spends 13 minerals. An initial test expectation
  of 40/256 was disproven by logging the integer formula: rounding gives 41.
  Wraiths gain 31/256 at their 900-frame build time; two repairers contribute
  independently and each pays its own resource debit.
- Return Minerals/Gas honors a specified active depot or searches for one,
  delivers the carried resource and schedules harvesting again. We retain an
  existing resource target across explicit return, and can deliver even after
  Stop has discarded that target. An absent/destroyed depot preserves cargo.

**Implementation consequence.** Orders run through existing tic commands and
the ordinary Doom-style mobj thinker; there is no parallel order ticker or
object store. A one-point `TC_PATH` with `WP_BACKTRACK` expands at execution to
the clicked cell and each unit's own starting cell. `TC_ATTACK` without a
target ID becomes a one-point `WP_ONCE` route; a stale nonzero target ID cannot
become attack-move. Combat preserves the route and resumes it after pursuit.
Hold uses a checksummed unit flag. Repair uses the existing pending-order
target/timer and fractional damage remainder, and the compiled SCV AlmostBuilt
work animation. Ordinary Stop cancels it; spells, boarding, merging and siege
orders discard previous routes. Rejected move/attack commands preserve Hold.

Return Cargo shares the existing harvesting lifecycle. Returning is processed
before requiring a valid resource vent; only resuming harvest needs that vent.
SC depots must belong to the worker's owner. Right-click returns to the clicked
depot; damaged Terran mechanical targets instead accept SCV repair, except a
Refinery whose contextual command remains Gather. Explicit Repair works there.
Repair cannot convert a damaged Bunker click into boarding. Move and ground
right-click do not acquire or retaliate against enemies during travel.

**Remaining approximations/unknowns.** Repair currently uses Stargus's one-tile
reach, with our existing building footprints. OpenBW uses five pixels between
native collision bounds; placement dimensions and engine radii are not those
bounds, so substituting a tuned radius would be unjustified. Native repair
startup/turning frame timing, gathering-slot arbitration, acquisition ranges,
unarmed patrol fleeing, sub-cell patrol endpoints, and shift-order queues are
not implemented by this pass. With no remembered field, Return Cargo finishes
idle rather than searching for a new resource. Retry timing for a missing depot
uses the shared thinker, not OpenBW's 75-frame retry timer. The Return Cargo
button uses native frame 233 in the spare second-row slot; its precise retail
button replacement rules remain unverified. No new PNG assets are introduced.

**Reproduce:** `make -j4 build/bin/tests/starcraft/test_orders
build/bin/tests/starcraft/test_mining build/bin/tests/starcraft/test_gameplay`,
then run those binaries with `SDL_VIDEODRIVER=dummy`. The tests cover combat
interruption/resumption, two patrol legs, unarmed air patrol, forbidden air
pursuit, repair eligibility/debits/fractions/multiple SCVs, target removal,
explicit depot selection, missing-depot cargo retention and mining resumption.
The gameplay test drives the native command card and world clicks and writes
`/private/tmp/starcraft-core-orders.bmp` for visual inspection.

Verification completed: `make -j4 all`; all 16 StarCraft and 41 Warcraft II
quick regressions (including loopback menu/multiplayer checks); StarCraft's AI
soak; Dark Colony's `test_combat_and_harvest`; and both per-game `--check` smoke
tests passed. All test runs used the dummy SDL driver. The rendered command
card was inspected: all five Marine order icons occupy their expected slots.
Two existing unused-helper warnings in `test_mission.c` are unchanged.

## Completeness recheck (2026-10-11)

The [current source/regression audit](BLIZZARD_COMPLETENESS_AUDIT.md#starcraft)
supersedes older broad status statements while preserving their evidence below.
No new executable analysis or fingerprint is claimed. Confirmed source gaps
include core orders, six base abilities, full mobile transports and Nydus,
selectable construction sites, per-player CHK upgrade/technology initialization,
partial trigger/IScript/AI execution, save/load/options and gameplay audio.
`game_info.sound` is unset; sound event calls alone do not implement playback.
UPGR/PUPx/PTEC/PTEx restrictions are distinct from supported cost patches and
PUNI. `p_trig.c` silently skips unhandled actions and uses movement orders for
Move Unit; these require semantic tests before campaign completion claims.

Corrections: Shield Battery recharge, Zerg tiers, add-on research and selected
caster AI already exist. Bunker boarding code also accepts other capacity-bearing
units under Bunker restrictions, so transport is **partial**, not wholly absent.
Stargus itself comments out converted trigger logic and skips per-player tech
chunks; matching it is insufficient. The audit records exact code/reference
paths, remaining unknowns, test commands and prioritized completion work.

## 2026-10-11: moving SCV engine glow

**Confirmed from retail assets.** The original disc's `scripts/iscript.bin`
(SHA-256 `e62ecef8b5f7785d937c3125b90d1164fdc09db6c0cd6ef5d0e5d0761bafd0ca`)
defines `SCVWalking` as `playfram 0`, `imgol 249 0 0`, `setvertpos 0`, then
the normal loop. Image 249 is `thingy/tscGlow.grp` in `images.dat` (SHA-256
`03f596ed97f47ec7278af3633d73717ecbc59f729d99456a0a6df46be220e7e0`):
with 755 rows of 38 bytes, its `gfx_turns` at `4*755+249` is 1, draw function
at `8*755+249` is 9, and remapping at `9*755+249` is 1. Stargus's pinned
`doc/iscript.txt` labels its image script `Engines_GlowHeaderInit`, which
alternates `engset 0; wait 1` and `engset 1; wait 1` at 24 Hz. The retail
`unit/thingy/tscglow.grp` (SHA-256
`f083b78855434679dac1a4fe6f68ab16db76e22bd1d36103c18de08eeb16e7d9`)
has 68 pictures on a 72×72 canvas, four 17-facing sets. The parent SCV GRP
has 51 pictures, so the two glow phases are sets 0 and 3. Decoded glow
indices are at most 46.

**Confirmed lookup; inferred executable selection.** The Badlands
`tileset/badlands/ofire.pcx` (SHA-256
`841335964db2d3d186c4118587191576102af221adfb4f770390d5e75ae63276`)
is 256×63 indexed pixels. Stargus's palette notes identify it as a PCX2D
source-over-destination lookup. Source index 1 uses row 0 (the identity row),
source index 6 over destination index 64 gives 86, and source index 46 over
64 gives 63. Index 0 leaves the destination unchanged. The engine now loads
the active tileset's table, selects a phase per walking SCV, uses the SCV's
facing and the GRP's native crop pivot, and blends against the existing world
pixel. It hides the attachment when the walking state ends. The exact retail
EXE instruction that maps draw-function 9/remapping 1 to `ofire.pcx` was not
disassembled; that association is inferred from the DAT flags, asset shape,
and Stargus documentation. Other native remapping modes remain unknown.

Reproduce with `env SDL_VIDEODRIVER=dummy build/bin/tests/starcraft/test_gameplay`;
it writes `/private/tmp/starcraft-scv-glow.bmp` with the moving SCV, checks
both animation phases over a fixed background, and checks that the idle pose
does not emit the glow. The orange exhaust also appears beneath moving SCVs
in the user-provided retail screenshot from 2026-10-11.

Investigated 2026-10-08. This is the original StarCraft disc, not a Brood War
installation. No executable disassembly was used for the initial catalog.
Do not infer original executable behavior from the sandbox's engine behavior.

For the consolidated byte-level specification, lookup formulas, all 54 unit
columns, adapter limits and unresolved audit findings, read
[StarCraft classic format reference](SC_FORMATS.md). This file remains the
investigation journal and input fingerprint record. The initial implementation
sections describe commit `a23c42d`; later work should append dated findings
rather than silently treating these limits as permanent.

## Reproducible inputs

SHA-256 fingerprints:

| Local input | SHA-256 |
| --- | --- |
| `data/STARCRAFT/StarCraft.iso` | `4f7455c8ea55ca3b7d49209bc25cf9edb62f40a7e06015701159ae47cbd0033b` |
| `install/files/stardat.mpq` | `4a503190f79289d827bb7ea99f06b8b9f18a120e360f8442e10933939eafb525` |
| `install/files/starcraft.exe` | `16cd8f0d1098f8bcd0625d5e13964c2facf2df4874636acb01c2e0bfb062b2f5` |

`bsdtar` extracts the ISO unchanged to `disc/`. Its `INSTALL.EXE` contains an
MPQ. The C `sc_import` tool uses StormLib with Stargus's listfile, extended
with the installer `files\` prefix. Enumeration extracts 782 installer
members, then 2,897 members from `files/stardat.mpq`, with zero extraction
failures. Unknown member names are retained as supplied by StormLib.
Output paths are lowercase and separator-normalized, with traversal rejected.
Fonts are in `install/files/font/`; most runtime assets are in `native/`.
The extraction retains all entries, including sounds, maps, movies, EXEs,
and files the initial implementation does not yet consume.

```
make starcraft-unpack
make build/sc_catalog
build/sc_catalog data/STARCRAFT/native > /tmp/sc-units.inc
cmp /tmp/sc-units.inc games/starcraft/units.inc
make test-starcraft
```

## Confirmed: UI assets and layout are external

The MPQ contains `rez/glumain.bin`, `rez/gamemenu.bin`, `rez/statdata.bin`,
`rez/statbtnt.bin`, `rez/statport.bin`, `rez/minimap.bin`, `rez/stat_f10.bin`,
and many more dialog resources. They are not IScript bytecode despite sharing
`.bin`. The original classic dialog record is 86 bytes. PyMS's independent
format definition and the retail bytes agree:

| Byte offset | Value |
| --- | --- |
| 0 | next control, u32 file offset |
| 4, 6 | x, y, u16 relative to dialog origin |
| 12, 14 | width, height, u16 |
| 20 | NUL-terminated text/path, u32 file offset |
| 24 | flags, u32 |
| 32 | identifier, u16 |
| 34 | control type, u32 |
| 54..60 | responsive rectangle, four u16 values |
| 66 | first child for root; SMK descriptor for a control |
| 70, 72 | text origin offset, u16 |

Flags include visible 8, disabled 2, hotkey prefixes 0x100/0x200, font
selection and text alignment. Text uses embedded color controls. The loader
retains hotkeys and highlighted spans and strips nonprinting color bytes.
The initial shared-menu adapter uses the visual bounding rectangle for input;
precise responsive subrectangles are not yet applied.

A SMK descriptor is 30 bytes: next overlay u32 at 0, flags u16 at 4, path
u32 at 10, and x/y u16 at 18/20. Flag 4 repeats and flag 8 restricts an
overlay to hover. Normal and hover files are layered; replacing the whole
normal movie with its smaller hover overlay was an incorrect early approach.
The movies retain each frame's palette and native dimensions/timing.

`glumain.bin` has ten controls. Single Player is id 3 at (0,0), 320x190,
with text offset (150,111); its hover overlay is at (46,66). Multiplayer is
id 4, type 14, at (78,190), 283x178. Campaign Editor is id 5 at (375,104).
Exit is id 2. Main menu background is `glue/palmm/backgnd.pcx`.
No invented logo or catalog buttons are inserted into this screen. Single
Player deliberately launches the catalog.

**Correction (2026-10-08):** id 4 opens the shared engine TCP lobby described
in "Multiplayer lobby" below. Retail connection dialogs are not used. The
remaining main-menu actions still open the unavailable dialog.

Native HUD geometry, after adding parent offsets:

- minimap (6,348), 128x128;
- nine command slots, first (505,358), 36x34;
- portrait (413,410), 60x56;
- Menu button (416,388), 64x20;
- selection dialog origin (138,388), 270x92.

`game/tconsole.pcx` supplies the full console. `dlgs/terran.grp` contains
widget artwork, including authored pressed states; `dlgs/tile.grp` supplies
nine dialog frame tiles. Compressed and raw GRPs both occur. Duplicate raw
frame offsets contribute once to the payload size, matching Stargus's
raw-layout discriminator. Button three-part frame indices 115..117 and
118..120 are documented by PyMS. The game uses these through shared menu
items and draw callbacks, not a separate menu responder or application loop.

Stargus's `scripts/guichan.lua` manually places animated buttons at different
coordinates (for example Single Player at 100,50). `UIConsole.cpp` slices
PCX graphics; it is not a native BIN layout decoder. The initial assumption
that Stargus already reproduced all original BIN layouts was disproven.
The executable still supplies control actions and gameplay-dependent state;
external layout is not a complete UI implementation. Warcraft II in this repo
also reads external dialog records from REZDAT.WAR, with some strings/resources
in its EXE; the distinction is not simply “StarCraft files versus Warcraft EXE.”

## Confirmed: fonts and palette indirection

`FONT` header bytes 4..7 give low/high glyph, maximum width/height, followed
by u32 glyph offsets. Each glyph begins width,height,x,y bytes. Payload byte
high five bits skip pixels, low three select ink; ink zero is transparent.
Treating ink zero as opaque produced magenta fringes and was corrected.
`glue/palmm/tfont.pcx` and `game/tfontgam.pcx` supply the actual font ramps.

Player colors remap indices 8..15 through eight-pixel rows of `game/tunit.pcx`.
Command images use the first 16 pixels of `unit/cmdbtns/ticon.pcx` as their
palette, not the console's embedded palette. Full-health wireframes remap
192/193 from twire pixels 3/4, 208..211 from pixel 1, and 216..219 from pixels
0/1/18/10. These are the Stargus author's published mappings, verified against
our extracted ramp pixels. Damaged-section randomization is not implemented.

## Confirmed: unit image chain and terrain

Classic table sizes in this disc are units 19,192, flingy 2,760, sprites 2,081,
and images 28,690 bytes. All 228 units follow units.graphics -> flingy.sprite
-> sprites.image -> images.grp -> images.tbl path, prefixed with `unit/`.
Shared images alias one engine-owned sheet; all 228 unit entries are retained,
including heroes, subunits, unused slots, resources, doors and map objects.
The result has 148 unique GRPs and 7,667 original physical frames.

The first 772 bytes of classic sprites.dat are **386** u16 image indices.
Stargus's expanded-table formula yields 353 for this classic file and wrongly
rejects door sprite IDs 381..385. The verified classic size has an explicit
386-entry image span. No interpretation of the remaining classic sprite
columns is claimed by this loader.

GRPs have six-byte headers and eight-byte cropped-frame descriptors. Crops,
pivots, RLE skip/repeat/literal runs, and every physical frame are retained.
Directional sets store 17 facings north through south clockwise. Shared
renderer slots run counterclockwise, with the other half mirrored. This is
sprite lookup, not a world-coordinate flip. Native unit subunit1 references
and images.dat special-overlay LOL offsets compose tank/Goliath turrets.

IScript Init/StarEditInit and Walking visual paths compile to shared states:
frame changes, waits, unconditional jumps, calls/returns. Conditional branches
are skipped, not evaluated. Only the bounded visual path is supported, not
the complete original VM or spawned effects. The adapter assumes
24 Hz for wait conversion to the engine's 30 Hz; this was not established by
a retail timing trace. The current import logs next-free state index 1,322
(including the reserved zero slot in that span). Missing visual paths retain
the first frame.
Shadows, construction/damage overlays, some special image draw modes, combat,
and exact original randomized animation behavior remain unimplemented.

Badlands WPE supplies RGBx colors. CV5 groups have 52-byte records and their
16 megatile references begin at byte 20. VX4 contains sixteen u16 minitile
references per 32x32 tile, with bit zero indicating horizontal reflection;
VR4 stores 8x8 indexed pixels. The extracted tileset decodes 4,844 megatiles.

## Explicit sandbox behavior and verification

The authored 128x128 catalog places every unit slot at native Y-down pixel
coordinates `(192 + column*240, 192 + row*240)`, 16 columns. Dirt is CV5 group
2. All cells are explored and visible. This layout and the common mobile
speed of three grid cells/sec are sandbox choices, not reverse-engineered
retail behavior. HP, costs, dimensions, flags and portrait IDs are generated
once into committed C literals; runtime balance does not depend on DAT tables.

Build uses `RTS_WORLD_Y_UP=0`, exactly as Warcraft II. No shared coordinate or
framebuffer transformation was changed. Navigation, thinker movement,
selection and menu input all use shared engine facilities. Original native
portraits follow units.portrait -> portdata.dat idle -> portdata.tbl, with the
first authored numbered `.smk` variant. Random talking/idle variation is not
implemented. The HUD currently uses the Terran console for the catalog.

`make test-starcraft` verifies 228 actors, all image references, the classic
sprite chain, decoded frame counts, malformed GRP/BIN rejection, representative
north/east/south/west facing/mirror relationships, movement toward increasing
native X/Y, full catalog visibility, native menu/HUD rectangles and selected
portrait availability. It saves main-menu, Marine, building and pause BMPs
under `/private/tmp/`. The visual review also checks palette remapping.

This is a basic catalog sandbox. Economy, combat, missions, saves, multiplayer,
intro playback, full retail resource/selection states, and race switching are
not implemented. Imported files remain available for that subsequent work.

**Correction (2026-10-08):** the catalog map is still that sandbox. Campaign
and melee CHK maps now run triggers, shared combat and economy, a computer
build order, supply, and a victory/defeat screen. See "Campaign triggers and
results" below. Multiplayer is the shared TCP lobby, not a retail Battle.net
dialog. The paragraph above records the earlier limit.

## Documentation audit (2026-10-08)

The detailed [format reference](SC_FORMATS.md) was checked against the pinned
schemas, current C readers and local data. Additional findings:

- **Confirmed locally:** unfiltered file counts remain 782 installer and 2,897
  native members. Ordinary `rg --files` reports only 771 installer files due
  to ignore rules; use `--hidden --no-ignore` for inventory counts.
- **Confirmed locally:** portdata.dat is 1,080 bytes, giving 90 classic portrait
  entries. Badlands has 1,665 CV5 groups, 4,844 VX4 megatiles and 26,247 VR4
  minitiles. The full 54-column units layout sums to 19,192 classic bytes;
  adding the three expanded byte columns gives the reference's 19,876 bytes.
- **Implementation limitation:** `race` in the imported unit struct is the
  original editor group bitfield and `orders` is the right-click action byte.
  These are not a race enum and complete order program respectively.
- **Disproven overstatement:** “branches supported” is too broad for IScript.
  Only unconditional jumps and calls/returns are followed; conditional jumps
  are skipped, and waitrand deterministically uses its first bound. The earlier
  summary is corrected above.
- **Known discrepancy, not repaired by this documentation change:** the current
  IScript operand-length table differs from both Stargus and PyMS for several
  instructions, including 0x3f, 0x41 and 0x42. Those instructions require 2, 1,
  and 4 operand bytes; the current table gives zero. Paths encountering them
  can decode subsequent bytes incorrectly. A full opcode audit and focused
  fixtures are needed before claiming general animation correctness.
- **Unknown retail behavior:** exact timing, callback semantics, virtual-key
  mapping, damaged wireframe randomization and hover movie restart behavior
  have not been traced in STARCRAFT.EXE. Existing data and visual tests cannot
  substitute for that evidence.

No runtime code was changed by this documentation audit.

## Native CHK terrain and placements (2026-10-08)

**Confirmed cause of the repeated terrain screenshot:** the initial default
`catalog` never read a map. It allocated 128x128 cells and assigned
`32 + (linear_index % 16)` everywhere. Temporary loader diagnostics reproduced
IDs 32, 33, 34 at the beginning of every row. CV5/VX4/VR4 pixel composition was
already correct; changing its stride, palette, or flip direction would have
been a compensating error. The catalog remains an explicit inspection mode.
Single Player and the default check now load Terran 01's extracted CHK.

No executable was disassembled for this change; executable/MPQ fingerprints
are above. Additional SHA-256 inputs:

| Input under `data/STARCRAFT/` | SHA-256 |
| --- | --- |
| `install/campaign/terran/terran01/staredit/scenario.chk` | `4b0e5009c4ae80cfb4cb13ede30842db0aa05bb3be2eedad695fb093fa5786f6` |
| `native/tileset/badlands.cv5` | `b417ddd4523461159fce5357462bb2c9fe5c6f4536b10e0ddd1bdc84c56ae3ee` |
| `native/tileset/badlands.vx4` | `270663f0c17589b61193bdd0fb8a3e9bdac0931f9e92ee3e76373ed3423bd9ec` |
| `native/tileset/badlands.vr4` | `e3e96586ad1dca3678f0d0a0132e7c454b5c701e3dfd2299c93fcc45f036aaa7` |
| `native/tileset/badlands.vf4` | `7bfa246f2bc9e8d9cc1323c968fb0a74c9ab7c7abe41daf110ad82fe05090d89` |
| `native/tileset/badlands.wpe` | `37736f2f738384e0668d7ff8bd658e540d02138e00f660d59b75fb5c5cb5a3ea` |

**Confirmed format chain:** Stargus `src/Chk.cpp` reads `DIM ` as two u16s,
`ERA ` as the tileset selector, and `MTXM` as width*height row-major u16 CV5
indices. Its `TilesetHub.cpp`, `MegaTile.cpp` and Kaitai terrain schemas agree
with the local data. For MTXM value t, read CV5 at
`(t >> 4)*52 + 20 + (t & 15)*2` to obtain the VX4/VF4 megatile index. Both
VX4 and VF4 have 32-byte records; VF4 bit 0 marks each 8x8 minitile walkable.
Do not substitute the editor's `ISOM` or `TILE` chunk for runtime `MTXM`.

PyMS `CHKSectionERA.py` corroborates the low-three-bit tileset selector and
uses native basename `ice` for ERA 6; Stargus's CHK converter calls that era
`arctic`. This disc only supplies eras 0..4, so expansion terrain remains
unverified. The loader selects all terrain resources and the WPE palette from
ERA rather than hardcoding Badlands.

`UNIT` records are 36 bytes: pixel x/y at +4/+6, unit ID +8, valid property
bits +14, owner +16, HP percentage +17. IDs remain native and convert to
engine type ID +1 only when spawning. ID 214 is a start location, not an
actor. OWNR value 6 chooses the local human player. `THG2` records are ten
bytes: ID +0, pixel x/y +2/+4, owner +6, flags +8. PyMS's `CHKSectionTHG2.py`
distinguishes sprite records (flag 0x1000, ID indexes sprites.dat) from unit
records (ID indexes units.dat). Non-sprite THG2 records therefore spawn
ordinary actors; sprite records use the existing level decoration renderer.
GRP crop pivots minus pixel remainders preserve placement within a 32px cell.
Native records are kept in one level-owned blob, released by P_FreeLevel.

Terran 01: 64x64 Badlands, MTXM payload at CHK offset 1158, 8,192 bytes,
1,134 distinct tile references, 49 UNIT records including three starts,
46 actors, and 20 sprite doodads. The human is owner 1, starting at pixel
(128,176), grid (4,5.5). All coordinates stay top-left/Y-down.

Focused native-data verification:

| Map | Dimensions | Tileset | Distinct MTXM IDs | Actors including THG2 | Sprite doodads |
| --- | --- | --- | --- | --- | --- |
| Terran 01 | 64x64 | badlands | 1134 | 46 | 20 |
| Terran 04 | 128x128 | install | 1040 | 120 | 10 |
| Terran 05 | 96x96 | badlands | 1498 | 168 | 14 |
| Zerg 01 | 64x64 | jungle | 810 | 99 | 47 |
| Terran tutorial | 64x64 | platform | 856 | 44 | 16 |
| Zerg 03 | 64x96 | ashworld | 1034 | 129 | 18 |

`test_map` compares every MTXM value, each referenced tile's pixels (including
both flip branches), WPE colors, wholly unwalkable VF4 cells, unit types,
owners, exact fixed-point positions and camera coordinates. It rejects bad
chunk spans, missing MTXM, invalid dimensions and out-of-range tile IDs, and
accepts reordered/unknown chunks. The existing catalog test still checks all
228 unit slots. Reproduce with:

```sh
make
make test-starcraft
env SDL_VIDEODRIVER=dummy build/bin/starcraft --check
env SDL_VIDEODRIVER=dummy build/bin/starcraft \
  --map install/campaign/terran/terran01/staredit/scenario.chk \
  --screenshot /private/tmp/starcraft-terran01.bmp
```

Verification completed: `make` and `make test-starcraft` pass, and all 35
extracted campaign CHKs pass `--check` without sprite-load warnings. The map
test saves `/private/tmp/starcraft-terran01-terrain.bmp`, a 2048x2048 terrain
and sprite-doodad overview without fog or actors. Visual inspection confirmed
continuous native road/cliff/river boundaries and authored bridge tiles;
the ordinary gameplay screenshot also preserves the native player start.

**Explicit engine limits, not claimed retail fidelity:** pathing remains at
32px resolution, with a cell blocked only when all sixteen VF4 minitiles are
unwalkable; mixed cliff/ramp cells need finer pathing. Sprite doodads currently
show their first GRP frame, without IScript animation or enabled/disabled
state evaluation. Creep, doodad special effects, mission triggers, rescue
rules/alliances, resource amounts, shields/energy and full campaign startup
are not implemented. Native UNIT placements are the initial map records;
triggers can change the retail opening state. Fog uses the existing engine
sight rules. SCM/SCX archives still require the existing import tool to extract
`staredit/scenario.chk`; no runtime MPQ dependency was introduced. These
limitations supersede the earlier catalog-only status without asserting that
loading a campaign map makes its mission playable.

## Shared gameplay and single-player flow (2026-10-08)

This section supersedes the initial inspection-only limitations above for basic
orders, harvesting, construction, training and front-end navigation. No new
retail executable disassembly or execution was performed; the executable
fingerprint remains `16cd8f0d1098f8bcd0625d5e13964c2facf2df4874636acb01c2e0bfb062b2f5`.

Additional SHA-256 inputs:

- `native/arr/units.dat`: `dc7b8b59bb14a3ba67829926ad50e8d363032a351299e5087f790ed09616619d`.
- `native/arr/weapons.dat`: `03551df8800a01ec1e99e3648ece6deb83c054588f52967ba76c1d85c92be461`.
- `install/rez/glucmpgn.bin`: `4f10cfbf9444c04be74de9cab223f46254ae76bd8cca99c1484af4c6550fa472`.
- `install/campaign/terran/terran01/staredit/scenario.chk`:
  `4b0e5009c4ae80cfb4cb13ede30842db0aa05bb3be2eedad695fb093fa5786f6`.

### Confirmed asset data

Stargus `src/kaitai/units_dat.ksy` defines the ground weapon in column 17,
build time in column 42 and worker/depot flags at `0x8`/`0x1000`. The weapons
file is 42 bytes per logical record, stored as parallel columns. With N records,
maximum range is u32 at `13*N + 4*id`, damage u16 at `28*N + 2*id`, and cooldown
u8 at `32*N + id`. Marine: 6 damage, 128 pixels, 15 frames; SCV: 5 damage,
10 pixels, 15 frames. Build/cooldown frame counts convert at 24 Hz, following
the existing visual compiler and Stargus `UnitsConverter.cpp`. These values
are extracted offline into `units.inc`, never loaded as runtime balance.
The engine's cell-footprint range rounds pixel ranges up to whole cells.

Stargus worker scripts specify an eight-unit cargo capacity for minerals and
gas. Native CHK UNIT resources are u32 at record +20; minerals are native types
176..178 and geysers 188. Refineries, extractors and assimilators are types
110, 149 and 157. The shared construction path replaces the geyser object,
retains its resource amount and transfers the vent to the completed building.
Raw geysers cannot be gathered. Exhausted mineral patches release their occupied
cells through ordinary deferred mobj removal. Gas buildings remain in place.

Terran 01 has 46 spawned objects. TRIG record 3 starts with condition type 12
(elapsed time), comparator 0 (at least), amount 0. Its Set Resources action is
type 26, player 1, amount 40, resource type 0 (minerals), modifier 7 (set).
The loader applies only direct-player Set Resources actions in unconditional
or zero-elapsed-time initialization triggers. This is not a general trigger
interpreter. The earlier assumption that an empty economy hook implied no
starting economy data is disproven.

The missing campaign dialog is in **install**, not native: `rez/glucmpgn.bin`.
Native assets retain precedence; asset lookup falls back to the installer tree.
The sequence now exposes Registry (`glulogin.bin`), New ID (`glunewch.bin`),
episode selection (`glucmpgn.bin`) and briefing (`glurdyt.bin`) before Start.
Registry names currently last for the session. Saved/custom games are not
implemented. Episode buttons choose their first CHK. The briefing currently
uses the Terran layout; race-specific layouts and playback remain work to do.

MBRF uses 2400-byte records, 16 20-byte conditions and 64 32-byte actions.
Action type is +26 and string ID +4; STR offsets are one-based u16 table entries.
Action 4 supplies objectives and action 8 transmission text. Terran 01's string
13 is Find Raynor / Build a Barracks / Train 10 Marines; strings 15, 16, 17 are
the three transmissions. Text is scrollable; Replay resets its scroll.
Portrait/voice timing, episode title cards and tutorial branching are not yet
played by a briefing interpreter.

Dialog flag `0x10` enables the responsive bounds at +54..60. They are
inclusive left, top, right, bottom (corrected 2026-10-09; an earlier reading
took the latter pair as an extent, which let Start reach 39px over Cancel). Example: briefing Start control 13 has artwork `(417,240,217,240)`,
response `(18,39,202,160)`, text offset `(60,140)`. Using artwork bounds for
hit testing lets Start intercept Cancel. The shared menu now has independent
hit bounds. Newlines in campaign labels must survive decoding. Explicit text
offsets are honored directly rather than adding whole-artwork centering again;
this interpretation is supported by the readable asset composition, but the
retail alignment routine has not been disassembled.

### Confirmed engine defects and implementation consequences

Terran 01's `OWNR` chunk identifies player slot 1 as the human (`consoleplayer`
is set to 1 from owner value 6). The shared single-player `D_PlayerIsHuman`
check previously treated only slot 0 as human. As a result, the generic AI
could assign economy orders to the local player's SCVs on this map. The check
now uses `consoleplayer` in single player and the network player range in
multiplayer. This corrects the observed immediate worker movement at startup.
The CHK `UNIT` records do not encode initial movement orders; workers remain
idle until the player orders them or game behavior gives them a target.

The Terran 01 `STR ` table also contains a `TIP` entry with the opening
selection instructions shown in the user-provided retail screenshot. The HUD
now finds that labeled string in the loaded map and displays it at startup.
The screenshot places the tip near logical screen coordinate (58, 190), which
is the HUD position used here. The precise retail trigger and lifetime for this
generic tip have not been established.

The retail multi-selection screenshot shows small green unit wireframes in the
selection panel. `unit/wirefram/wirefram.grp` has 228 64x64 frames, matching the
228 unit types, while `unit/cmdbtns/cmdicons.grp` has 365 36x34 command glyphs.
`rez/statdata.bin` includes twelve 33x34 selection slots at x=168, 204, 240,
276, 312 and 348, on rows y=396 and 433. The HUD now uses the unit's
`type_id - 1` wireframe frame for each small selection icon, lays the twelve
slots out left-to-right across those native positions, and keeps the command
glyph sheet for the command card. GRP frames store cropped pixels plus their
authored canvas offset; the GRP loader retains it on every frame, and shared
menu drawing applies it at native size or scales it with the frame canvas. This
keeps all GRP-backed menu pictures aligned, including command glyphs in their
36x34 slots and scaled wireframes in the selection list. A single selection
continues to use the full-size native wireframe and stats.

The console item covers 640x480. Temporary diagnostic output demonstrated
`world click consumed=1` with its original settings and `=0` with decorative
pass-through enabled. This was an input dispatch defect, not a sprite definition
or pathfinder defect. A shared menu flag fixes world clicks while retaining the
full native console image. Regression coverage includes actual selection and
right-click command dispatch through the HUD and world responders.

Combat uses ordinary `A_Look`/`A_Chase`/`A_Attack` state actions and the shared
thinker loop, consistent with Doom `p_mobj.c`'s entry-action and spawn contracts.
IScript GroundAttackInit (header slot 2) supplies visual frames. Finite attack
paths return to standing; damage currently occurs on the entry action rather
than interpreting IScript weapon opcodes. Native ground weapon damage/range/
cooldowns are available; independent air weapons, turret weapon routing,
projectiles, armor/shields, spells and many faction rules remain incomplete.
Movement retains the previously explicit three-cells/second engine pacing.

Construction and training use engine product queues, multi-resource payment,
prerequisites, footprint reservations and ordinary mobj spawning. Worker builds
must reach a free perimeter cell before the timer advances. The approach helper
is also used by Warcraft II harvesting, repair, transport and building callers.
Construction art/staged hit points and race-specific worker consumption/release
are not implemented by this shared queue path. Add-ons, hangars and research
leave the queue through the game's release hook (see Unit mechanics below).
Command cards cover worker structures, Terran and Protoss production and the
Zerg morphs (see below). These are
explicit engine capabilities, not claims of complete retail gameplay fidelity.

The initial sprite load already owns all 228 DAT slots. Reloading it after each
production would append another catalog and reset every object's state. Repeated
`R_InitSprites` now reuses the complete cache; a regression checks both count and
state preservation.

### Fog: shared semantics, unverified retail geometry

StarCraft and DC both need unexplored, explored and currently visible regions.
That does not establish identical retail visibility algorithms. The DC native
ray table ends at radius 10, while StarCraft DAT sight reaches 11; its old use
silently granted those units **no sight**. StarCraft now uses the engine radial
FOV path already used by Warcraft II, selected through game data, and retains
its full DAT radius. DC keeps its verified native ray/terrain rules. StarCraft
height, cliff and detector/cloak rules remain unverified. The catalog alone
intentionally reveals the whole map.

**Rendering correction (2026-10-08):** StarCraft had no `draw_fog` callback, so
the driver selected `R_DrawFogTiles`, whose procedural corner masks make
hard-edged tile transitions. The user-provided retail StarCraft screenshot
shows softly interpolated visibility boundaries over explored terrain. As
requested, StarCraft now selects the same `R_DrawFog` compositor used by Dark
Colony. This changes only rendering: StarCraft's sight bits and radial sight
rules remain in place. The screenshot supports the requested soft appearance;
it does not establish that retail StarCraft uses Dark Colony's exact
brightness samples, interpolation, or vision geometry. Those retail details
remain unverified pending executable evidence.

`R_DrawMinimapFog` applies shared visibility and palette darkening to each game's
native minimap terrain before markers. Unexplored is black, explored is dim,
visible is unchanged. Existing `P_VisibleToPlayer` suppresses hidden unit markers.
SC previously drew every marker and all terrain. WC2 previously masked shroud
but left explored terrain fully bright; it now dims explored terrain too. DC
uses the shared palette quantization instead of its separate RGB formula.
The pass converts menu coordinates and clipping to framebuffer pixels before
shading. A temporary diagnostic printed `scale=2 rect=6,348 128x128
screen=1280x960`: applying those logical coordinates directly to the framebuffer
left the minimap unshaded. The regression now checks every pixel in 2x blocks
as well as the unscaled output.
Stargus also explicitly configures minimap fog in `scripts/stratagus.lua`, but
its opacity values are not taken as evidence of Blizzard's renderer.

### Reproduce

```
make build/sc_catalog
build/sc_catalog data/STARCRAFT/native > /private/tmp/sc-units.inc
cmp /private/tmp/sc-units.inc games/starcraft/units.inc
env SDL_VIDEODRIVER=dummy make test-starcraft
env SDL_VIDEODRIVER=dummy build/bin/starcraft --check
env SDL_VIDEODRIVER=dummy build/bin/starcraft --screenshot /private/tmp/starcraft.bmp
env SDL_VIDEODRIVER=dummy build/bin/starcraft --map install/campaign/terran/terran01/staredit/scenario.chk --screenshot /private/tmp/starcraft-game.bmp
```

`test_play` checks hauling/depletion, refinery/gas, costs, worker approach and
construction, training, attack/move commands, radius-11 sight and minimap fog.
`test_native` checks the real input route, menu sequence, native briefing strings,
repeat sprite initialization and screenshots under `/private/tmp/starcraft-*`.
Shared `test_menu_items` covers decorative input and responsive rectangles;
`test_minimap_fog` covers shroud, dimming, visibility, clipping and bounds.

Verification: `make all`, all three StarCraft test executables, and 17 focused
Warcraft II / Dark Colony / 7th Legion / KKnD / Dark Reign regressions passed.
The broader suites also exposed Warcraft II navigation, Dark Colony Human01
corner movement and LAN/menu failures. The same failures were reproduced in
an untouched `git archive HEAD` checkout; they are not claimed as repaired here.

## Campaign triggers and results (2026-10-08)

Stargus `src/Chk.cpp` `SaveTrigger` writes each TRIG record out as a Lua
comment. It does not execute it. `scripts/stratagus.lua` `SinglePlayerTriggers`
instead ends a game when the local player has no units, or when
`GetNumOpponents` is zero. `scripts/menus/results.lua` is the victory, defeat
and draw screen; Continue leaves that screen. `scripts/campaigns/campaign.lua`
advances `campaign_position` after a victory. The Terran campaign list is the
tutorial followed by numbered maps 01 through 12.

**Confirmed:** every install campaign `scenario.chk` has zero action 41
(Set Next Scenario). The next mission is the next numbered file in that race's
campaign directory. `tutorial` is followed by `<race>01`. `<race>12` has no
successor. A custom string from action 41 is kept and preferred when that
path can be opened, but the retail campaign files do not contain one.

**Confirmed trigger record, from PyMS `FileFormats/TRG.py` and the CHK
reader:** 2400 bytes. Sixteen 20-byte conditions, then sixty-four 32-byte
actions at offset 320, four unused bytes at 2368, and a 28-byte execution
mask at 2372. A mask byte of 1 means the trigger runs for that group. Groups
are players 0–7, current 12, foes 13, allies 14, neutral 15, all 16, and
forces 1–4 at 17–20. Condition byte 15 is the type and byte 14 is the
comparison. Action byte 26 is the type and byte 27 is the modifier. Disabled
is flags bit 1. Comparisons used by the campaign files are 0 (at least),
1 (at most), 2 (switch set), 3 (switch cleared) and 10 (exactly). Resource
modifiers are 7 set, 8 add and 9 subtract. Transmission and Wait times are
milliseconds. Countdown and elapsed quantities are seconds.

**Confirmed outcome rule:** Victory (action 1) and Defeat (action 2) are
results for the local human, including when the trigger's mask is another
force. Terran 01's win runs for Force 2 and requires player 2 to command at
least ten Marines, one Barracks and Jim Raynor (unit 19). Its losses also run
for Force 2. Terran 05's loss condition counts Foes relative to that force and
still means the human lost. Draw (action 58) is the third result. A map that
contains a Victory action does not also end when one side is wiped. A map
with no Victory action uses the Stargus rule: the human loses with no units,
and wins when every opponent slot is empty. Opponent slots are OWNR 1, 2, 5
and 6, excluding the human and allies. Inactive (0), rescue (3) and neutral
(7) are not opponents. No opponent slots means a lone human does not win on
the first tick.

**Confirmed alliances:** FORC is 20 bytes. `force[8]` is at 0 and `flags[4]`
is at 16. Flag bit 2 (allied) writes `sight.allies` and sets `player_teams`.
Without `player_teams`, every computer enemy is treated as allied to the
others. Terran 01's force flags are all zero, so the human in slot 1 is not
allied with the computer in slot 4. Flag bit 8 (shared vision) does not by
itself ally the players. Rescue is neutral and does not receive the build AI.
OWNR 1 and 5 do. Run AI Script (actions 15 and 16) also marks that player as
a computer. The retail AI bytecode is not executed.

**Confirmed UPRP:** 64 records of 20 bytes. Special-valid and data-valid are
u16s, then player, hit-point percent, shield percent, energy percent, resource
u32, hangar u16 and state u16. Applied bits are owner, hit points, invincible
(special bit 16 with state bit 16) and hallucination (special bit 8 with
state bit 8). Burrow, cloak, shields, energy, hangar and resource amounts are
stored and not simulated.

**Confirmed supply:** `units.dat` columns 45 and 46 are bytes in halves
(2 is one supply). The committed `SC_UNIT` rows carry both bytes. The cap is
200 supply, which is 400 halves. Queued training counts. A depot or command
center has no supply cost, so it can still be built at the cap. `units.inc`
gained these two columns; an older byte-for-byte `cmp` against a pre-supply
`units.inc` no longer applies.

**Inferred presentation:** Pause and Unpause (actions 5 and 6) are ignored.
The shared pause flag skips both the world tick and the mission tick, so an
Unpause inside a trigger could never run. Leaderboard actions and doodad
state are ignored. Transmission text is shown; its WAV is not played.
Center View requests a camera move, which the driver applies after the
mission tick. Objectives replace the map's start tip on the HUD. Minerals,
gas and supply are the resource line.

Zerg production morphs in place (`games/starcraft/p_zerg.c`, `p_prod.c`).
A Hatchery, Lair or Hive grows a larva every 342 frames up to three; each
larva is its own producer. An order turns it into an Egg (units.dat 36) that
carries the order, reserves its supply and hatches into the unit, two for
units with flag 0x400 (Zergling, Scourge) at one price. A killed egg loses
the order. A Mutalisk becomes a Cocoon and then a Guardian. Hatchery to
Lair to Hive, Spire to Greater Spire and Creep Colony to Sunken or Spore
Colony morph the building itself: it works and counts as the old type until
the morph ends, then keeps its share of hit points. A Lair or Hive satisfies
"Hatchery" and a Greater Spire "Spire". The hatchery's command card shows
what its larvae can become and gives the order to a free larva.

Buildings with units.dat flag 0x20000 must stand wholly on creep and those
with 0x80000 must have their centre in one of the owner's psi fields. Creep
is a fixed radius of 10 cells around a Hatchery tier and 5 around a colony,
present at once and gone with its source; retail creep grows and recedes
tile by tile, and map-placed creep is not read. The psi field is OpenBW's
16 by 10 cell mask around a Pylon. A Protoss building outside power keeps
its order but does not release it until power returns. Creep is not drawn.
Nydus Canals can be built but do not transport.

The computer build order is a short race ladder (workers, supply, production
buildings, the first combat unit, gas), not the scenario's AI script.
Burrow is not simulated.

The result screen is a modal menu. Victory with a following campaign file
offers Continue, which opens that mission's briefing and then loads it.
Restart reloads the current map (`install/` kept relative to the data root,
an absolute path kept whole). Defeat offers Restart and Quit. Draw, the last
campaign mission, a multiplayer game, and a victory with no next file end
the session. Escape does not resume play. A multiplayer result hides Restart.

### Reproduce

```
make build/sc_catalog
build/sc_catalog data/STARCRAFT/native > games/starcraft/units.inc
env SDL_VIDEODRIVER=dummy make test-starcraft
```

`test_mission` checks one-shot Victory, a non-preserved resource add, melee
elimination, a lone player, Terran 01's starting minerals and alliances, the
200-supply cap, and numbered campaign advance. `test_play` still requires the
load-time mineral set of 40 before any mission tick.

## Multiplayer lobby (2026-10-08)

Superseded for the screen flow by "Native multiplayer screens (2026-10-09)"
below. The CHK list, SIDE, OWNR and melee-spawn notes in this section stay
in force. The paragraph that follows describes the previous front end: id 4
opened `hud/m_net.c`, and an empty map list said that no maps were found.

No executable was disassembled for this. `glumain.bin` control id 4
(Multiplayer) opens the engine lobby in `hud/m_net.c`. The game supplies the
map list, the three race names, and a commit callback that stores the lobby
races. Browse, join, host, chat, seats, ready and launch stay in the engine.
Retail `gluconn`, `glujoin`, `gluchat` and the create/custom/load dialogs are
not opened. Modem and Battle.net are out of scope.

**Confirmed from the installed files:** every `install/multimaps/*.scm` begins
with the MPQ signature. The runtime does not parse MPQ; `tools/sc_import`
remains the way to extract `scenario.chk`. The lobby lists `.chk` files under
the data root, skips directory names `campaign` and names that start with `.`,
and keeps at most 64 maps. `scenario.chk` is titled from the folder above a
parent named `staredit` (so `mapname/staredit/scenario.chk` is "mapname"). Any
other `.chk` uses its filename without the extension. The player cap is the
count of OWNR bytes 5 or 6 in the first eight of the first OWNR chunk when
that count is at least 2; otherwise a `(2)` through `(8)` prefix on the
filename or the title. Maps below two players are omitted. With no extracted
multiplayer CHK, Create stays disabled and the screen says no maps were found.
The 35 campaign `scenario.chk` files stay out of that list.

**Confirmed SIDE bytes** on the three episode-01 campaign maps: the human slot
(OWNR 6) is Terran 1 on Terran 01, Zerg 0 on Zerg 01, and Protoss 2 on Protoss
01. Lobby index 0 is Terran, 1 is Zerg, 2 is Protoss. An unset race is stored
as Terran.

**Explicit engine behavior on a network load** (`netgame`, at least two
players): the first OWNR chunk's playable slots (5 computer or 6 human), in
slot order, are moved onto seats 0..n-1, where n is the smaller of that count
and `doomcom->numplayers`. OWNR, SIDE and the first eight FORC bytes are
permuted with those slots. Seats 0..n-1 become human (OWNR 6) and take the
lobby SIDE byte. Extra playable slots become neutral (OWNR 0). UNIT and THG2
owners below 8 follow the same permutation. TRIG Set Resources actions (byte
26 equals 26, player below 8) follow it too. Player group 13 is left as 13.
Single-player loads do not permute, and they clear a previous lobby race list.
Continuing from a campaign briefing clears that list as well.
`consoleplayer` in a network game stays the seat assigned before the map loads.

A network seat whose only placement is a start location (UNIT or THG2 id 214)
receives that race's town hall and four workers: Command Center 106 and SCV 7,
Hatchery 131 and Drone 41, or Nexus 154 and Probe 64. Those buildings are
128 by 96 in `units.inc` and have the building bit set, so the existing spawn
centres the footprint on the start pixel. The workers are placed on the row
below that pixel. This is not traced to an executable address. `D_CheckNetGame` rejects a seat
with no selectable unit, and a start location alone does not spawn an actor.
Campaign maps already place units on those slots, so this fill does not run
for them. No melee `scenario.chk` is in the tree; the `.scm` archives are not
opened at runtime. A network seat whose minerals are still 0 after
the initial Set Resources pass receives 50. Maps that already set minerals
keep that amount. Group 13 resource actions are not applied by the loader, so
the 50 is written on the seat directly.

## Native multiplayer screens (2026-10-09)

No executable was disassembled. `glumain.bin` control id 4 (Multiplayer) now
opens the retail dialog chain instead of `hud/m_net.c`. The session under
those screens is still the engine TCP lobby: browse, join, host, seats, ready
and launch. Other games keep `hud/m_net.c`.

**Confirmed from the dialog files.** `native/rez/gluconn.bin` is 640×480 with
11 controls. List id 5 is the provider list. This build puts one row in it,
the retail LAN label "IPX network" (`rez/gluall.tbl` index 94). Id 6 is that
same label, id 7 is "Supports up to 8 players" (index 100 with `%d` = 8), and
id 8 is the local-area-network paragraph (index 101), not the IPX-install
paragraph (index 98). Id 9 is Ok and id 10 is Cancel. Modem, direct cable and
Battle.net are not listed. Calling that row "IPX network" while the socket
under it is TCP is engine behaviour, the same stand-in Warcraft II uses. It
is not a traced `STARCRAFT.EXE` choice.

`native/rez/glujoin.bin` (21 controls) is the game list. Id 5 lists advertised
names only. Id 13 joins the selected game's address and stays on this screen
until the lobby is up, then opens chat. Id 15 is Create Game. Id 14 is Cancel
and returns to the connection screen. Id 6 carries a connection error. The
detail lines are id 7 title, id 8 "Melee", id 10 map, id 11 size (`gluall.tbl`
index 31, `%ux%u`) and id 12 "Normal" (index 84). There is no address field
and no "Searching the LAN..." line.

`install/rez/glucreat.bin` (25 controls) is Create Game. Id 5 is the map list.
Id 17 is a one-row Melee dropdown. `native/templates/templates.lst` names
`Melee(1).got` among the scenario templates. Subtype id 14, the empty id 15,
slider id 16, subtype dropdown id 18, and the "-" / "+" labels are hidden.
Map Settings, Greed, Slaughter and the other types are not offered. Id 19
`glue\create\pSlots.pcx` stays not visible; the file is
`install/glue/create/pslots.pcx`. Id 12 Ok is enabled only when a map is
selected and opens `native/rez/glupedit.bin` (360×200, centred on 640×480).
That dialog's id 2 asks "Please enter a game name to continue." (`gluall.tbl`
index 140). Id 4 is the name, at most 31 characters because the session name
is 32 bytes, prefilled with the map title. An empty name does not host. Id 3
returns to create without hosting. The game-name default is the map title,
not a traced "Player's game".

The create screen reads one CHK pass for the selected map: DIM width and
height, ERA masked with 7, SPRP name and description through STR (the same
offset rule as the mission tip reader), OWNR and SIDE. Tileset labels for
ERA 0–4 are `gluall.tbl` indices 38–42 (Badlands, Space, Installation,
Ashworld, Jungle). ERA 5–7 are labelled Desert, Ice and Twilight; those three
names are not in that table. Playable OWNR bytes (5 computer or 6 human) in
the first eight of the first OWNR chunk, in slot order, supply the seat
races. SIDE 0 becomes lobby Zerg (1), SIDE 2 becomes Protoss (2), and every
other byte, including user-select 5 and random 6, becomes Terran (0). The
hosted player count is that playable count clamped to 2..8. There is no
close-slot call, so the count cannot be reduced after the game is published.
With no extracted multiplayer CHK, Ok stays disabled and the description line
stays empty. `.scm` archives are still not opened.

**Correction (2026-10-09), bundled multiplayer map import:** the empty Create
screen was an import omission, not an absent map-list widget. Before the fix,
temporary `OPEN_RTS_DEBUG_SC_MAPS` logging reported zero extracted maps under
`data/STARCRAFT`; the only CHKs there were excluded campaign missions. The
62 installed `install/multimaps/**/*.scm` archives had not been opened by the
import workflow. `sc_import --maps` now walks that directory and uses StormLib
to extract each archive's exact `staredit\\scenario.chk` member to
`maps/<relative SCM name>/staredit/scenario.chk`. The `.scm` filename remains
in the directory name, so the Create list can show filenames independently of
the CHK's SPRP title. The source archives remain intact. The runtime continues
to read native CHKs and has no MPQ dependency. SCX files use the same member
lookup, but this classic disc contains only SCM inputs; SCX import is unverified
against a retail expansion map.

**Confirmed native data:** all 62 archives extract successfully. Excluding the
five `campaign/` maps leaves 57 playable entries in the current Create list;
the list remains flat, including the `ladder/` and `scenario/` subdirectories.
The `(2)road war.scm` CHK has SPRP title `Road War`, description containing
`Suggested Players: 2` and `Battle on the highway.`, DIM 128×128, ERA 0
(Badlands), and two playable OWNR slots. SHA-256 of that SCM is
`25a43d4b28bbc5a3e1d7f00f51353b4870b6541bc7be27a78c92b004aba6d190`;
its extracted CHK is
`f2976dc0cff5138b9ac4f67c7ff1967ad721381e808700541482bcef9df948a5`.
`native/rez/gluall.tbl` also contains `Map Size:%c%s`, `Tileset:%c%s` and
`Number of Players:%c%s`; the Create detail lines now include those labels.

The user's retail reference screenshot (`Screenshot 2026-10-09 at 13.25.28.jpg`)
corroborates filenames on the left and the Road War details on the right.
It also shows directory/file icons, a scrollbar, Free For All and local player
settings. Those controls and the translucent panel treatment are not reproduced
by this map-import change; the current network Create screen offers Melee and
uses the existing native dialog artwork. No executable was examined for this
correction, so it establishes asset contents and engine behavior only.

`make starcraft-unpack`, `make starcraft` and `make test-starcraft` now import
the installed maps; `make starcraft-maps` imports them without re-extracting
the disc. With no CHKs available, Create explains how to import them and keeps
Ok disabled. `test_native` now requires all 57 bundled non-campaign entries,
selects `(2)road war.scm`, checks every detail and the game-name default, and
writes `/private/tmp/starcraft-create-road-war.bmp`. Previously its conditional
map check accepted an empty list, which concealed this omission.

Reproduce with `make starcraft-maps && make test-starcraft`, then
`env SDL_VIDEODRIVER=dummy build/bin/starcraft --check --map 'maps/(2)road war.scm/staredit/scenario.chk'`.

**Correction (2026-10-09), Create scrollbar and dropdown chrome:** populating
the list did not supply the type-12 scrollbar or the type-13 combobox artwork.
Those are separate pictures in `native/glue/palnl/dlg.grp`, not baked into
`install/glue/create/plistmap.pcx`. The scrollbar and closed dropdown outline
and arrow are now rendered from that GRP. Icons, directory browsing, other
game modes and translucent panels remain outside this correction.

**Confirmed asset metadata:** `dlg.grp` SHA-256 is
`446ab6727c423989be92747c4f7385eb1613d2a53291e0b98398200c2e39b212`.
It contains 139 frames on a 256×128 canvas. Temporary
`OPEN_RTS_DEBUG_SC_WIDGETS` logging recorded these tight frame dimensions:

| Zero-based frames | PyMS names / artwork | Dimensions |
| --- | --- | --- |
| 16, 17, 18 | scroll up disabled, normal, pressed | 16×16 |
| 19, 20, 21 | scroll down disabled, normal, pressed | 16×16 |
| 28 | scrollbar thumb | 14×14, displacement (1,1) |
| 29, 30, 31 | vertical track top, middle, bottom | 16×8 |
| 50, 51, 52 | combobox arrow normal, hover, disabled | 13×7 |
| 53, 54, 55 | combobox left, middle, right | 16×16 |
| 56, 57, 58 | combobox left, middle, right hover | 16×16 |

The disabled down frame has displacement (0,2); the other listed arrow frames
have displacement (0,0). The source records are retained unchanged by the
GRP loader. Widget pictures use their tight image rectangles, as PyMS's
`PyBIN/PyBIN.py::dialog_asset` does, rather than stretching the GRP canvas.
Their palette comes from `glue/palnl/backgnd.pcx`.

**Corroborating source and implementation choice:** pinned PyMS
`FileFormats/DialogBIN.py` names those frame indices.
`PyBIN/WidgetNode.py::update_dialog` places a vertical scrollbar inside the
list's right edge, with two pixels between each arrow and the track, and
centres the combobox artwork vertically with its arrow five pixels inside
the right edge. open-rts follows that editor preview layout and the user's
retail reference screenshot. Those two spacing values are confirmed in the
reference source; their exact retail executable instructions remain unknown.
No executable was examined for this correction. PyMS source is read only;
no Python tool was added or executed.

Create id 5 keeps its original (60,130,288,118) control extent, reserving the
rightmost native arrow width for three added shared menu items: two linked
step buttons and an `MI_SCROLLBAR`. The list's content width is 272; arrow
clicks, wheel/keyboard scrolling and thumb dragging remain shared engine
behavior. Native track and combobox pieces use the menu picture callback;
the engine draws and positions the thumb. The dropdown's font uses the button
ramp, while its normal/hover border and normal/hover/disabled arrow use the
authored frames directly. Text placement inside the border is not traced to
retail instructions; its existing native text offset is retained.

Verification: `test_native` compares rendered nontransparent pixels for the
normal and pressed scrollbar arrows, dropdown border rims, and normal/hover
dropdown arrows against the source GRP. It clicks both arrows, drags to the
last page without changing selection, selects Road War with the keyboard,
and opens/accepts the dropdown. It writes Create screenshots at 640×480 and
1280×960 to `/private/tmp/starcraft-create-road-war{,-scaled}.bmp`.
Run `make test-starcraft` to reproduce.

**User-requested text inset (2026-10-09):** add four logical pixels to the
native horizontal text offset for type-8 text inputs and type-13 dropdowns.
This is an explicit presentation choice, not a newly traced retail constant.
It covers the player-name, game-name and chat inputs and the Game Type
dropdown, including its popup rows. Native vertical offsets and control
rectangles are retained. `test_native` also writes `starcraft-new-player.bmp`
and `starcraft-game-name.bmp` under `/private/tmp/` for visual verification.

`native/rez/gluchat.bin` has 79 controls, which is why the dialog adapter now
keeps 96 controls rather than 64. Id 6 Ok toggles ready; its text stays "Ok".
Ready names use font ramp 1 (the button ramp; corrected to the screen's
`glue/palnl/tfont.pcx` by the complete glue audit below).
Id 9 is the chat field and id 10 is the log. Id 8 is the 1×1 Send button.
Slots are ids 28+i*4 (name) and 29+i*4 (race) for i in 0..7, shown only for
seats inside `doomcom->numplayers`. An empty seat says "Open" (`gluall.tbl`
index 129). The local name is the registry name. A remote joined seat is
"Player %d". The local race is a button and calls the lobby race cycle;
other seats are not. Race names are `rez/network.tbl` indices 7, 6 and 8
(Terran, Zerg, Protoss), which is lobby order 0, 1, 2. Ids 27+i*4 and
30+i*4, the rows from id 59 up, the password, the observer lines and the
"Starting in" countdown stay at their native hidden flags. `gluchatpopup.bin`
is not used.

Backgrounds are `glue/palnl/backgnd.pcx`. The panel PCX files are the type-5
children of each dialog (`glue/selconn`, `glue/gamesel`, `glue/create`,
`glue/chatroom`, and `glue/PalNl`).

**Still unknown**, because no function in `STARCRAFT.EXE` was traced: the
retail control callbacks, whether Ok's label changes when a player is ready,
the countdown, closed and computer slots, game-type parameters, and the
subtype control. Random is not a fourth lobby race.

### Reproduce

```
env SDL_VIDEODRIVER=dummy make test-starcraft
```

`tests/starcraft/test_native.c` walks Single Player through the briefing,
then Multiplayer through connection, the game list and create, and back to
the main menu, without hosting. It requires `gluchat.bin` to decode to 79
controls. Screenshots of the three lobby screens are written under
`/private/tmp`.

## Runtime identifier audit (2026-10-09)

This is an implementation audit against the committed native DAT catalog
`games/starcraft/units.inc`, not new executable analysis or a new Stargus source
comparison. Native fingerprints and reference provenance above are unchanged.
No Lua is loaded by the game.

The existing Stargus-referenced command-card recipes and AI plans already used
numbers rather than script-name lookup, but their bare unit IDs hid which
types a row meant. Seventy-two gameplay types now have named MT_* enum values
in the existing info.h. As with mobjinfo and Warcraft II, each value is the
native units.dat/CHK ID plus one, with MT_NONE zero. The other native slots
remain loadable; this does not restrict the 228-type catalog.

Production recipes now carry typed actor IDs for the product, maker and
prerequisite. Catalog and icon access subtract one at the native-index boundary;
product/actor IDs pass through unchanged. The 65 resulting recipe triples and
27 AI product/count steps were compared with the previous numeric tables and
are identical. Resource classification, gas-building replacement, melee
start buildings/workers, Start Location handling and the SCV command-card
selection use the same named IDs. CHK decoding still reads native IDs, and
sc_spawn_actor performs the existing conversion to engine actor IDs.

The remaining string comparisons handle actual paths, native chunk tags,
mission text and the explicit catalog input. Unit labels come from the native
stat_txt.tbl-derived C catalog; sc-NNN keys serve the shared sprite cache.
This audit found no Stargus script-name gameplay dispatch to retain or port.

Verification: all four StarCraft test binaries pass (map, mission, native,
play), including 228 catalog entries, 148 unique GRPs and 7,667 decoded native
frames. The first Terran mission smoke check still loads 46 units, 20
decorations, 10 resource vents and 2,079 visual states.
The in-game BMP is byte-identical to the baseline with the previous numeric
unit IDs. No catalog values, native asset pixels or animation timing changed.

Run:

```sh
env SDL_VIDEODRIVER=dummy make test-starcraft
env SDL_VIDEODRIVER=dummy build/bin/starcraft --check
```

## Complete glue asset and shortcut audit (2026-10-09)

This is an asset/runtime audit, not executable disassembly. The original EXE
fingerprint remains `16cd8f0d1098f8bcd0625d5e13964c2facf2df4874636acb01c2e0bfb062b2f5`.
No new retail function addresses are claimed. Sources are the original-disc
files above, pinned PyMS `DialogBIN.py`, `WidgetNode.py`, `FNT.py`, and the
pinned libsmacker decoder; see `REFERENCES.md` for provenance. The user's two
Create-screen images show the missing scrollbar, dropdown outline and arrow.
They establish the requested presentation, not the meanings of unknown BIN
fields or atlas frames.

### Confirmed complete resource coverage

`tests/starcraft/test_glue.c` walks both `native/glue` and `install/glue`,
using native-first precedence, and decodes every file. There are no other
extensions in these trees. All frames are decoded, including localized and
inactive-screen movies. Loading support does not mean every screen's game
logic is implemented or that every file should be resident simultaneously.

| Format | Files | Coverage |
| --- | ---: | --- |
| PCX | 331 | Indexed pixels, native dimensions and embedded palette |
| GRP | 44 | All 2,176 original frames, tight extents and displacement |
| SMK | 53 | All 1,758 indexed frames and per-frame palettes |
| Classic dialog BIN in `rez/` | 76 | 1,049 children; maximum 79 in `gluchat.bin` |

Observed control counts by type 1..14 are respectively
29, 106, 16, 50, 72, 10, 0, 9, 465, 161, 30, 13, 21, 67.
Type 7 is not present in this installation. The existing 96-control/four-movie
caps accept every observed dialog; they remain engine bounds, not retail limits.

The twelve complete themes are `palmm`, `palcs`, `palrt`, `palrz`, `palrp`,
`paltd`, `paltv`, `palzd`, `palzv`, `palpd`, `palpv`, `palnl`. Each has
`dlg.grp` (139 frames, 256×128 canvas), `tile.grp` (nine 8×8 frames), and
`arrow.grp` (five frames, 128×128 canvas). The other eight GRPs are six
`score{td,tv,zd,zv,pd,pv}/iscore.grp` (54 frames each), `chatroom/ichat.grp`
(11), and `create/icreate.grp` (5). These retain native frame numbers; they
are not deduplicated or converted into replacement artwork.

PCX families include screen panels, the Battle.net artwork, race briefing and
score panels, title images, theme backgrounds, popup pictures, `tfont` and
`teffect` lookup images. Auxiliary `pal{t,z,p}{a,b,c}` contain three PCXs each;
they are not complete dialog themes. Theme `tfont.pcx` files are 48×1;
`title/tfont.pcx` is 40×1. PyMS names the first five eight-pixel font ramps.
The adapter interprets those five; the sixth theme block's role is unknown.
`palcs/arrhot` is a PCX, not a second SMK.

### `glue/palnl/dlg.grp` working frame map

SHA-256: `446ab6727c423989be92747c4f7385eb1613d2a53291e0b98398200c2e39b212`.
The following meanings come from pinned PyMS's named constants and agree
with the original frame artwork. Unnamed ranges stay unknown.

| Frames | Role |
| --- | --- |
| 0–5 | Blank and terrain HUD buttons, disabled/normal/pressed |
| 6–10 | Radio: disabled, off, off pressed, on, on pressed |
| 11–15 | Checkbox: disabled, off, off pressed, on, on pressed |
| 16–27 | Up/down/left/right arrows, each disabled/normal/pressed |
| 28 | Scroll thumb, 14×14, displacement (1,1) |
| 29–34 | Vertical top/middle/bottom and horizontal left/middle/right track |
| 35–43 | Dropdown popup nine-piece panel |
| 44–49 | Combo pieces while dropped upward/downward |
| 50–52 | Closed combo arrow: normal, hover, disabled |
| 53–58 | Closed combo left/middle/right, normal and hover |
| 59–82 | Unknown; visually alternate white/blue combo artwork |
| 83–85 | Alliances HUD button states |
| 86–90 | Unknown icon roles |
| 91–98 | Slider disabled/normal left/middle/right and stop artwork |
| 99–102 | Slider dot: disabled, yellow, green, red |
| 103–129 | Left/center/right button styles, disabled/normal/pressed, three pieces each |
| 130–132 | Messaging HUD button states |
| 133–138 | Unknown; red outline and solid three-piece artwork |

The GRP header canvas is not a control's visible extent. Compose widgets
from their tight native frame rectangles. The numbered contact sheet keeps
each raw frame separate; it does not infer new states or recolor them.
`palnl/tile.grp` SHA-256 is
`ec3f606893190763212b5242e09574edfb4201c6437bd5f08a64c119e16d4a36`;
its frames run top-left to bottom-right in a 3×3 grid.

### Shortcuts found and corrected

- Every front-end screen used `palmm/tfont.pcx`. Themes now own their native
  background palette, widget atlas, tile atlas and four fonts, and screens
  borrow these resources. `sc_font_colors` now uses native/install lookup,
  so the installer-only `palcs` theme works. The main menu uses the same
  loading path instead of duplicating image/movie setup.
- Only Create had a scrollbar, manually appended in multiplayer wiring.
  Every loaded native type-12 list now gets the native track, arrows and
  thumb through the dialog adapter. Shared menu code still owns input.
- `as_list` and `as_drop` inserted the guessed color `0xff203040` as a
  selection/focus fill. This was not native evidence and is removed.
  Selected list labels use authored font ramps instead.
- Row spacing added two guessed pixels to the font height; the registry
  separately hardcoded 20, and lists replaced the native text offset with 2.
  These overrides are removed. Rows use the FONT header's native height,
  and lists retain the BIN text offset. `font16.fnt` header bytes 4..7 are
  32,255,17,19; `font14.fnt` has 32,255,15,16. The 118-pixel Create list now
  fits six 19-pixel rows, agreeing with six visible rows in the user's
  original-game reference. The +2 rule previously fitted only five. PyMS
  `StringPreview.get_positions` likewise advances lines by font height;
  exact retail list-spacing instructions are still not traced.
- Types 3, 4 and 6 were plain static text, and 8/12/13 were patched into
  widgets by individual screens. Known glue types now map during loading.
  Types 1/2 use native center-style button pieces; type 14 remains a distinct
  highlighted/movie button. Radio/check artwork uses authored on/off and
  pressed/disabled frames. Slider chrome uses the native bar and dot, but
  game callbacks must supply its range; no retail slider limits are invented.
  Current observed radio dialogs each contain one exclusive group; assigning
  that group is an adapter policy, not a decoded group-number field.
- New-player and game-name dialogs had separate hardcoded 220×100 and 360×200
  centering loops and no panel. The loader derives placement from the BIN
  root and composes the native `tile.grp` panel as PyMS does. Centering small
  glue roots is an engine presentation policy inferred from their zero root
  origin, not traced retail placement. The exact retail choice between tile
  art and the theme's `p*popup.pcx` pictures is still unknown.
- Chat replaced a native list with static prose, and refreshed its scroll
  position every frame. It now uses the shared list with wrapped-line count,
  a native scrollbar, and follows the end only when new text arrives. Shared
  list rendering honors the prose scroll position and font palette.

The explicit user-requested four-pixel textbox/dropdown inset is preserved.
The prior 2-pixel track clearance and 5-pixel combo-arrow inset are backed by
the PyMS preview described above, not newly verified retail instructions.
The underscore editing caret and game/player-name character limits remain engine choices.
They must not be cited as discovered original-game constants.

### Decoder defect exposed by the complete scan

`install/glue/palcs/arrow.smk`, SHA-256
`a4be4faf6e197e4ae3a0ec078fe8b897baac780a23b02a56b5a83e81bc9c5ed8`,
failed although the 52 previously encountered movies decoded. Gated diagnostic
logging showed Huffman capacities/populated counts 7/7, **2/1**, 81/81, 45/45.
The old `limit == tree.size` check rejected a complete one-node tree because
the header allocated two nodes. The FFmpeg primary decoder likewise checks
capacity rather than exact filling. Removing that equality preserves bounds
and end-bit validation. All five 32×32 frames at 100 ms now decode, with
concatenated indexed-pixel FNV-1a `82fe4404e591d1f9`.

Error cleanup also asserted when freeing a not-yet-allocated video buffer.
NULL cleanup now follows C `free` semantics; a 104-byte truncated copy returns
failure without aborting. The corrected LGPL decoder and its license are
tracked in `third_party/libsmacker`, so rebuilding no longer silently relies
on an ignored reference-tree edit. Temporary diagnostic logging was removed.

### Explicit remaining unknowns and limits

Decoding all assets is verified. Full retail glue behavior is not. Unknown
BIN flags/fields, exact responsive/virtual-key semantics, transparency
`0x2000`, translucency `0x40000`, and `teffect.pcx` rendering remain unresolved.
Movie fade/dark flags, audio and original start-time scheduling are not
implemented. Embedded font color commands are still stripped rather than
fully interpreted. Dropdown popup artwork 35–49 is decoded and catalogued,
but open-popup composition still uses the shared menu list, not retail chrome.
`arrow.grp`, `icreate.grp`, `ichat.grp` and score icons decode completely;
not all are connected to screen behavior. Sliders' discrete stops/ranges and
unused dialog callbacks require further evidence. Race-specific briefing,
score, Battle.net, modem and other inactive screens remain outside the mapped
front-end flow. The current briefing always uses Terran art, and lobby race
selection cycles a button rather than opening the original popup; these are
known feature gaps. The synthetic in-game result menu is an engine screen,
not the native score screen. None of these gaps is hidden by replacement
PNG assets, guessed alpha values, renamed atlas states or pixel tuning.

### Reproduce

```sh
make build/bin/tests/starcraft/test_glue
env SDL_VIDEODRIVER=dummy build/bin/tests/starcraft/test_glue
env SDL_VIDEODRIVER=dummy make test-starcraft
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_menu_items
```

The C audit emits `/private/tmp/starcraft-glue-assets.tsv` with every file's
decoded dimensions/frame count/pixel hash and every GRP frame/control layout,
plus `/private/tmp/starcraft-glue-dlg.bmp`, a numbered native contact sheet.
`test_native` checks theme-specific font pixels including installer-only
`palcs`, native scrollbar/dropdown pictures and interaction, and exports menu
screenshots at 640×480 and 1280×960. The shared menu test verifies that wrapped
log content actually moves with its scroll position.

### Games browser versus map selection (2026-10-09)

**Superseded navigation advice:** this paragraph records the earlier adapter
flow. The map-first behavior requested by the user is implemented below.

The user's later screenshot shows `glujoin.bin`, titled Games, rather than
`glucreat.bin`, titled Create. Runtime id 5 on Games reads advertised sessions
from `I_NetGames`; id 15, Create Game, opens the separate map selector. Gated
diagnostics on the same navigation path report zero hosted sessions and 57
maps under `data/STARCRAFT`. This is not a map-import failure. The empty Games
list now says "No hosted games found. Select Create Game to choose a map."
This is an intentional engine usability message, not a discovered retail
string. The native navigation regression verifies the empty message and the
57-map Create list. Temporary diagnostics were removed after verification.

### User-requested map selection before Create Game (2026-10-09)

**Superseded intermediate flow:** the subsequent user clarification removes
both the second map selector and the game-name dialog, as recorded below.

The user clarified that maps must already be visible before pressing Create
Game. `glujoin.bin` now lists the local map catalog alongside advertised
sessions: session rows first, followed by map filenames. A local map selection
fills the native detail fields with its title, Melee, filename, dimensions
and Normal speed. Create Game is enabled for a local map; Ok is enabled for
an advertised session. Activating a local map or Create Game opens the native
Create setup with that same map selected and visible, then its Ok proceeds
to the existing name dialog. This is explicitly requested engine navigation,
not a newly traced retail callback sequence.

The browser scans maps on entry, independent of whether discovery finds
sessions. Discovery refreshes preserve a selected local map and its viewport
as session rows appear or disappear. Remove the previous empty-state advice
to press Create Game before choosing a map; the empty message applies only
when neither maps nor sessions are available. Gated diagnostics verified
57 maps, zero sessions, 57 visible list entries and selection preservation.
The temporary logging was removed after verification.

`test_native` now selects Road War in Games, verifies its filename and
128×128 preview, checks Create Game/Ok availability, and verifies the same
selected row and title after Create Game. It then runs the existing native
scrollbar, dropdown and game-name checks. The updated Games screenshot is
`/private/tmp/starcraft-join.bmp`.

### Single map selection, automatic server title and working lobby (2026-10-09)

**User-requested engine behavior, not a retail callback finding:** Games is
the only map selector. Create Game (or activating a local map) hosts it and
opens the lobby directly. The CHK title becomes the advertised server name
(within the transport's 31-byte name limit). Remove the runtime `glucreat`
and `glupedit` screens and their name-entry/second-selection callbacks; the
native decoders and exhaustive glue audit still cover both assets.

Dark Colony's `SKIRMISH` host path is the behavioral reference: the selected
map supplies `M_NetHost`'s title/path, configured player count sets the lobby
capacity, and all joined players ready before launch. StarCraft now exposes
2..map capacity in a Players dropdown, defaults to two, and exposes engine
speed percentages (10..200 in steps of 10). It previously demanded every
map slot and displayed a hardcoded Normal speed. Speed travels through the
existing WELCOME packet, not a second game-specific option record. Discovery
does not advertise speed, so an unjoined server's speed field stays blank.

**Confirmed native layout:** `glujoin.bin` id 9 is the unused 206×20 detail
field at (404,183); id 12 is the 90×18 speed value at (536,278). These are
reused as the two dropdown rectangles without invented placement offsets.
`gluchat.bin` ids 29+4n are the native 96×16 race fields at (224,35+18n).
Their shared dropdown behavior uses the native `palnl/dlg.grp` outline and
arrow, plus the user's requested four-pixel text inset. Race selection now
lists Terran/Zerg/Protoss, publishes the selected seat through `M_NetSetSeat`,
and becomes disabled while ready. Ready/Unready names the actual action.
Only the local joined player's race is editable. Chat, cancel and connection
errors use the common multiplayer lifecycle; lobby failures keep their error
message when returning to Games. Screen entry refreshes lobby values before
the first input event.

The adapter remains Melee-only; this change does not implement the other
retail game types, computer/observer slots or Random race. The former one-row
Melee dropdown is now a static game-type value rather than a false choice.

Verification: `test_native` checks direct hosting of Road War, native list
scrolling, dropdown artwork/interaction, four-pixel insets, readiness and
cancel. `test_multiplayer` drives two real native menus over loopback: select
four-slot Lost Temple, choose four then two players, set 120% speed, discover
the map-title server, choose Zerg/Protoss, exchange chat, ready/unready and
launch. Both peers load the same CHK and spawn exactly five chosen-race
objects per active player. Temporary gated host diagnostics confirmed the
map, title, player count and speed, then were removed. Screenshot outputs:
`/private/tmp/starcraft-create-road-war.bmp` and `/private/tmp/starcraft-lobby.bmp`.

```sh
make -j8
env SDL_VIDEODRIVER=dummy make test-starcraft
env SDL_VIDEODRIVER=dummy build/bin/starcraft --check
```

Socket tests require loopback permission even with SDL's dummy video driver.

## Single-player glue screens (2026-10-09)

Asset/runtime audit only; no executable disassembly. Screen to dialog and
theme, as now loaded:

| Screen | Dialog | Theme |
| --- | --- | --- |
| Main menu | `glumain.bin` | `palmm` |
| Registry, new ID | `glulogin.bin`, `glunewch.bin` | `palnl` |
| Campaign select | `install/rez/glucmpgn.bin` | `palcs` |
| Terran, Zerg, Protoss briefing | `glurdyt.bin`, `glurdyz.bin`, `glurdyp.bin` | `palrt`, `palrz`, `palrp` |
| Load Saved | `install/rez/gluload.bin` | `palnl` |
| Play Custom | `install/rez/glucustm.bin` | `palnl` |
| Ok / Ok-Cancel popups | `glupok.bin`, `glupokcancel.bin` | the covered screen's theme |

The briefing was previously always `glurdyt`; the campaign folder now picks
the race. The three share control ids. `glurdyz` adds an unlabelled button 1.

Responsive areas are inclusive LTRB. Every type-14 button with flag `0x10`
stores `0,0,w-1,h-1`, and the field at +74/+76 repeats right/bottom for those
buttons but not for the campaign races, so it is not used. Lists, fields and
comboboxes set flag `0x10` with all zeros and answer on their whole rect.

Popups are 360×200 and the theme folders each hold three 360×200 PCXs:
`pOPopup` (Ok), `pDPopup` (Ok/Cancel, by elimination) and `pEPopup` (entry,
for `glupedit`). The size match is the evidence; the retail selection routine
was not disassembled.

Play Custom lists the same extracted melee CHKs as Create Game, flat. The
first playable OWNR slot is the player; the rest are Computer or Closed
(gluall.tbl 129/131), each with a Zerg/Terran/Protoss/Random race. The CHK
loader rewrites OWNR to 6/5/0 and SIDE in place, so slot numbers, start
locations and the console player are unchanged, then spawns a melee start
for every open slot. OWNR 5 owners get the shared computer player. The info
rows use gluall.tbl 36/38 (Computer Slots, Human Slots). gluall.tbl 54 refuses
a game with no computer opponent.

Load Saved lists engine `.sav` files from the user directory and any retail
`.snx` under `save/`. Loading is not implemented and says so; Delete asks
gluall.tbl 22 first.

The briefing script's portrait and timing actions are still not played. The
transmission text holds five seconds and then rises a line every 2.5 s;
Replay restarts it.

Glue not yet used at runtime: the `pal{t,z,p}{a,b,c}` loading pictures,
every theme's `arrow.grp` cursor, `iscore.grp` and `scorebox.pcx`, and the
Battle.net, modem and chatroom families. (The score and title screens are
covered below.)
`test_glue.c` still decodes all of them.

## Animations, research, triggers and the in-level screens (2026-10-09)

Asset and reference audit; no executable disassembly. References: pinned
PyMS `IScriptBIN.py` (opcode table, `ENTRY_TYPES`), `TRG.py` (player ids,
briefing actions), and Stargus's decompiled `doc/iscript.txt` and unit Lua.

### IScript compiler corrections

- Header entry counts follow PyMS `ENTRY_TYPES` (type 15 has 16 entries, 21
  has 22, 23 has 24, ...). The previous table, from Stargus's Kaitai spec, was
  one short and dropped AlmostBuilt (SCV mining), WarpIn and StarEditInit.
- Opcode lengths: `sproluselo` (0x15) is 3 bytes; 0x3F..0x44 are 2, 2, 1, 4, 0, 0.
- A turning image's playfram is a set of 17 plus the facing slot. After
  `setfldirect`, or off a multiple of 17, it names one picture. Turning GRPs
  now carry one single-direction frame per picture after the turning sets, so
  the Marine's death (0xdd..0xe4) shows its own eight pictures.
- A script that waits before its first playfram keeps the idle pose (Marine
  walking, SCV mining start in set 4 / set 0).
- Attack scripts put A_Attack on the frame after their first attack opcode
  (`attack`, `attackwith`, `useweapon`, `attackmelee`, `domissiledmg`,
  `dogrddamage`); scripts with none strike on their first frame as before.
- Death (entry 1) is compiled. Overlay opcodes (`imgol` family) and sprite
  opcodes (`lowsprul`, `sprol`, ...) become extra sprites, `sc-img-NNN`, whose
  Init scripts follow the death: Marine → tmaDeath remnant, SCV/Probe →
  explosion, Drone → own frames then corpse, Terran buildings → explosion then
  rubble. The original plays overlay and remnant at once; here they run one
  after the other. The chain ends in S_NULL, which removes the unit.
- Workers mine with AlmostBuilt (entry 15), set as the engine's harvest state.

### Research

`tools/sc_catalog native upgrades` writes `upgrades.inc` from upgrades.dat
(46 rows: base and per-level costs and times, icon, race, maximum level).
`units.inc` gains armor, armor upgrade, weapon upgrade, weapon damage bonus
and the build and destroy scores. Researching buildings are not in the DAT
files and follow Stargus's command cards; add-on and Brood War research are
left out. Each level is its own product (ui id 1000 + upgrade×4 + level). A
hit deals weapon damage + bonus × weapon level − (armor + armor level), at
least 1, through a new optional `gameinfo_t.hit_damage`. Levels live in
`level.upgrades[upgrade][owner].weapon`, so they are saved and hashed.

### Triggers

Player ids now follow PyMS: 13 Current Player, 14 Foes, 15 Allies, 16 Neutral
Players, 17 All Players, 18..21 Forces, 26 Non Allied Victory Players. The
runtime used ids one lower. Each trigger also runs once per owning player
with that player as the current player, as retail copies triggers per owner.
Blizzard's default melee triggers ("current player commands at most 0
buildings: Defeat") had fired at once on Road War. Outcomes, messages and
camera actions apply only to the local player's copy.

### Screens

- In level: gamemenu, options, spd_dlg, snd_dlg, video, savegame, loadgame,
  objctdlg, helpmenu, help (help_txt.tbl or tips.tbl), abrtmenu, restart,
  quit2mnu, quit — on `dlgs/<race>.grp` with tiled `dlgs/tile.grp` panels.
  Restart, Quit Mission and Exit work; Save, Load and the option controls are
  visual only.
- Result: wmission (Victory / Continue Playing, or the draw line) or lmission,
  then gluscore on `pal<race><v|d>` with `score<race><v|d>/pmain.pcx` and
  `pinset.pcx` rows. A campaign win briefs the next mission, a campaign loss
  briefs the same one, other games end. Score inputs are the mission's death
  and kill counters, units.dat scores and stock changes (rises are income,
  falls are spending); "produced" includes starting units. This is not the
  retail formula, which has not been disassembled.
- Title: titledlg over `title/title.pcx` with `title/tfont.pcx`, once, for
  three seconds or until a key or click.
- Briefing: the MBRF script runs (Wait, Show/Hide Portrait, Transmission,
  Talking Portrait, Text Message). Portraits are portdata.dat's idle (`fid`)
  and talking (`tlk`) SMKs at twice their 60×56 size in frames 15..18; the
  voice WAVs are not played.

`tests/starcraft/test_gameplay.c` plays Road War against a Zerg computer slot
and checks the compiled animations, mining, construction, training, research
costs and effect, combat damage, death and removal, structure cells freed,
victory and defeat dialogs, the score screen, every in-level dialog route,
the campaign result flow and the Terran 01 briefing timeline.


## Combat model from the DATs (2026-10-09)

No executable was disassembled. Sources are the retail DATs (fingerprints
above), PyMS's DAT and IScript layouts, and OpenBW's rules, used as a
reference rather than copied.

```sh
make build/sc_catalog
build/sc_catalog data/STARCRAFT/native > games/starcraft/units.inc
build/sc_catalog data/STARCRAFT/native weapons > games/starcraft/weapons.inc
build/sc_catalog data/STARCRAFT/native techs > games/starcraft/techs.inc
build/sc_catalog data/STARCRAFT/native upgrades > games/starcraft/upgrades.inc
make build/bin/tests/starcraft/test_combat && build/bin/tests/starcraft/test_combat
```

**Confirmed from the data.**
- `units.dat` is the classic 19,192-byte table: no Brood War max-hits columns
  and no Lurker. Units now keep their ground (column 17) and air (19) weapon
  rows, their size (26) and a speed; weapon damage, range and cooldown come
  from `weapons.inc`. 100 is "no weapon" in this 100-row `weapons.dat`.
- `weapons.dat` columns (x N=100): target flags 7, minimum and maximum range
  9/13, damage upgrade 17, damage type 18, behavior 19, explosion 21, splash
  radii 22/24/26, damage 28, bonus 30, cooldown 32, damage factor 33.
  Damage types: 1 explosive, 2 concussive, 3 normal, 4 ignore armor.
  Explosions: 1 normal, 2 radial splash (Arclite Shock Cannon 10/25/40,
  spider mine, Psionic Storm 48), 3 enemy splash (Flame Thrower 15/20/25,
  Psionic Shockwave 3/15/30, Scarab 20/40/60), 24 air splash (unused here).
  Behavior 7 is the Glave Wurm bounce.
- Damage factor is 2 only for the Hellfire Missile Pack and Anti-matter
  Missiles. Psi Blades and Flame Thrower are 1: the Zealot's attack script
  (ZealotGndAttkRpt) has two `attackmelee`, and the Firebat's has three
  `attkshiftproj`. The engine counts one attack of `hits` hits, using
  Brood War's max ground hits (2) for Zealot, Firebat and their heroes; the
  IScript compiler now strikes only on the first attack opcode of a pass.
- `units.dat` flags: 0x200 cloakable (Ghost, Wraith, their heroes),
  0x400000 permanent cloak (Observer, Dark Templar, Zeratul), 0x8000
  detector, 0x200000 spellcaster (energy).
- `flingy.dat` (N=184) top speed is 1/256 pixel per frame. Flingies with
  movement control 2 store 1 there and move by their Walking script's
  `move` opcodes; the catalog sums `move` over `wait` frames once round that
  loop. Marine 1024 (4.0 px), Zergling 1426, Hydralisk 950, Dragoon 1344,
  Goliath 1203, Ultralisk 1382, Vulture 1707, Overlord 213. At 24 frames a
  second on 32-pixel cells, cells/s = speed × 3 / 1024: a Marine keeps 3.0.
- `techdata.dat` (24 rows of 18 bytes): minerals, gas, time and energy
  (u16), an unknown u32, icon and label (u16), race, unused byte. Cloaking
  Field (9) and Personnel Cloaking (10) cost 25 energy.

**Inferred from OpenBW, not verified against the executable.**
- Hit order: damage plus bonus × upgrade level, split for splash, then
  shields less Plasma Shields (upgrade 15) without damage-type scaling, then
  armor, then the damage type against size (explosive 50/75/100%,
  concussive 100/50/25%), at least half a point; hit points keep the 1/256
  remainder (`sc.wound`).
- Splash rings are measured from the target's position to each unit's edge,
  and ground splash stays on the target's layer. Bounces reach the nearest
  enemy within 96 pixels other than the last two targets, three targets in
  all, each a third of the previous damage.
- Shields regenerate 7/256 and energy 8/256 per frame, to 200 energy;
  casters start with 50. A switched-on cloak drains 13/256 energy per frame
  instead of regenerating and drops at zero.

**Engine choices, not retail.** The Shield Battery restores up to two shield
points a frame to one unit within four cells for one energy per two points.
Cloaking requires its research, and the sieged tank's 64-pixel minimum range
is enforced (`weapondef_t.min_range`).

This supersedes two statements of the Research section above: weapon upgrade
and bonus now come from each weapons.dat row (so the Goliath's turret weapons
upgrade too), and the minimum hit is half a point carried in `sc.wound`
rather than one whole point.

## Unit mechanics and spells

`games/starcraft/p_units.c` (hangars, add-ons, Bunkers, Archons, nukes),
`p_spell.c` (techdata abilities) and `p_tactics.c` (the computer player's use
of them); `tests/starcraft/test_units.c` checks each.

**Confirmed from the data.**
- `units.dat` add-on position (column 37, buildings 106..201): 128, 32 pixels
  for every add-on, its top-left from its building's, so it sits right of a
  4-by-3 building, level with its lower edge.
- Transport space required and provided (47, 48): Marine, Firebat, Ghost and
  SCV take 1, a Vulture 2, a Siege Tank 4; a Bunker offers 4. Retail Bunkers
  take only infantry, so here only organic Terran walkers of one slot that
  do not gather.
- `techdata.dat` icon (u16 at 288) is the command-icon frame; the ability's
  energy, research cost and time are its other columns.
- Spell reaches come from their weapons.dat rows (Lockdown, EMP 256 pixels;
  Irradiate, Ensnare, Dark Swarm, Plague, Psionic Storm 288; Parasite 512;
  Yamato 320). Psionic Storm: 16 ignore-armor damage in 48 pixels; EMP: 64
  pixels; Nuclear Strike: 600 in 128/192/256-pixel rings; Scarab: 100 (+25)
  normal with enemy splash 20/40/60.

**Inferred from OpenBW and retail behavior, not verified.**
- Capacities: 4 interceptors (8 with Carrier Capacity), 5 scarabs (10 with
  Reaver Capacity), one nuke per silo. Reaver cooldown 60 frames. Retail
  melee grants Scanner Sweep, Defensive Matrix, Dark Swarm, Parasite and
  Archon Warp; heroes know every ability.
- Timers (frames): Defensive Matrix 250 points for 1344, Lockdown 1048,
  Ensnare 600 at half speed, Hallucination 1350, Broodlings 1800, Dark Swarm
  900, Scanner Sweep 262, a Ghost paints a nuke for 336. Irradiate deals 250
  over 37 beats of 8 frames to organic units within 32 pixels of its host;
  Plague 300 over 75 beats and never kills. Storms strike every 9 frames,
  seven times, and a unit hurt by one storm is spared by the others until
  the next strike. Reactor upgrades raise a caster's 200 energy by 50; U-238
  Shells, Grooved Spines and Singularity Charge add 1, 1 and 2 cells.
- An Archon warps for its units.dat build time (300 frames) and has full
  shields. Units in a Bunker have one more cell of reach.

**Engine choices, not retail.** An interceptor strikes once a pass, timed
here at 30 frames (weapons.dat's cooldown of 1 is its script's repeat); a
docked interceptor is whole again at once. A scarab is a ground unit that
fizzles after 90 frames. The siege transform takes 48 frames each way.
Ensnare and Plague cover 2 cells around the spot (weapons.dat leaves them
0). Under Dark Swarm every hit from a weapon reaching more than one cell
misses. Lingering areas are neutral mobjs: storms and sweeps are Map
Revealers (units.dat 101), the swarm its own unit (202). The AI storms clumps
of three enemies with none of its own, shields wounded fighters with a
Defensive Matrix, EMPs 150 shields, irradiates the biggest organic unit,
sieges within 12 cells and unsieges with none in 13. Not simulated: Stim
Packs, Spider Mines, Burrow, Recall, Stasis Field, Infestation, Dropship
and Shuttle transport.

## Resource strip and command-card input (2026-10-11)

**Confirmed native assets.** This investigation used the same original-disc
`stardat.mpq` fingerprint recorded above; it did not inspect or execute
`starcraft.exe`. The extracted files have these SHA-256 fingerprints:

| File | SHA-256 |
| --- | --- |
| `rez/statres.bin` | `00220598238d8f702fa41a80faa4043ed3f2973c91fdc96b57945fdbfd8b98c1` |
| `game/icons.grp` | `cb81835f98201c12dcacbee820ab47fcf6eb561ec2ffc74856f7e1790889a881` |
| `unit/cmdbtns/cmdicons.grp` | `966a89921788e7f3305c06bba64b924c187275f566bc045633daff6648b787d0` |

`statres.bin` starts at `(220,0)` with a `420×20` root. Six numeric
controls occupy the top row, with 66-pixel fields starting at x=300, 368,
436, 504 and 572 (the leftmost is 69 pixels at x=229). `game/icons.grp`
contains twelve frames on a `64×64` canvas; frames 0, 2 and 5 are `14×14`
mineral, gas and supply pictures. The pinned Stargus
`scripts/gameui.lua` independently places those frames at x=436, 504 and
572, y=3, and their numbers at x=452, 520 and 588. The HUD now draws those
native frames and values in one top row. The earlier separate “Minerals”,
“Gas” and “Supply” strings were an engine placeholder, not retail layout.

**Confirmed command input.** `statbtnt.bin` supplies nine `36×34` slots.
`cmdicons.grp` has 365 frames on the same canvas; its Move frame 228 has a
`27×28` visible crop and Build Structure frame 234 a `22×20` crop. A trace
at `M_MenuResponder` showed the build-page press and release hitting slot 7,
the Supply Depot press and release hitting slot 2, then a world click at a
valid cell dispatching `TC_CONSTRUCT`. `test_gameplay` now sends SDL mouse
events through the real HUD at both 640×480 and 1280×960: three Command
Center clicks queue three SCVs, and an SCV build-page/product/world-click
sequence starts a Supply Depot and spends 100 minerals. This disproves the
hypothesis that the shared menu responder drops all StarCraft build clicks.
The old test had invoked button routines directly and missed this path.

**Engine behavior chosen from the native geometry.** The UI copy of each
command glyph now centers its cropped pixels within the `36×34` slot.
Gameplay GRP ground points remain untouched. A Command Center with a queued
unit now shows its queue count, and the card displays only products that the
selected maker can currently accept and afford. This removes bright buttons
that previously ignored clicks because prerequisites, supply, queue state or
resources rejected the order. The exact retail dimming or visibility of an
unavailable command remains **unknown**; no executable drawing path was
traced, and the current hide rule is an explicit engine UI choice.

## Production pipeline and building controls (2026-10-11)

**Confirmed native assets and reference layout.** No executable instruction
was inspected in this investigation. The original-disc assets and the pinned
Stargus checkout (`2a4d54604949e4772f6638a8412c8ebd62444044`) are the
evidence. `scripts/gameui.lua` sets one active training icon at `(240,396)`
and four pending icons at `(240,434)`, `(278,434)`, `(316,434)`, `(354,434)`
on the 640×480 console. `scripts/ui.lua` draws the progress track at x=278
and shows five blue frame slots. `scripts/menus/help.lua` says five units may
be queued and a queued unit may be cancelled by clicking its pipeline icon.
Blizzard's [SCV guide](https://classic.battle.net/scc/Terran/uscv.shtml)
independently confirms five SCVs per Command Center. The queue caption position
`(316,410)` and progress y=422 are **inferred from the user-supplied retail
screenshot and Stargus panel placement**, not an executable trace.

| Native file | SHA-256 | Finding |
| --- | --- | --- |
| `unit/cmdbtns/tcmdbtns.grp` | `dc5fbfca0065af2ac2fddc05b2708502363c39fd4503b53f6636d89b219f2945` | 37 frames on a 72×72 canvas; frame 2 is a 36×36 blue queue border, frames 6–10 are the blue numerals 1–5 |
| `unit/cmdbtns/zcmdbtns.grp` | `dc5fbfca0065af2ac2fddc05b2708502363c39fd4503b53f6636d89b219f2945` | Same bytes as the Terran file in this extraction |
| `unit/cmdbtns/pcmdbtns.grp` | `ff087333f0924e1934b2b5fcfc2fff42329afd0c2c20ce8611770575363e4315` | Race-specific queue border and numerals |
| `game/tpbrempt.pcx` | `270572ff03cb265affd815b097ec5e82beb22f6e042f27853bbef048f9a8c07c` | 108×9 empty progress track |
| `game/tpbrfull.pcx` | `d92e0567711375e155c0b8fdd2073a8844584ef7ce3d66265e22ea40b92fa74c` | 108×9 progress fill |

The native GRPs were decoded through `sc_decode_grp`, drawn as a temporary
37-frame contact sheet, and inspected at native palette colors. This disproved
the first assumption that Stargus's converted `pcmdbtns.png` frame numbers
could be used directly: its panel names frame 6 for a box, whereas retail GRP
frame 2 is the 36×36 blue box and frames 6–10 contain numerals. The temporary
diagnostic drawing was removed after the mapping was established. Reproduce
the dimensions with `od -An -t u2 -N6` on the GRP and the visible layout with
`SDL_VIDEODRIVER=dummy build/bin/tests/starcraft/test_gameplay`, which writes
`/private/tmp/starcraft-queue.bmp`.

**Confirmed commands.** Stargus `scripts/icons.lua` maps Rally Point to native
command icon 286 and Cancel to 236. Its `scripts/terran/units.lua` places Rally
Point in command slot 6 (zero-based slot 5); `scripts/buttons.lua` places
Cancel Unit Training in slot 9 (zero-based slot 8). The user-supplied screenshot
shows these same two glyphs on the selected Terran Command Center. The panel
now sends `TC_SET_RALLY` with a world target and `TC_CANCEL_PRODUCTION` with a
pipeline slot index. A selected building stores its rally point on its mobj;
the production completion hook sends each newly trained mobile unit there.
Cancellation removes one queued unit, refunds its catalog cost, and restarts
the next unit only when the active slot was cancelled. **Unknown:** the exact
retail refund and active-slot timer rules were not traced from the executable;
these are current engine behaviors, not claimed native findings.

**Engine queue representation.** The first panel pass exposed a separate
limitation: the engine had only one product type plus a count, so a Barracks
could not retain the order Marine, Firebat, Marine. `production_t` now keeps
the active product and a bounded array of pending products. StarCraft accepts
different unit products on the same maker; non-unit products and games that
lock unit queues retain their existing single-product rule. Completing or
cancelling an active product promotes the next entry with its own build time;
cancelling a pending slot shifts only those later entries. The UI draws each
slot's catalog glyph, and supply reservations sum the actual pending types.
`test_units` reproduces the Marine/Firebat/Marine sequence, removes the middle
Firebat, then verifies release order and refund. This is a necessary engine
representation correction, not a claim that an executable queue structure was
recovered.

The prior `Training %` text in the HP field was a temporary engine substitute;
the HP value is again shown under the wireframe while native queue icons,
caption and progress occupy their own status widgets. The queue widget owns
its assets and callbacks in `m_status.c`; `m_menu.c` retains the screen table
and command-card refresh. The gameplay test verifies the rendered bar changes
with time, queue-slot and Cancel clicks change resources and depth, and a
completed SCV receives its building's rally movement order. `test_zerg`
also verifies that a larva's Zergling morph inherits its hatchery's rally
target.

## Selected-unit status, equipment and compact font (2026-10-11)

**Scope and evidence.** This pass examined the original-disc assets and the
user-supplied retail screenshot of Sarah Kerrigan, not `starcraft.exe`.
Blizzard's [original manual](https://ftp.blizzard.com/pub/misc/StarCraft.PDF)
calls the two bottom boxes **Equipment** (armor and weapon upgrade status),
not carried-item inventory. Stargus at pinned commit
`2a4d54604949e4772f6638a8412c8ebd62444044` maps armor to
`cmdicons.grp` frame 292 and piercing damage to frame 290 in
`scripts/ui.lua`; `dataset/palettes.json` selects the native `ticon-0`
palette for these command icons. The retail screenshot confirms the two
blue-framed boxes, unit rank, personal kills, and a second number under HP.
The subsequent user-supplied SCV screenshot confirms that ordinary workers
also show “Private”, “Kills: 0”, and both equipment boxes.

| Native file | SHA-256 | Confirmed finding |
| --- | --- | --- |
| `rez/statdata.bin` | `9ef01fd2900787518642b496ce1199253ba96ff205176f462410a8340a65d41f` | Wireframe ID 1 at (168,388), name ID 65531 at (240,388), HP ID 65529 at (160,452), second status ID 65528 at (160,464), label slots 65516/65515 at (244,404)/(244,418), equipment slots 9/10 at (242,439)/(281,439), all relative to the 640×480 console |
| `arr/units.dat` | `dc7b8b59bb14a3ba67829926ad50e8d363032a351299e5087f790ed09616619d` | Column 11, byte offset 4524, is a unit's rank/sublabel selector; Sarah Kerrigan row 16 has value 17 |
| `rez/stat_txt.tbl` | `e1f4144bbe26383f93e37f694de5d92126cf5de793f21c41e6a0c08f7930b1a1` | This disc has 1185 strings; one-based label `550 + rank` resolves Kerrigan's 17 to label 567, “Lieutenant” |
| `install/files/font/font10.fnt` | `10dcf7cf3ee78f29f20992d4608e4f6c9b1c8f26d502b2e70794b437033c27f9` | Native compact game font, line height 13, fits the status rows |
| `unit/cmdbtns/ticon.pcx` | `13aafd5f42546f35518bb375f8a38ae3bd82dfb74366061cfde874bb0b9d197b` | Supplies the command icon palette and first-16-color mapping |

**Implementation consequence.** The C catalogue now emits the rank text
from each `units.dat` row and this disc's `stat_txt.tbl`; `m_status.c` owns
the native status controls and uses `font10` for their centered text. A
StarCraft mobj keeps its own kill count, incremented at the common lethal-hit
point when an enemy unit is credited as killer. The screenshot's `221/250`
confirms Kerrigan's 250 maximum energy; applying that maximum to the other
spellcaster heroes is an **inference** pending an executable or retail-data
trace.
Armor and weapon badges show the current upgrade level for the selected
owner's catalog upgrade IDs. The exact retail icon tint remains **unknown**
without an executable palette trace. The current `ticon-0` translation is
native-source based, not a visual color correction.

**Disproved assumption.** Newer PyMS tables place rank strings at a different
base. Applying their `1302 + rank` index to this original-disc 1185-string
table yields no label; the checked disc gives Kerrigan's “Lieutenant” at 567.
Likewise the prior 16-pixel game status font produced oversized labels in
the available 14-pixel control rows.

**Correction (2026-10-11, user-reported Command Center label).** `font10`
still makes “Terran Command Center” 141 pixels wide inside the native
152-pixel name control. The user-provided retail screenshot shows a noticeably
narrower label. The original disc's `install/files/font/font8.fnt` (SHA-256
`33b56e39c0bde96834b3c1ad8e8b90e91e7f518e6209dcd1124c43399907ed01`)
has an 11-pixel line height and renders that name 119 pixels wide. The status
panel now uses this native font for its name and other selected-unit labels;
the game palette still comes from `game/tfontgam.pcx`. This supersedes the
`font10` implementation statement above. The `statdata.bin` name record's
flag word is `0x00010000` at control ID 65531. PyMS labels that flag as
font14, which conflicts with the observed retail label size; the original
EXE's status-specific font dispatch is **unknown**, so that flag alone is not
treated as a rendered-size rule. The earlier 16-pixel choice and the later
13-pixel choice were both too large for the supplied screenshot.

Reproduce the measured widths and selected-building screenshot with
`env SDL_VIDEODRIVER=dummy build/bin/tests/starcraft/test_gameplay`:
`/private/tmp/starcraft-command-center-status.bmp`. The test checks the
11-pixel font height and 119-pixel name width.

Reproduce with `build/sc_catalog data/STARCRAFT/native` (Kerrigan is row 16)
and `SDL_VIDEODRIVER=dummy build/bin/tests/starcraft/test_gameplay` (writes
`/private/tmp/starcraft-hero-status.bmp` and
`/private/tmp/starcraft-scv-status.bmp`). The test checks rank, personal
kills, energy, compact font and both equipment controls.

**Weapon-icon correction from the SCV screenshot.** Stargus's fixed frame
290 was a simplified panel mapping, not the retail icon rule. The original
`arr/weapons.dat` (SHA-256
`03551df8800a01ec1e99e3648ece6deb83c054588f52967ba76c1d85c92be461`)
has 100 parallel records and a 16-bit `icon` column at byte offset
`40 × 100`. Weapon 13, the SCV's Fusion Cutter, points to `cmdicons.grp`
frame **329**; weapon 3, Kerrigan's C-10 Canister Rifle, points to frame
**324**. The worker and hero screenshots visibly have different weapon
pictures. `sc_catalog` now carries that native icon ID into each weapon row,
and the status box selects it from the selected unit's ground weapon (or air
weapon when ground is absent). The armor picture comes from the unit's
`upgrades.dat` armor-upgrade icon, frame 292 for these Terran units. Temporary
`OPEN_RTS_DEBUG_SC_EQUIPMENT` logging confirmed SCV 292/329 and Kerrigan
292/324 before the logging was removed. The gameplay test asserts those
frame IDs and verifies “Kills: 0” on the SCV. It also checks that a worker's
construction progress caption does not cover the rank and kills lines;
production progress belongs to the selected building's panel.

## Stargus HUD availability and interaction audit (2026-10-11)

**Scope/provenance.** This investigation used the pinned local Stargus
`2a4d54604949e4772f6638a8412c8ebd62444044` and Stratagus
`3d87c93f7fd8c0b62ee1be5df0a6d9efc72ca6cc` sources. No retail executable
was examined or run; there is no new executable fingerprint or address claim.
The user's `Screenshot 2026-10-11 at 13.59.36.jpg` shows **open-rts**, with
zero minerals, one SCV training, and no SCV command icon. It is evidence of
our defect, not evidence of retail visibility rules. Native artwork and file
fingerprints remain those recorded in the preceding HUD sections.

**Confirmed cause, disproven earlier choice.** Temporary
`OPEN_RTS_DEBUG_CARD` logging in `product_ready` printed maker/product IDs,
minerals, gas, cost, affordability and queue depth. The card refresh was
using both `R_CanAfford` and producer acceptance to decide visibility.
Spending the last 50 minerals on an SCV therefore removed the button.
The earlier section's deliberate "accept and afford" hide rule is
superseded: it does not match the Stargus behavior requested by the user.
The diagnostic was removed after regression coverage reproduced the failure.

**Confirmed reference behavior.**

- Stratagus `src/ui/botpanel.cpp::IsButtonAllowed` checks dependencies for
  train/build/research, not funds or supply. `DoClicked_Build`,
  `DoClicked_Train`, and `DoClicked_Research` check costs/limits at activation.
  Training remains visible while training when queues are enabled.
- Each Stargus `DefineButton` supplies a fixed `Pos`, `Level`, and `Key`.
  Command Center SCV is position 1 with S; Barracks Marine/Firebat/Ghost
  use positions 1/2/3 and M/F/G. Unavailable products must not shift later
  products into their positions. SCV Engineering Bay/Missile Turret/Academy
  occupy positions 5/6/7, correcting our old Academy-first order.
  Drone basic construction has six products with a gap at position 6;
  Hydralisk Den is position 7. Its advanced page starts with Spire, not the
  ninth entry of a shared worker catalog.
- `DoClicked_SelectTarget` and `DoClicked_Build` switch to level 9,
  which `scripts/buttons.lua` defines as Cancel only. Selection changes
  invalidate the pending command. Our target copies the menu item outside
  the nine displayed slots so refreshing Cancel cannot overwrite the order.
- `UpdateButtonPanelMultipleUnits` checks group commands against all units;
  same-type selections use their type's card. `DoClicked_Train` chooses the
  same-type producer with the shortest training queue. Our implementation
  additionally skips a maker that cannot currently accept that product.
- `DoClicked_CancelTrain` sends slot -1. In
  `src/action/command.cpp::CommandCancelTraining`, -1 cancels **all** training;
  a nonnegative slot cancels just that entry. The three action files
  `action_train.cpp`, `action_research.cpp`, and `action_upgradeto.cpp` use
  100-percent cancellation refunds. Our previous command-card Cancel of
  slot zero is superseded; pipeline icon clicks still cancel one slot.
- Research/upgrading switches to a cancel-only card in
  `UpdateButtonPanelSingleUnit`; `mainscr.cpp::DrawUnitInfo_single_selection`
  uses one icon rather than a five-place training queue for those orders.
  `gameui.lua` places eight cargo icons at x=240/276/312/348 and y=396/433;
  `mainscr.cpp` draws boarded passengers. Clicking our cargo icon now sends
  the passenger's stable ID with `TC_UNLOAD`; the simulation verifies it
  belongs to the selected transport before unloading that passenger.
- `IsButtonAllowed(Unload)` requires passengers. Hatchery/Lair/Hive scripts
  provide Select Larva at position 1 (S), rally at position 2 (R), and
  building morph at position 7. Their larva card belongs to selected larvae,
  avoiding the collisions created by our previous combined building/larva
  card. We select the hatchery's live free larvae as a group; the reference
  callback repeatedly selects a single nearby larva.
- `DrawPopup`/`DrawPopupCosts` and the status-line code in `botpanel.cpp`
  expose command names, resource/supply costs and spell energy. Our tooltips
  now show those values from the same catalog and supply calculation used
  for purchases (including two-per-egg products and morph supply deltas).
  `gameui.lua` places its status line at (172,350), used here for the latest
  engine message. The previous StarCraft HUD never displayed `chat_text`,
  making purchase failures and chat invisible despite their engine lifetime.

**Implementation consequences and explicit engine choices.** Visibility now
uses tech prerequisites; funds, supply, power, queue capacity and local
producer restrictions are checked on click. Failed purchases display a
message without charging resources or changing the queue. All commands use
existing menu input and ticcmd dispatch. Production/status data is read from
the selected mobj; enemy queues are not shown. Switching between two objects
of the same type resets targeting, build pages and held input. Reinitializing
the HUD also clears this UI state.

Stargus has unfinished/conflicting definitions: its Starport Science Vessel
and Battlecruiser both claim position 3, multiple Protoss products share the
same key, and Terran Liftoff is wired to the ordinary move action. We do not
copy those defects: Battlecruiser uses position 4; the C product table assigns
unique mnemonic keys to the overlapping/unfinished cards. The new tooltip
box above the command panel is an engine layout, not a claimed retail popup
rectangle. Notification lifetime follows the existing engine five-second
message convention. Native GRP/PCX artwork is unchanged; no assets are generated.

Our Zerg runtime temporarily changes a larva/mutalisk into an egg/cocoon.
Cancelling a newly exposed morph status icon restores the product's original
maker type before removing its order; otherwise it leaves an inert egg.
Buildings already retain their original type until completion. This is a
required correction to our representation, not an executable-derived rule.

**Remaining gaps/unknowns.** This is broader HUD interaction coverage, not
complete Stargus or retail parity. Repair, Hold Position, attack-move,
Patrol, worker Return Cargo, and building Liftoff need corresponding
simulation behavior before their controls can be exposed correctly. Research
and spell cards retain numeric hotkeys where there is no mapped command;
research positions are stable by upgrade family. Exact disabled icon colors,
retail popup appearance, damaged wireframes, full shield/energy/equipment
panels and race-specific details are not established by this investigation.
Construction sites still live on workers in this engine rather than as
selectable unfinished buildings, so their separate cancel-construction panel
cannot yet follow the reference lifecycle. These limitations must not be
reported as complete HUD fidelity.

**Reproduction.** `make` and `make test-starcraft` rebuild and exercise all
StarCraft tests with `SDL_VIDEODRIVER=dummy`. `test_gameplay::command_card`
reproduces spending the final 50 minerals and checks that a further click
preserves queue depth and resources while showing "Not enough minerals".
It writes `/private/tmp/starcraft-zero-minerals.bmp`, with the SCV button
still visible. `hud_contract` covers mouse and S hotkey activation, full
queues, supply and gas rejection, shortest-queue selection, same-type target
reset, mixed selections, fixed worker slots, research, individual unloading,
empty transports, enemy queue privacy, larva selection/morph cancellation,
and the Zerg advanced Spire page. Existing tests retain scaled mouse input,
queue progress, per-slot refunds, rally, spell and status coverage.

**Verification outcome.** Final `make -j4`, focused `test_gameplay`, and
headless `starcraft --check` passed (64×64 Terran 01, 46 units, 10 vents,
4,844 terrain tiles). All 16 StarCraft test programs passed across the suite
run and its continuation: sandbox networking stopped `test_multiplayer`,
then both it and `test_native` passed with local socket access; the remaining
seven programs passed afterward. The last selection/no-larva and stale-cargo
checks passed in the final gameplay rerun. Zero-mineral and startup BMPs
were rendered and visually inspected. Only the existing unused-helper
warnings in `test_mission.c` appeared during the suite relink.

### Regenerating hit points (2026-11-12)

**Confirmed in OpenBW, not independently executable-verified.** At OpenBW
`4b046d5f65302b10cb0a745f0fecd37ec85b20a8`, `bwgame.h`
`update_unit_values` checks the completed unit's `units.dat` regenerate
flag, positive health and its maximum, then adds 4/256 hit points per 24 Hz
frame, capped at the type maximum. The visible and hidden unit update paths
both call it; the check does not exclude invincible or disabled units.
Stargus also sets `RegenerationRate` on its Zerg unit/building definitions,
but its value is not used as the retail timing evidence.

The engine uses `units.dat` flag `0x80` and its existing fractional damage
(`sc.wound`), so regeneration heals both fractional and whole wounds without
a second HP representation. In-progress building morphs are excluded as the
engine's representation of an incomplete building; ordinary research does not
stop regeneration. This is an OpenBW-based implementation, not a claim that
all construction, stasis, or damage timing is verified against the executable.

Reproduce: `make build/bin/tests/starcraft/test_combat`, then
`env SDL_VIDEODRIVER=dummy build/bin/tests/starcraft/test_combat`.

### Terran building fire (2026-11-12)

**Confirmed in OpenBW, not independently executable-verified.** Pinned OpenBW
`bwgame.h` `update_unit_values` applies 20/256 HP direct damage per 24 Hz frame
to completed grounded or flying Terran buildings when the visible rounded-up
HP percentage, truncated to an integer, is at most 33. Its `unit_deal_damage`
does not check invincibility; weapon damage does that separately. Stargus's
Terran building definitions also implement burning, but use their own 30%
threshold and `BurnDamageRate` rather than this OpenBW timing.

The engine accumulates burn damage in `sc.wound`, routes whole damage through
the normal death path, and checks the threshold each frame so repair ends the
burn. Its construction model creates the building mobj only once completed.
Weapons and spell target filters retain their invincibility checks; the old
post-damage health restoration was removed so direct effects, including fire
and explicit scenario kills, can bypass them. The unused saved `guard_hp`
field was removed, so the save signature rejects prior incompatible StarCraft
saves. Fire visuals and native audio are not established by this change.

Reproduce with `make build/bin/tests/starcraft/test_combat` and run
`env SDL_VIDEODRIVER=dummy build/bin/tests/starcraft/test_combat`.

### Trigger energy and shield actions (2026-11-12)

**Superseded field conclusion:** the checked-in PyMS `FileFormats/TRG.py` normal-action
catalog assigns 50 to Modify Unit Energy and 51 to Modify Unit Shield Points.
Both use the same player, quantity, unit, location and percentage arguments as
Modify Unit Hit Points (49). `action_percentage` stores a value from 0 to 100
in the third action field, at byte +8 of the 32-byte CHK action. The quantity
is the modifier byte at +27. PyMS's `action_percentage` is inconsistent: its
compile path writes field 2 (+8), while its decompile validation reads field 5
(+20). The earlier +8 conclusion followed the former and was wrong.

**Corrected format, reference-defined:** eudplib `stockact.py` constructs
actions 49–51 with `percent` as the sixth dword, at +20, and `action.py`
documents the action layout. open-rts now reads +20. Mission fixtures put
different values at +8 and +20 to verify the correction. This is tool-source
evidence, not a retail executable trace; source links are in `REFERENCES.md`.

**Inferred implementation:** these actions set the selected units' energy or
shields to the specified percentage of their current maximum, initializing
unit combat state first. Energy uses the existing reactor-aware maximum and
shields use the native unit definition; values above 100 are clamped. The
retail handling of malformed percentages, fractional rounding, and upgraded
energy maxima has not been verified. Reproduce the selected-unit/count and
percentage checks with `env SDL_VIDEODRIVER=dummy build/bin/tests/starcraft/test_mission`.
PyMS `action_qnumber` encodes All as zero and finite quantities as 1–255;
All is not restricted to 256 objects. The checked OpenBW revision
`4b046d5f65302b10cb0a745f0fecd37ec85b20a8` has no action 50 or 51 case in
`bwgame.h::execute_trigger_action`, so it does not establish their semantics.
The engine starts combat state only on selected units that have the relevant
energy or shields; the native handling of ineligible units when a finite
quantity is specified remains unknown.

### Trigger resource amounts (2026-11-12)

**Confirmed reference behavior:** pinned OpenBW `bwgame.h` action 52 iterates
units in the chosen location, matches their owner and location elevation, then
calls `set_unit_resources` with the second group field (CHK action +20). That
setter affects resource units and changes mineral animation at resource
thresholds. PyMS identifies this action as Modify Unit Resource Amount and
encodes player, quantity, location and amount; a zero quantity means All.
OpenBW's case does not apply the quantity field, so retail finite-count
behavior remains unknown. **Superseded:** open-rts previously followed that
case and ignored the count. The eudplib `stockact.py` constructor instead
encodes a count at +27 and describes a bounded quantity; open-rts now honors
that field, with zero meaning All. This is a tool-reference choice rather than
a retail executable finding. It supports explicit neutral player 12 (slot 11)
and changes deposit stock rather than player stock. It does not reproduce
OpenBW's mineral threshold animation or elevation filtering; amounts above
INT_MAX are clamped to the engine's signed deposit range.
`env SDL_VIDEODRIVER=dummy build/bin/tests/starcraft/test_mission` exercises
neutral selection, one-unit and All counts, and a decoy value at +8.
The action has no unit-type selector: only resource unit types qualify. All
Players includes neutral slot 11. A built refinery takes over the geyser's
vent source in `game/g_game.c`; selection therefore uses the refinery owner's
slot. Restoring a refinery's stock reopens an inactive vent, while an unbuilt
geyser remains inactive. This vent transition is an engine representation of
resource-unit state, not a traced retail instruction.

### Trigger Order and Comment (2026-11-12)

**Confirmed reference:** pinned OpenBW `bwgame.h::execute_trigger_action`
case 46 selects matching player, unit type and source location and orders
mobile units to the destination location's center. Modifier 0 is Move, 1 is
Patrol and 2 is Attack Move. PyMS `TRG.py` identifies action 47 as Comment,
not Order. The previous engine treated 47 as an order and ignored 46. The
engine now dispatches those orders through its existing player-order path and
ignores Comment. This does not yet reproduce OpenBW's location elevation
filter, burrowed-unit unburrowing, or all of its special unit exclusions; patrol
uses the engine's tile waypoint representation. Run `env
SDL_VIDEODRIVER=dummy build/bin/tests/starcraft/test_mission` for all three
order modes, unit selection and the Comment regression.
The trigger patrol test runs through the destination and back to its starting
cell. Action 46 uses the stored MRGN bounds for location 64 (modified Anywhere)
when that record exists; the general `loc_box` helper retains its whole-map
handling of IDs 0 and 64 for its other callers. The order path also applies
location elevation exclusions and unburrows selected burrowed units. Patrol
still uses the engine's tile waypoint representation; other special-unit
exclusions and exact retail order behavior remain unverified.
