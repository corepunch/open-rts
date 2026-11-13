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
The compiler's `op == 42` branch ends a worker's swing, but 42 is
`gotorepeatattk` (its comment says `engframe`, 43): left as it was, because
changing it moves worker swing visuals and no trace says which was intended.

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

| weapons.dat behavior | OpenBW state | This engine | Difference |
|---|---|---|---|
| 0 fly | `state_move`, aim fixed at creation, miss roll source 1 | `projectile_tick`, fixed aim, `weapon_misses` | none found |
| 1 follow | `state_follow`, re-aims at the target each frame unless it missed | same | none found |
| 7 bounce | `state_bounce`, 3 arrivals, divisors 1, 3, 9, search 96 px square, no lifetime | same divisors and count; `WEAPON_BOUNCE_RANGE` scan in thinker order | candidate order: OpenBW takes the first unit its unit finder yields, this engine the first thinker; which is first is not established |
| 2 appear on target | Init sigorder, then Death; damage on the Death script's `domissiledmg` | a pending-hit shot mobj deals damage on the Death script's `domissiledmg` frame | none left: 0 frames for most, 1 (Arclite Shock Cannon), 3 (Psionic Shockwave), 5 (Subterranean Tentacle, Acid Spray); see "Delayed damage" |
| 3 persist | `state_damage_over_time`, hit every 7 frames | spell code | not compared |
| 4, 5, 6 | Death script, `domissiledmg` at frame 5 for the shared placeholder flingy 172 | not projectile weapons here | Flame Thrower, Nuclear Strike, Suicide, Scarab, Spider Mine delay 5 frames in OpenBW |

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

Not done: behavior 5 and 6 (Flame Thrower, Suicide, Scarab, Spider Mine, Nuclear
Strike, Warp Blades) also delay `domissiledmg` by 5 frames after the same
flingy-172 Init, but the engine does not create those bullets: Firebat flames
are dealt by the shared attack path around the target, and the others are units
or orders with their own code. Delaying them needs the OpenBW bullet geometry
(appear at the source, `forward_offset`, splash around that point) first; no
trace here establishes that the engine's target-centred flames equal it.

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

StarCraft save layout is now 22 (18: the script cursor is part of the mobj; 22: the pending-hit flag).
