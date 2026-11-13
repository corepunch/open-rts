# StarCraft projectile IScript visuals

## Evidence

The pinned local Stargus IScript listing (`reference/stargus/doc/iscript.txt`)
provides concrete projectile examples:

- Fragmentation Grenade, image 532 / IScript 242: its ground-attack path
  spawns sprite 310 (the `Fragmentationgrenadesmoke` image 441), and Death spawns image 440
  (`Fragmentationgrenadehit`) before `domissiledmg`.
- Wraith Burst Lasers, images 535/534 / IScript 243: Death spawns image 447
  (`LaserHit`) before `domissiledmg`.
- Mutalisk Glaive Wurm, image 511 / IScript 264: the ground-attack path
  spawns sprite 367 (image 513, `SpoTrail`) across its ten-frame loop.
  Death spawns sprite 365 (image 512, `SporeHit`) before `domissiledmg`.

The `domissiledmg` opcode is not copied from IScript: the current projectile
simulation applies damage at its own native-signaled impact point. IScript is
used here as the source for the visual chain and trail image. Stargus and the
asset script are references, not independent traces of `STARCRAFT.EXE`.

## Implementation

`games/starcraft/w_assets.c` now compiles missile Death animations and their
`imgol`/sprite overlays alongside the existing projectile image animation.
It also compiles the first native ground-attack `sprol` sprite into an effect
state. Sprite creation commands are counted while the script path is compiled:
single-command trails such as the grenade's smoke spawn once, while the Glaive
Wurm's repeated command spawns on successive 24 Hz projectile frames. Duplicate
weapons sharing a flingy reuse the same visual states.

`games/starcraft/p_combat.c` spawns trail images as ordinary nonblocking
projectile-type mobjs whose IScript states expire through `S_NULL`. On impact,
the projectile enters its compiled Death chain and remains until that chain
ends. Its selected death and trail state IDs, repetition flag and emitted
flag are in `sc_projectile_t`; the game's mobj save and consistency mechanisms
include these fields.

Intermediate Glaive Wurm hits now spawn the compiled Init state for image 512
(`SporeHit`) when a following bounce target is acquired. This is sourced from
`GlaveWurmSpecialState1`, which emits `sprol 365` before `domissiledmg` and
returns to its ground-attack script. The visual state is located in the
already compiled projectile Death chain, so no projectile save fields or save
layout changes are added for this visual.

The presentation regression loads the native assets and compiles IScript,
then checks grenade/laser impact images, the Glaive trail, repeated versus
one-shot trail behavior, projectile death-state lifetime, and the intermediate
Glaive hit image's renderability, initial frame and eventual thinker removal.
It also retains flight and save/reload checks.

Run:

```sh
make build/bin/tests/starcraft/test_projectiles
env SDL_VIDEODRIVER=dummy build/bin/tests/starcraft/test_projectiles
```

## Opcode inventory

