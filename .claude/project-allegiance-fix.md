# Dark Reign allegiance fix (PR #30)

## The bug

All units loaded from `.SCN` maps had `mobj->allegiance == 0`
(`ALLEGIANCE_PLAYER`) because `games/dark-reign/w_map.c` never set the field.

`P_AreAllegiancesAllied(0, 0)` returns `true`, so `P_IsAlly(player, enemy)`
was always true. Both `attack_target_in_range` and `P_Attack` skip allied
targets — no unit ever fired.

## The fix

In `games/dark-reign/w_map.c` after `unit->team = unit->owner`, add:

```c
unit->allegiance = unit->owner == 0 ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY;
```

## Scope

Any code that spawns units for a non-player team must set `allegiance`.
KKnD does this correctly in `games/kknd/g_game.c:447`. Production-spawned units
inherit allegiance from the producer (`game/g_game.c:272`).

## Why KKnD combat worked without this

KKnD map units are spawned in `g_game.c` (not a per-game `w_map.c`) and the
enemy spawn loop explicitly sets `unit->allegiance = ALLEGIANCE_ENEMY`.

## Symptom to watch for

If a new game's combat test soft-passes with 0 attacks, check allegiance first.
`P_AreAllegiancesAllied` is the gating function.
