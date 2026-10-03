# 7th Legion loader evidence

## Unit and building stat tables (2026-10-02)

Reference: `data/7LEGION/legion.exe`, SHA-256
`a312f7b50a940e5a0ec737cf8923c1d03f552f9c9111c62c72546bbb46f6c154`
(PE32, compiled 1997-09-01).

**Confirmed: stuff.dat overrides built-in tables at startup.** `0x004023a3`
calls `0x004716e0`, which opens `data\stuff.dat` ("rb") and overwrites the
built-in tables. `0x004714b0` writes the same layout ("wb") for the editor's
Save button. The file is a sequence of records ended by -1, followed by the
1600-byte damage table at `0x004b5ee8`:

- vehicle `id` < 1000, ten int32s: id, cost, armour, weapon, weapon 2,
  speed (16.16), turn speed, health, build time, reload.
- building `id` >= 1000, six int32s: id, cost, armour, weapon, health,
  build time.

The installed file has 21 vehicle and 10 building records
(21*40 + 10*24 + 4 + 1600 = 2684 bytes) and ends exactly after the damage
table.

**Confirmed: table layout and field names.** The labels come from the
in-game "Debug Edit" stats screen (`0x00471d33..0x00472100`: Name, Cost,
Armour, Weapon, Weapon 2, Speed `%1.1f` after `* 2^-16`, Turn Speed, Reload
Time, Health, Build Time). The descriptor setup (`0x00471380`) uses this
layout:

- Vehicles are 76-byte records at `0x004c0860 + id*76`: health `+0x02`,
  cost `+0x04`, build time `+0x08`, weapon `+0x0e`, weapon 2 `+0x10`,
  reload `+0x12`, armour `+0x14`, speed `+0x1c`, turn `+0x20`, side `+0x24`.
- Buildings are 516-byte records at `0x0043d820 + id*516` (id >= 1000):
  health `+0x00`, cost `+0x04`, build time `+0x08`, armour `+0x14`,
  weapon `+0x18`.

The vehicle type id indexes the 20-byte `vt_` name table at `0x004baa70`
(`0x004027d1..0x004027f7`, ids 0..43); `bt_` names start at `0x004ba688`.
Side 0/1 prints the `7L`/`CH` prefix from `0x004c4aa0`. Armour 0..4 is
Body/Light/Medium/Heavy/Structure (`0x004c4ab8`).

**Confirmed: weapon damage.** `0x00471280(weapon, armour)` maps `weapon - 2`
through byte table `0x00471354` to a damage class and returns
`damage[class*20 + armour] >> 16` from `0x004b5ee8`. Weapon names use
`0x00473a90`.

**Unknown.** The meanings of the speed (e.g. troop1 2.0), turn and reload
units are not known, nor is the simulation tic rate, so they cannot be
converted to engine units yet. Weapon range is not in these tables. Which
`vt_` id each BIM sprite belongs to has not been traced: the renderer
switches on type at `0x004266f6` (jump table `0x00426e8c`), but those
handlers were not followed. The seven placeholder actors in
`games/7legion/g_game.c` (Trooper, Slave, Spider Mech, Tank, Rock Mech,
Truck, Mobile Base) therefore keep their invented stats. Name similarity
(e.g. LTROOP/troop1 side 7L, CTROOP/troop2 side CH, MOBBASE/mobilebase)
is a lead, not proof.

Reproduce: `make 7legion-units` prints every vehicle, building and weapon
damage row as loaded, marking each record's source (`legion.exe` default or
`stuff.dat`).

## Info-table generation audit (2026-09-10)

**Confirmed from installed assets.** The current playable actor catalog has
seven entries, and all seven referenced BIM files exist under `data/7LEGION/GFX`:
Legionnaire, slave, spider mech, tank, rock mech, truck, and mobile base.
`tools/7legion_info_gen.c` now validates those assets and emits the Doom-style
`sprnames[]`, `states[]`, and `mobjinfo[]` tables. Reproduce with
`make test-info-gen`.

**Known limit.** The installed GFX directory contains many more unit-part and
effect BIM files. No open-source rules implementation or executable evidence
was found in this audit that maps those assets into a complete actor roster,
so the generator deliberately preserves the verified seven-actor vertical
slice rather than guessing additional `mobjinfo` records.

## Representation cleanup (2026-09-09)

**Confirmed by asset-loader comparison to `46f826a`.** The installed MAPT.000
and all 240 non-TILES BIM candidates produce identical results: 235 successful
sprite pixel/metadata fingerprints and five unchanged rejections. The default
screenshot is byte-identical. This task did not inspect legion.exe, so it makes
no new executable-address or retail-behavior claim. Reproduce with the commands
in [loader verification](LOADER_REFACTOR_VERIFICATION.md).

Representative `data/7LEGION/GFX/TROOP1W.BIM` SHA-256:
`65408d8835ce593c4443b4e0e38ec19e5c52fda7a9f475e807ceae29a3e270e4`.

The existing decoder reads a leading 32-bit offset table. Sparse frame headers
contain a 16-bit relative pixel-data offset and height; each row contains a
16-bit span count and `(x,length)` pairs. Those validated spans still determine
the shared canvas and final bounds. Missing spans remain transparent; every
palette value inside a span retains the supplied color, including index zero.
Trailing zero-height frames remain excluded, and the existing eight-facing
block interpretation remains unchanged. The full per-frame metadata table and
temporary RGBA atlas were allocation artifacts, not asset requirements.

The VCLZ ring decoder is unchanged; its output replaces the compressed file
buffer immediately. Expanded outputs under four bytes now reject before reading
the offset table. A focused fixture covers this malformed-input correction.

Unchanged rejected sprite candidates: `FOGGY.BIM`, `FOGWAR.BIM`, `FONT16.BIM`,
`INDFOG.BIM`, and `SNOWFOG.BIM`. Their format/usage remains **unknown in this
investigation**; no fallback, name exception, or guessed frame metadata was
added. Existing MAPT/MAPOVL/MAPL indexing and decoding formulas are preserved,
not newly verified against an executable by this comparison.

## Engine combat and production wiring (2026-09-12)

**Engine behavior, not a retail finding:** combat-capable generated actors now
enter `A_Look` and `A_Attack` through normal state ticking. Previously every
state had infinite duration and no action, preventing attacks entirely. The
one-tic dispatch states reuse existing C-authored range, damage and cooldown;
the current frame-zero presentation is unchanged. This does not establish
native attack animation lengths or timings. No executable or new asset format
was examined for this change.

The engine-added skirmish opponent receives the mission's starting cash, then
maintains bounded goals using its own living and queued actors and budget.
It no longer spends owner zero's resources. This opponent and the shared text
production sidebar are engine features, not reproductions of retail mission AI
or interface scripts. `env SDL_VIDEODRIVER=dummy make test-7legion` verifies
all attack-capable types acquire nearby targets, respect range and cooldown,
deal damage through `P_Ticker`, and maintain enemy production goals.