`tests/starcraft/test_iscript_inventory.c` walks the real `iscript.bin` from
every image the game draws: the 228 unit rows, every weapon's bullet flingy,
and, transitively, each image, sprite, flingy or weapon a reachable script
spawns (`imgol` family, `sprol` family, `uflunstable`, `useweapon`,
`imgulnextid`). It follows `goto`, `call` and both arms of every conditional
jump from all animation entry points, and counts each reachable instruction
site once: 431 images, 6799 instruction sites, 56 distinct opcodes. The same
test asserts that no `wait` is zero and no `waitrand` has `hi < lo` (the cases
where OpenBW's arithmetic leaves its defined range). `--table` prints the
table below; "w_assets.c" is how the visual state compiler treats the opcode,
"p_iscript.c" what the runtime added by this work does.

StarCraft IScript: 431 images, 6799 instruction sites, 56 opcodes in use
| Opcode | Name | Uses | w_assets.c (visual states) | p_iscript.c (runtime) |
|---|---|---|---|---|
| 0x00 | playfram | 2264 | yes | reported, not acted on |
| 0x01 | playframtile | 2 | mis-handled: frame without the tileset offset | reported, not acted on |
| 0x03 | setvertpos | 28 | ignored | reported, not acted on |
| 0x05 | wait | 2389 | yes | executed (OpenBW timing) |
| 0x06 | waitrand | 77 | mis-handled: compiled as a wait of its minimum | executed (OpenBW timing) |
| 0x07 | goto | 529 | yes | executed (OpenBW timing) |
| 0x08 | imgol | 175 | partial: first image of a death or bullet script | reported, not acted on |
| 0x09 | imgul | 121 | ignored | reported, not acted on |
| 0x0a | imgolorig | 1 | ignored | reported, not acted on |
| 0x0b | switchul | 5 | ignored | reported, not acted on |
| 0x0d | imgoluselo | 8 | partial: as imgol | reported, not acted on |
| 0x0f | sprol | 43 | partial: first sprite of a death, trail or impact | reported, not acted on |
| 0x11 | lowsprul | 18 | partial: as sprol | reported, not acted on |
| 0x13 | spruluselo | 1 | partial: as sprol | reported, not acted on |
| 0x14 | sprul | 1 | partial: as sprol | reported, not acted on |
| 0x15 | sproluselo | 2 | partial: as sprol | reported, not acted on |
| 0x16 | end | 191 | yes | executed (OpenBW timing) |
| 0x17 | setflipstate | 7 | ignored | reported, not acted on |
| 0x18 | playsnd | 135 | ignored: first sound only, in sounds.c | executed, plays its row |
| 0x19 | playsndrand | 2 | ignored: first sound only, in sounds.c | executed, plays its row |
| 0x1a | playsndbtwn | 12 | ignored: first sound only, in sounds.c | executed, plays its row |
| 0x1b | domissiledmg | 28 | yes: strike | reported, not acted on |
| 0x1c | attackmelee | 6 | yes: strike | executed, plays its row |
| 0x1d | followmaingraphic | 3 | ignored | reported, not acted on |
| 0x1e | randcondjmp | 17 | ignored: the jump is never taken | executed (OpenBW timing) |
| 0x1f | turnccwise | 38 | ignored | reported, not acted on |
| 0x20 | turncwise | 26 | ignored | reported, not acted on |
| 0x21 | turn1cwise | 1 | ignored | reported, not acted on |
| 0x22 | turnrand | 6 | ignored | executed, draws its roll |
| 0x23 | setspawnframe | 1 | ignored | reported, not acted on |
| 0x24 | sigorder | 97 | ignored | executed, ends the game cursor |
| 0x25 | attackwith | 31 | yes: strike | reported, not acted on |
| 0x26 | attack | 6 | yes: strike | reported, not acted on |
| 0x27 | castspell | 12 | ignored | reported, not acted on |
| 0x28 | useweapon | 3 | yes: strike | reported, not acted on |
| 0x29 | move | 200 | ignored | reported, not acted on |
| 0x2a | gotorepeatattk | 50 | partial: ends a worker swing | reported, not acted on |
| 0x2b | engframe | 4 | ignored | reported, not acted on |
| 0x2c | engset | 2 | ignored | reported, not acted on |
| 0x2e | nobrkcodestart | 51 | ignored | reported, not acted on |
| 0x2f | nobrkcodeend | 49 | ignored | reported, not acted on |
| 0x30 | ignorerest | 21 | partial: ends the path | branch answered by the game |
| 0x31 | attkshiftproj | 9 | ignored | reported, not acted on |
| 0x32 | tmprmgraphicstart | 23 | ignored | reported, not acted on |
| 0x33 | tmprmgraphicend | 23 | ignored | reported, not acted on |
| 0x34 | setfldirect | 27 | partial: frames stay unflipped | reported, not acted on |
| 0x35 | call | 6 | yes | executed (OpenBW timing) |
| 0x36 | return | 1 | yes | executed (OpenBW timing) |
| 0x37 | setflspeed | 2 | ignored | reported, not acted on |
| 0x38 | creategasoverlays | 22 | ignored | reported, not acted on |
| 0x39 | pwrupcondjmp | 12 | ignored: never taken | branch never taken |
| 0x3a | trgtrangecondjmp | 3 | ignored: never taken | branch never taken |
| 0x3b | trgtarccondjmp | 2 | ignored: never taken | branch never taken |
| 0x3c | curdirectcondjmp | 3 | ignored: never taken | branch never taken |
| 0x3d | imgulnextid | 1 | ignored | reported, not acted on |
| 0x3f | liftoffcondjmp | 2 | ignored: never taken | branch answered by the game |
PASS: iscript.bin opcode inventory

Statuses: "yes" is handled as OpenBW does for the visual; "partial" feeds only
the death, bullet or trail effect chain; "ignored" has no visual effect in the
compiled states; "mis-handled" compiles to something other than the opcode's
meaning (`playframtile` omits the tileset offset; `waitrand` waits its minimum).
The compiler's `op == 42` branch ends a worker's swing at `gotorepeatattk` (42;
`engframe` is 43). The branch was right and its comment wrong: the SCV, Drone
and Probe attack scripts (`GndAttkInit`, `GndAttkRpt`) end the blow with
`gotorepeatattk` (OpenBW clears movement flag 8 there) and then `ignorerest`,
which ends the compiled path anyway, and none of them uses `engframe`. The
comment is fixed and `worker_swings` in `test_iscript` pins it: the opcode
names, the call on which the real script reaches opcode 42 (SCV 3, Drone 1,
Probe 1 waits) and the compiled harvest chain having exactly that many states
before it returns to idle.

## Runtime (`games/starcraft/p_iscript.c`)

`sc_script_frame` runs one 24 Hz game frame of a program exactly as OpenBW's
`iscript_execute` does: opcodes execute until a `wait`/`waitrand` (resume `n`
frames later: OpenBW stores `n - 1` and counts it down) or an `end`. Opcode
lengths and operand types are PyMS `IScriptBIN.py` `OPCODES`; the old private
length table in `w_assets.c` is gone. `call` keeps a single return address, as
OpenBW does (the visual compiler's 16-deep stack was never needed: the inventory
test asserts that the six calls all enter one subroutine that holds none). Random opcodes, evidence `iscript_execute` in OpenBW:

| Opcode | Draw (OpenBW `lcg_rand` source) | Result |
|---|---|---|
| `waitrand a b` | 3 | wait `a + (r & 0xff) % (b - a + 1)` frames |
| `playsndrand n ...` | 4 | row `ids[r % n]` |
| `playsndbtwn a b` | 5 | row `a + r % (b - a + 1)` |
| `attackmelee n ...` | 4 | blow reported first, then as `playsndrand` |
| `turnrand n` | 6 | turn the other way when `r % 4 == 1` |
| `randcondjmp c t` | 7 | jump when `(r & 0xff) <= c` |

All of them use OpenBW's synchronized `lcg_rand`, the same LCG `sc_random`
already implements (triggers and launch rolls share it), so in the game every
draw goes through `sc_random` and is part of simulation state: no script
random is kept out of it. `playsnd` (fixed row) and the other opcodes draw
nothing. OpenBW draws for a sound opcode whether or not anything is audible;
so does this runtime. The 15-bit result and the call-site sources are
OpenBW's; the source index only labels the draw in OpenBW's statistics.

World-dependent conditions (`liftoffcondjmp`, `ignorerest`, `pwrupcondjmp`,
`trgtrangecondjmp`, `trgtarccondjmp`, `curdirectcondjmp`) ask the host. The
game host answers `liftoffcondjmp` (flying) and `ignorerest` (no target: take
the idle path) and never takes the others; no animation the game starts with a
cursor (below) contains them, but this is a stated approximation, not retail
behavior.

### Where the game uses it

A mobj carries an `sc_script_t` (`pc`, `ret`, `wait`, `anim`, `active`; hashed
with the mobj, saved with it). `sc_script_begin` starts an animation of the
unit's or bullet's image script and runs its first frame at once, as
`iscript_run_anim` does; `sc_mobj_ticker` then advances it by the engine's
24 Hz frame cadence. Started by: a bullet entering its Death
(`projectile_impact`), a bounce (`SpecialState1`), Burrow, UnBurrow, Lift Off
and Landing (`p_spell.c`, `p_order.c`). The game host plays sounds and stops
the cursor at `sigorder`: the animation has then signalled its order done and
what follows is the idle loop, which this runtime does not play (so scripts'
idle `waitrand`/`randcondjmp` do not draw here, unlike OpenBW; that makes the
RNG sequence differ from retail after these animations).

Verified timings from the real script (`test_iscript`): Lift Off plays row 471
on its first frame, Landing row 472 on frame 18 (frame 15 to 25 for the other
Terran buildings), a Spider Mine's Burrow and UnBurrow row 354 on frame 0, a
Glave Wurm's bounce row 91 and final Death row 93 on frame 0 of their
animations. Zerg burrow scripts contain no sound. Rows are loaded with the bank
because the sound loader walks every reachable `playsnd` operand of these
animations (`sounds.c`).

Frame to tick mapping is the engine's existing one: script frame `k` after a
start runs on the `k`-th tick whose 24 Hz frame counter advances. The compiled
visual states still round each wait separately, so a sound can differ from the
matching picture by up to one 30 Hz tick.

## Weapon behaviors against OpenBW

Compared with OpenBW `initialize_bullet`, `bullet_state_*`, `bullet_hit` and
the Death scripts of the installed `iscript.bin`:

Evidence: OpenBW `fire_weapon`, `initialize_bullet`, `create_bullet`, `bullet_execute` and its `bullet_state_*`, `bullet_hit`, `bullet_deal_splash_damage`; weapons.dat names are PyMS's. Every bullet is created inside the attacker's script (`attackwith`, `attkshiftproj`, `useweapon`, `castspell`), then its Init script signals and it flies or dies as below; its damage runs when its Death script reaches `domissiledmg` (frame 0 for fly/follow/bounce weapons, 1 to 5 as listed for the rest).

| id | name (OpenBW state) | start position and target point | movement | Death/damage | users in the supported data |
|---|---|---|---|---|---|
| 0 | fly (`state_move`) | attacker position + `forward_offset` along heading, y - `upward_offset`; heading turned by `bullet_heading_offset` (random side, alternating per source); aimed at the target position; miss roll `lcg_rand(1)&255 <= miss chance` (only with a target unit and an owner) sets hit flag 1 and moves the aim 30 px back | flingy movement until position == target or `lifetime` runs out | Death frame 0; `bullet_hit` by hit type | Grenades, Gemini/Burst, ATS/ATA, Interceptor Pulse, trap/merc missiles, Yamato, EMP, Venom |
| 1 | follow (`state_follow`) | as 0 | re-aims at the target every frame unless missed; target gone -> `state_move` to the old point | Death frame 0 | Hellfire, Longbolt, Acid Spore, Seeker Spores, Phase Disruptor(s), Anti-matter, Photon Cannon, Lockdown, Parasite, Broodlings, Ensnare |
| 2 | appear on target unit | `get_bullet_appear_at_target_pos` (line from the target's quarter-shrunk box centre toward the attacker, clipped to the box); roll as 0 (miss: 30 px back toward the attacker) | none; Init -> `bullet_kill` | Death frame 0, 1 (Arclite Shock Cannon), 3 (Psionic Shockwave), 5 (Tentacle, Acid Spray) | Gauss, C-10, Twin Autocannons, Arclite, Needle Spines, Spines, Tentacle, Particle Beam, Psi Assault, Dual Blasters, Shockwave, Irradiate, Consume |
| 3 | persist on target site (`state_damage_over_time`) | the target unit's or order point | none; `bullet_hit` every 7 frames for `lifetime` | repeated | Psionic Storm (spell) |
| 4 | appear on target site | the order point (`fire_weapon`); roll as 0 | none | Death | Plague (spell) |
| 5 | appear on attacker | attacker position + `forward_offset` (an `attkshiftproj` operand replaces it), y - `upward_offset`; `useweapon` uses the order point instead; no roll | none | Death frame 5 for the placeholder flingy 172 | Flame Thrower (25, 26, 98), Spider Mines, Suicide (Infested Terran), Scarab, Nuclear Strike, Warp Blades (85, 86), Stasis Field, Dark Swarm; melee rows (Claws, Blades, Spores) have no flingy and strike on `attackmelee` |
| 6 | attack and self-destruct | as 5; `initialize_bullet` kills the owner at once | none | Death frame 5 | Suicide (Scourge) |
| 7 | bounce (`state_bounce`) | as 0; 3 arrivals | follows target, 96 px square search | Death frame 0 per arrival, divisors 1, 3, 9 | Glave Wurm |
| 8, 9 | attack nearby area, extend to max range | (table of 14 offsets; max range + 20) | | | no weapon row uses them |

What `bullet_hit` strikes: hit type 1 (normal) the bullet's target if it still has one and the roll did not miss (divisor 1; 3 and 9 for bounces). Types 2, 3 and 5 (radial, enemy, nuclear) run `bullet_deal_splash_damage` around the bullet's position: every unit except the bullet owner's unit that the weapon's target flags allow (air 1, ground 2, mechanical 4, organic 8, non-building 0x10, non-robotic 0x20, 0x80) whose box lies within the outer radius takes full damage inside the inner radius, and, unless burrowed, half inside the medium and a quarter inside the outer; an enemy splash (type 3) skips units of the bullet's own player except its target, a radial one does not. The remaining hit types are spells.

For behaviors 0, 1 and 7 every Death script deals damage on its first frame
(asserted for all weapons in `test_iscript_inventory`), so damage at the
impact tick matches OpenBW. Behavior 8 (3x3 area) and 9 (max range) have no
weapon with damage in this table. Behavior 2 is implemented below. Behavior 5
and 6 are not (see "Delayed damage").

## Delayed damage (behavior 2)

Evidence is OpenBW: `fire_weapon` and `create_bullet` make the bullet inside
the attacker's `iscript_execute` (an `attackwith` opcode); `process_frame` then
runs `update_units` and `update_bullets` in one frame, and `update_bullets`
executes the new bullet's sprite script (its Init, which is `wait 1` then
`sigorder 1`) and `bullet_execute`, whose `bullet_state_init` sees the signal
and calls `bullet_kill`, i.e. `sprite_run_anim(Death)`, which runs Death frame
0 at once. So Death frame 0 runs on the firing frame, not one frame later (the
"one-frame Init wait" of the first batch is absorbed in the same frame), and
`domissiledmg` (`bullet_hit`, via `bullet_deal_damage` or `bullet_deal_splash_damage`)
runs on Death frame k, k = 0, 1, 3 or 5 as in the table. The Death scripts of
the installed `iscript.bin` give k (from `test_iscript_inventory`): Gauss Rifle,
Needle Spines and the other single-frame bullets 0; Arclite Shock Cannon 1;
Psionic Shockwave 3; Subterranean Tentacle and Acid Spray 5.

The engine now fires an appear-on-target weapon (`immediate_fire` in
`p_combat.c`) by spawning the existing `MT_SC_PROJECTILE` mobj at the bullet
position with `sc_projectile_t.pending` set and starting its Death script
(`sc_script_begin`). The mobj carries the pending hit: its owner, team and
allegiance, the hallucination flag and the miss roll are taken at the firing
tick (OpenBW `initialize_bullet`, which rolls once per bullet); its source and
target are the mobj's own `target` and `attack.target`. `domissiledmg`
(`host_op` in `p_iscript.c`, opcode 27 or `dogrddamage` 68) calls
`sc_projectile_script_hit`, which deals the damage and removes the mobj; no
new pool. Script frame k runs on the k-th tick whose 24 Hz counter advances
after the firing tick (the shot's own tick in the firing tick does not advance
the script), so frame 0 damage is still on the firing tick.

At the hit, as OpenBW does: upgrades are read then (`weapon_damage_amount` at
`bullet_hit`), not at the firing tick; a target removed before the frame has
been cleared from the bullet (`remove_target_references`), so a single-target
hit deals nothing, while a target that moved is still hit; a splash hit
(`bullet_deal_splash_damage`) is centred on where the bullet appeared and
measures each unit's distance now, so units that moved in or out count and a
removed target only drops out; an attacker removed before the frame no longer
excludes itself from splash, and the damage is credited to the bullet's
player. The shot's own sound rows are not played by the script (`host_sound`):
sounds.c already plays a weapon whose attack script is silent from its bullet's
sounds at the firing tick, and in the installed script those sounds sit on
frame 0 of Init or of Death (Tentacle: Init, the rest: Death), the firing frame.

If `iscript.bin` is not loaded or the weapon has no script, the hit is dealt at
the firing tick as before. A saved pending shot is validated against its weapon
row (`G_SavedObjectValid`). Save layout 22 (the `pending` flag).

Behavior 5 Flame Thrower is done (next section). Not done: Suicide, Scarab, Spider Mine, Nuclear
Strike, Warp Blades (all behavior 5 or 6) also delay `domissiledmg` by 5 frames after the same
flingy-172 Init, but the engine does not create those bullets: Firebat flames
are dealt by the shared attack path around the target, and the others are units
or orders with their own code. Delaying them needs the OpenBW bullet geometry
(appear at the source, `forward_offset`, splash around that point) first; no
trace here establishes that the engine's target-centred flames equal it.

## Flame Thrower (behavior 5, `test_flames`)

The Firebat, Gui Montag and the wall Flame Traps run `attkshiftproj 24`, `wait 2`, `attkshiftproj 52`, `wait 1`, `attkshiftproj 80` after one frame in GndAttkInit and GndAttkRpt (asserted from `iscript.bin` by `test_flames`). `attkshiftproj` is OpenBW `attack_with_forward_offset`: it makes the bullet only if the attacker still has an order target. So an attack makes three bullets on script frames 1, 3 and 4, 24, 52 and 80 px ahead of the attacker along its heading, and each strikes on frame 5 of its Death script (frames 6, 8 and 9 after the attack tick), centred where it appeared, with weapons.dat's 15/20/25 px rings (enemy splash, ground only). `sc_projectile_fire` spawns the three pending shots with `sc_projectile_t.delay` (frames before the attacker's script makes it) and `shift`; at that frame the attacker must be alive, enabled and have a living target, otherwise nothing is made. Without `iscript.bin` all three are made at once. The Firebat's old `hits = 2` stand-in is no longer used for these (it stays as the unit's display value).

