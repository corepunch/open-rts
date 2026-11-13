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

The presentation regression loads the native assets and compiles IScript,
then checks grenade/laser impact images, the Glaive trail, repeated versus
one-shot trail behavior, and projectile death-state lifetime. It also retains
flight and save/reload checks.

Run:

```sh
make build/bin/tests/starcraft/test_projectiles
env SDL_VIDEODRIVER=dummy build/bin/tests/starcraft/test_projectiles
```

## Limits

This does not interpret every IScript opcode. Trail emission follows the
compiled ground-attack path and is advanced at projectile frame cadence; it
does not reproduce arbitrary script branches, per-command timing gaps, or
native sound playback. Intermediate Glaive Wurm hit visuals from
`SpecialState1` are not yet connected to bounce transitions. The evidence is
Stargus/OpenBW source and native asset data, not a retail executable trace.
