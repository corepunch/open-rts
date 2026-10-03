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

**Unknown.** Speed (e.g. `troop1` 2.0), turn and reload still need conversion
to engine units; the retail simulation tic cadence has not been established.
Weapon range is not in these tables and its target-selection path remains
untraced. The current runtime actor stats therefore remain placeholders until
those values are derived.

## Vehicle render routines (2026-10-03)

**Confirmed: vehicle rows contain per-type render routines at `+0x38`.** For
vehicle `id`, the pointer is at `0x004c0860 + id*76 + 0x38`. The routine
column from `make 7legion-units` reproduces those pointers. Following each
routine to the BIM handle read, and the handle back to its hard-coded asset
load, confirms these mappings:

| Native ID | `vt_` name | Routine | BIM asset | Evidence |
|---:|---|---:|---|---|
| 0, 1 | `carrier`, `truck` | `0x00426f30` | `GFX\\truck.bim` | shared routine reads handle `0x006dd2e4`; loaded at `0x0041130a` |
| 14 | `troop1` | `0x00427e30` -> `0x00427b30` | `GFX\\ltroop.bim` | wrapper calls `0x00427b30`, which reads `0x006dd2d4`; loaded at `0x00412e29` |
| 37 | `spmech` | `0x00429870` | `GFX\\spider.bim` | routine reads handle `0x006ccad4`; loaded at `0x0041295d` |
| 42 | `mobilebase` | `0x00428140` | `GFX\\mobbase.bim` | routine reads handle `0x00804308`; loaded at `0x00412acc` |

ID 14's routine is a wrapper, so follow its call before assigning the sprite.
IDs 0 and 1 share a routine that reads the Truck BIM handle. IDs 38 and 43
share routine `0x00429980`, which conditionally reads the Rockmech handle;
ID 19's routine `0x00428c70` conditionally reads the Slaven1 handle. These
conditional reads do not by themselves prove the base sprite for those IDs.
ID 19 is named `dino` in the native table, not `slave`.

**Unresolved placeholder:** no `TANKBASE.BIM` path occurs in the executable's
string table. The executable does load faction tank assets such as
`GFX\\mttank.bim` (handle `0x00804330`), `GFX\\attank.bim` (handle
`0x0077b4d0`) and `GFX\\ntank.bim` (handle `0x0083a3f8`), but their vehicle
IDs have not been linked here. Do not treat the current `Tank`/`TANKBASE.BIM`
entry as a confirmed native actor.

Reproduce routine addresses with `make 7legion-units`; trace the file loads and
read sites with `r2 -q -e bin.cache=true -A -c 'axt @ <handle>' -c q
data/7LEGION/legion.exe` and disassemble the listed routines.

## Vehicle movement units and timer (2026-10-03)

**Confirmed: the vehicle `speed` field is a 16.16 positional movement
magnitude, not cells per second.** Movement routine `0x0043c0c0` reads
`0x004c087c + type*4` at `0x0043c1cc`. It adds the per-object adjustment
`mobj[+0x67febe] << 13` at `0x0043c1bd..0x0043c1d3`, multiplies the result by
the fixed-point direction components in `0x004b9e48` (for example
`0x0043c5d5..0x0043c5f6`), then adds the resulting planar delta to the object's
16.16 position (`0x0043c6b0..0x0043c6b8`). With a zero per-object adjustment,
the table speed is therefore the positional distance per movement update.
The separate movement-update cadence, and the role/range of the per-object
adjustment, remain unknown; a cells-per-second conversion is not yet
justified.

**Disproven: the 20/33 ms multimedia timer intervals are not evidence of the
simulation tic rate.** The timer setup calls `timeSetEvent` with either 20 or
33 ms and callback `0x00450be0`; that callback is only `ret 0x14`. The game
update loop at `0x00416fe0` consumes a game-speed value from `0x004afbe0` and
decrements counters, but the timer callback does not drive that loop. Continue
tracing the caller cadence of movement `0x0043c0c0` before converting native
speed to the engine's per-second units.

## Mission starting-unit counts (2026-10-03)

**Confirmed: `PVStart` is parsed as one decimal count per character.** In
`fcn.00403350`, the retail executable calls
`GetPrivateProfileStringA("PVStart", ...)` at `0x00403da3..0x00403db4`, then
walks the returned string by character index. At `0x00403df6..0x00403e00` it
reads the indexed byte and subtracts ASCII `'0'`; the loop at
`0x00403e27..0x00403eb9` repeats the per-slot creation path that calls
`0x00424c20`. The same key is read again at `0x00403fd9` for the other mission
side/path.

Mission 1's 43-character `PVStart` is
`0000000000000040000000000000000000000000001`: character 14 contains 4 and
character 42 contains 1. The counts at those positions are confirmed, but a
character position is not always the `vt_` id. Before calling `0x00424c20`,
the parser indexes the static dword map at `0x004adb38` (`0x00403e6f`); the
map's values are `0,1,2,3,3,5,6,7,9,9,9,11,12,15,15,15,16,...,42`.
Consequently character 14 maps to native type 15 in this branch, whereas the
other branch (`0x00403e78`) passes raw loop index 14. Character 42 maps to 42
in either branch. The branch depends on global word `0x005474b4` being 1; its
mission-side value and the meaning of the map aliases are not yet established.
The spawn helper receives the selected value, but proof from this mission's
side/mode to the corresponding rendered actor remains pending.

The current loader's assumption that character 14 is always native `troop1`
(id 14) is therefore unverified and may be wrong; its player Slave and
synthetic enemy force are not sourced from the confirmed mission counts. Do
not replace these with native spawns until the mission-side branch is traced.

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