The splash now follows OpenBW for the existing behavior 2 splash weapons too: the enemy splash spares units of the bullet's own player rather than allies, the weapon's target flags decide who can be hit, and a burrowed unit takes only the inner ring. Wall Flame Trap heading comes from `setfldirect` in OpenBW; this engine does not run that opcode, so traps flame along the engine's facing (unverified). Save layout is now 26 (`delay`, `shift`).

## Unit idle and walk loops: still stopped at sigorder

Second batch: continuing the cursor past `sigorder` was not done, because the draw sequence cannot be shown to match OpenBW's. In `update_units` OpenBW runs, per frame, the dead units, the movement of every visible unit, then for each visible unit in list order its values, main order, secondary order, subunit and, last, `iscript_execute_sprite` over every image of its sprite (main image, shadow, overlays and glow images each with their own script and their own `waitrand` or `randcondjmp`), and only afterwards the hidden units, bullets and thingies; every `lcg_rand` of those orders and scripts draws from the one stream. Matching it needs three things this engine lacks: the unit list order (it depends on units moving between OpenBW's visible and hidden lists, not on creation order alone); the scripts of every image of every sprite, where only the main image's script is known here (shadow and overlay images are not run), so the number of draws per frame would differ; and the same order of the other `lcg_rand` consumers (unit orders, acquisition, AI) around the scripts, where `sc_mobj_ticker` runs the script first and its own orders after. `sc_random` already serves launch rolls and triggers in an order of its own, so a partially matching stream would be worse than the documented limit; unit scripts stop at `sigorder` as before. A trace of per-frame `lcg_rand` source and count from OpenBW over a recorded game, set against the engine's `sc_random` calls on the same orders, would be the evidence to settle it.

