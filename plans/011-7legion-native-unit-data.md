# Plan 011 — Import native 7th Legion unit data into `games/7legion`

Follow-up to commit `c6ce0d8` ("Decode 7th Legion unit stats from legion.exe
and stuff.dat"). Dark Reign (`6c9fd45`) and KKnD (`2fbc33f`) now author their
actor stats from retail data. 7th Legion still uses seven invented
placeholder actors. Read [`AGENTS.md`](../AGENTS.md),
[`REVERSE_ENGINEERING.md`](../REVERSE_ENGINEERING.md) and
[`docs/7LEGION_EXE_FINDINGS.md`](../docs/7LEGION_EXE_FINDINGS.md) §
"Unit and building stat tables" before starting.

## What is already known (confirmed)

`make 7legion-units` runs
[`tools/7legion_units_extract.c`](../tools/7legion_units_extract.c). It
prints the stats the retail game loads: the `legion.exe` defaults overwritten
by `DATA/stuff.dat`.

- Vehicle ids 0..43 index the `vt_` names (`vt_carrier`, `vt_truck`, …,
  `vt_mobilebase`, `vt_aspider`).
- Per vehicle: cost, build time, health, armour (Body/Light/Medium/Heavy/
  Structure), weapon and weapon 2, speed (16.16), turn speed, reload and side
  (0 = `7L`, 1 = `CH`).
- Buildings (id >= 1000, `bt_` names): cost, build time, health, armour and
  weapon.
- The weapon × armour damage table (`damage[class*20 + armour] >> 16`).

## What is unknown (must be traced in `data/7LEGION/legion.exe`)

1. **Sprite mapping.** Which BIM files each `vt_` id draws. The renderer
   switches on type at `0x004266f6` (`movsx esi, word [edi+0x67fe12]`,
   `sub esi,2`, byte table `0x00426ec8`, jump table `0x00426e8c`). BIM handles
   are loaded into globals, e.g. `GFX\ltroop.bim` → `0x6dd2d4` (`0x00412e1c`),
   `GFX\mobbase.bim` → `0x804308`, `GFX\spider.bim` → `0x6ccad4`,
   `GFX\rockmech.bim` → `0x6e5c20`, `GFX\truck.bim` → `0x6dd2e4`,
   `GFX\slaven1.bim` → `0x70db1c`. Follow those globals' readers back to the
   type switch.
2. **Units of speed, turn and reload.** Find the movement code that consumes
   vehicle `+0x1c` (16.16 speed) and `+0x20` (turn speed), and the code that
   consumes `+0x12` (reload). Establish whether speed is pixels per tic and
   what a reload count measures.
3. **Simulation tic rate.** Find the timer that drives the simulation step
   (compare Dark Reign's `GameSpeed`, `1000 / speed` ms in
   `docs/DR_EXE_FINDINGS.md`).
4. **Weapon range.** Range is not in the stats tables. Find where a weapon's
   range is read when choosing or validating targets (start from the damage
   function `0x00471280` and its callers).
5. **Mission placement (lead, unverified).** `Missions.ini` `PVStart` strings
   are 43 digits. The first mission has `4` at index 14 (`troop1`) and `1` at
   index 42 (`mobilebase`). This suggests per-`vt_` id starting counts. Trace
   the parser before relying on it.

## Tasks

- [ ] Trace items 1–4 and record each address, layout and formula in
      `docs/7LEGION_EXE_FINDINGS.md`. Mark each finding confirmed, inferred or
      unknown. Keep any decompiler output under ignored `reverse/`.
- [ ] Replace the seven placeholder `ACTOR_TYPES` in
      `games/7legion/g_game.c` with actors keyed by native `vt_`/`bt_` id. Do
      not reuse the old invented stats.
- [ ] Author stats as C literals through small conversion macros, like
      `DR_SPEED` and `KK_CELLS`/`KK_MS`/`KK_TURN`. Use native values in the
      macro arguments, e.g. `.speed = SL_SPEED(0x20000)`, `.max_hp = 70`.
      Each macro's one-line comment names the instruction address that
      justifies the conversion. Cells are 32 px (`TILE_W`).
- [ ] Damage against five armour classes: either extend the shared
      `attack.versus` (currently 3 entries, used by KKnD) to 5 and set victim
      `armor_class` from the native armour, or justify an alternative in the
      findings doc. Zero entries must keep falling back to `attack.damage` so
      other games are unaffected.
- [ ] Update `tools/7legion_info_gen.c` so `info.c`/`info.h` cover the new
      actors and their mapped BIM sprites. Regenerate, and keep
      `make test-info-gen` byte-identical.
- [ ] If item 5 is confirmed, spawn mission units from `PVStart` instead of
      the synthetic force in `sl_load_initial_units` (`games/7legion/w_map.c`).
- [ ] Add `tests/7legion/test_native_stats.c`. Like
      `tests/kknd/test_units_cfg.c`, it reads `legion.exe` + `stuff.dat` and
      checks every authored actor against the native row (health, cost,
      armour, weapon damage per class, converted speed/turn/reload).

## Rules

- No stats without native evidence. If a unit's sprite or a value's unit
  cannot be traced, leave the actor out and record the gap. Do not infer the
  mapping from file-name similarity alone; for example, `LTROOP`/`troop1` is a
  lead, not proof.
- All tools in C; never list individual sources in the Makefile beyond the
  existing per-tool pattern; `#ifndef __NAME__` header guards.
- Commit messages follow the what/how/why format in `AGENTS.md`.

## Verification

```sh
make && make 7legion-units
env SDL_VIDEODRIVER=dummy make test-7legion test-info-gen
env SDL_VIDEODRIVER=dummy build/bin/7legion --check
env SDL_VIDEODRIVER=dummy build/bin/7legion --screenshot /private/tmp/open-rts-7legion.bmp
```

`test-dark-colony` currently has six failures that also occur on `6c9fd45`.
Do not count them as regressions, but do not add new ones.