## Glaive Wurm bounce target

OpenBW `bullet_state_bounce` calls `find_unit_noexpand(square_at(glaive position, 96), pred)`, which returns the first unit of `find_units_noexpand`: `unit_finder_search` walks `state.unit_finder_x`, the array of every non-turret unit's inner box (`unit_sprite_inner_bounding_box`: position minus and plus `dimensions`) inserted twice, at its left and its right x, sorted by that value (`unit_finder_insert`, `lower_bound`), over the entries with value in `[from.x, to.x)` of the square. An entry counts if its unit's box starts left of the square's east side and overlaps it in y (`in_bounds`), and a unit already seen through its other entry is skipped. The predicate is `unit_can_attack_target`, an enemy (`unit_target_is_enemy`), not the current or previous target. So the target is the qualifying unit with the smallest of: its box left edge if that is at or east of the square's west side, else its right edge.

`bounce_target` in `p_combat.c` computes exactly that key from `units.dat` boxes (`sc_unit_t.bounds`) instead of the old rule (first thinker whose centre is within 96 px on both axes), without a spatial finder. Test: `bounce_order` in `test_projectiles` (two candidates in either creation order, the box rather than the centre east and north, the square's edges, a unit west of the square whose right edge is inside it). Not established: units whose entries have the same x come out of OpenBW in the reverse of the order they entered the finder (`lower_bound` inserts before equal values), a history this engine does not keep; they stay in thinker order. The engine's own `P_CanTarget` and `P_VisibleTo` filters replace `unit_can_attack_target` (which has no fog test); that difference is unchanged.

## Limits

The visual compiler (`animation` in `w_assets.c`) is unchanged: it still follows
the first path of a script, so branches and random waits do not affect
picture timing. Only the cursor runtime above executes them, and only for
sounds. Trail emission is advanced at projectile frame cadence. Bounce visuals
are connected for the confirmed Glaive Wurm `SporeHit`; other bounce weapons
are unverified. The evidence is Stargus/OpenBW source and native asset data,
not a retail executable trace.

## Verification

```sh
make build/bin/tests/starcraft/test_iscript build/bin/tests/starcraft/test_iscript_inventory build/bin/tests/starcraft/test_delayed_damage
env SDL_VIDEODRIVER=dummy build/bin/tests/starcraft/test_iscript
env SDL_VIDEODRIVER=dummy build/bin/tests/starcraft/test_delayed_damage
env SDL_VIDEODRIVER=dummy build/bin/tests/starcraft/test_iscript_inventory --table
env SDL_VIDEODRIVER=dummy TEST_JOBS=4 TEST_BUDGET=60 make -o starcraft-maps test-starcraft
```

StarCraft save layout is now 26 (18: the script cursor is part of the mobj; 22: the pending-hit flag; 26: flame delay and shift).

Not done in this batch: Suicide (Scourge, Infested Terran: OpenBW kills the Scourge at its `attackwith 2` and the Infested Terran blasts at its own position from `SpecialState1`; the engine blasts around the target), Spider Mine (castspell in SpecialState1, bullet at the mine, mine killed next order frame), Scarab (bullet at the Scarab via `useweapon 82` when within `inner_splash/2` = 10 px, spawn 25 px along the Reaver heading, only one Scarab outside at a time, a Scarab whose target dies flies on to its last move target and explodes there), Warp Blades (single-target hit 5 frames after `attackwith`). All traced above, none implemented.
