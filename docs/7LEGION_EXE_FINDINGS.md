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

## Sprite direction mapping audit (2026-10-07)

**Confirmed: the current eight-block heuristic does not reproduce retail
sprite selection.** This is a comparison/investigation, not a runtime fix.
`sl_load_bim_sprite` interprets any usable frame count divisible by eight as
`8` directions and `count / 8` animation frames; everything else becomes
nondirectional. Native block `d` is installed in engine rotation `(8-d)%8`.
`render/r_draw.c::sprite_rotation_for_frame` starts north and proceeds
counterclockwise, so this assumes native blocks start north and proceed
clockwise. Neither the count heuristic nor that compass convention came from
native metadata. All current states select logical frame zero.

Temporary `OPEN_RTS_DEBUG_BIM` logging at the loader's definition construction
confirmed the following actual selections; it was removed after the audit.
The columns are native BIM lump indices for engine-facing N/E/S/W at logical
frame zero, not assertions about the direction depicted by those images.

| Asset | Usable lumps | Engine directions / stride | Selected N / E / S / W |
|---|---:|---|---|
| LTROOP.BIM | 344 | 8 / 43 | 0 / 86 / 172 / 258 |
| SLAVEN1.BIM | 232 | 8 / 29 | 0 / 58 / 116 / 174 |
| SPIDER.BIM | 96 | 8 / 12 | 0 / 24 / 48 / 72 |
| TANKBASE.BIM | 33 | 1 / 33 | 0 / 0 / 0 / 0 |
| ROCKMECH.BIM | 64 | 8 / 8 | 0 / 16 / 32 / 48 |
| TRUCK.BIM | 32 | 8 / 4 | 0 / 8 / 16 / 24 |
| MOBBASE.BIM | 32 | 8 / 4 | 0 / 8 / 16 / 24 |

### Retail instruction evidence

Executable fingerprint is the `a312f7b5...6f6c154` SHA-256 recorded above.
PE linker version is 4.20, entry point RVA `0x9e370`, timestamp
1997-09-01 01:56:04 UTC. A Microsoft Visual C++ 4.2 toolchain is **inferred**
from the linker metadata, not proved. Inspected draw routines use stack
arguments, EBP frames and caller stack cleanup. `r2`/r2ghidra were unavailable
in this environment; LLVM `objdump` supplied full discovery disassembly and
bounded instruction windows. No decompiler signatures were assumed.

Below, `a` is the integer part of the native angle field (raw value `>>16`),
not an engine BAM angle; `p` is the object's animation field at
`0x680466 + object_index*0x6d0`. Formulas describe the inspected branches,
not complete animation dispatch or timing.

- **LTROOP, confirmed:** `0x427b30` reads the LTROOP handle `0x6dd2d4`.
  At `0x427ca2..0x427ccc`, the `p == -1` branch selects
  `312 + 2*((((a+2)>>2)+8)&15) + (byte[0x680480+object_offset]&1)`.
  The `p == 99` branch at `0x427d20..0x427d43` uses the same 16 directions
  without the final parity bit. The ordinary branch at
  `0x427d67..0x427d85` uses `12*((((a+2)>>2)+6)&15) + p`.
  Thus 344/8 = 43 is not a native animation stride, and one uniform mapping
  cannot cover even this file's different animation ranges.
- **SLAVEN1, confirmed branch/asset pairing:** the routine at `0x428c70`
  chooses handle `0x6683bc` by default and `0x70db1c` when the side lookup
  equals 1. Loads `0x412f5e..0x412f84` link the first to
  `GFX\\slaven2.bim` (string `0x4b7210`); loads
  `0x412faf..0x412fd0` link the second to `GFX\\slaven1.bim`
  (string `0x4b71f0`). For SLAVEN1, `p == -1` and `p == 99` select
  `176 + (((a>>2)+7)&15)` at `0x428e1c..0x428e2b` and
  `0x428e70..0x428e7f`; ordinary animation uses
  `12*((((a+4)>>3)+3)&7) + p` at `0x428cee..0x428d04` and
  `0x428ed2..0x428edf`. The SLAVEN2 counterpart uses
  `200 + (((a>>1)+12)&31)` for the first two branches and
  `72 + 16*((((a+4)>>3)+3)&7) + p` for ordinary animation.
  The later death branch has further direction/range arithmetic; its complete
  state semantics remain **unknown**. The existing engine's generic Slave
  actor identity is still not established by this branch comparison.
- **SPIDER, confirmed:** `0x4298b5..0x4298cb` computes
  `d = ((((a+4)>>3)+3)&7)`; `0x42990b..0x429916` selects `12*d+p`.
  Handle `0x6ccad4` reaches the draw call at `0x429964..0x42996c`.
  Eight blocks of twelve agree with the file's 96 lumps, but our loader omits
  the native angle transform. Matching counts alone does not verify facing.
- **TRUCK, confirmed:** `0x426fe2..0x42700e` selects
  `((raw_angle>>17)+16-(type==1 ? 8 : 4))&31`.
  The `type==1` branch passes that value directly as the frame of Truck
  handle `0x6dd2e4` at `0x42709d..0x4270b2`. It has 32 directional images,
  not eight blocks of four temporal frames. **Correction to the earlier
  vehicle-render table:** sharing routine `0x426f30` does not prove that
  both carrier and truck use Truck artwork. The other branch uses a different
  handle (`0x70d824`), whose asset was not resolved here.
- **MOBBASE, confirmed:** `0x4281cd..0x4281f4` selects
  `((raw_angle>>17)+8)&31` and passes it directly to handle `0x804308`
  at `0x428217..0x428234` in the side-zero branch. Again this is 32
  directions, not eight animation blocks.
- **ROCKMECH, confirmed multipart use:** `0x429b32..0x429b3d` computes
  `((a>>1)+11)&31`; `0x429c04..0x429c0e` adds 32 when the signed object
  word at `0x67fea8+object_offset` exceeds 30. The result reaches the
  Rockmech handle `0x6e5c20` at `0x429c26..0x429c3e`.
  The load at `0x412433..0x412454` uses string `0x4b7450`,
  `GFX\\rockmech.bim`. This is a 32-direction part with two banks, not
  eight directions with eight temporal frames. A separate body uses
  `20*((((body_angle+4)>>3)+3)&7)+p` and handle `0x6dd2e0`
  at `0x429b96..0x429bfc`; its load at `0x412340..0x412366` names
  `GFX\\mech1leg.bim` (string `0x4b7488`). The precise gameplay meaning
  of the bank-switch word and the current engine actor's native identity
  remain **unknown**; do not guess a damage threshold meaning.
- **TANKBASE, confirmed engine failure / unknown retail use:** all angles
  select frame zero because 33 is not divisible by eight. No executable
  link to this asset was established; the previously documented placeholder
  warning remains. Do not silently discard its extra image or declare it a
  verified 32-direction native tank without tracing the actual tank asset.

**Unknown:** the complete conversion from native angle fields to our BAM
compass convention, animation-state semantics/timing, and all faction/actor
branches. The `0x4b9e48`/`0x4b9e68` eight-entry movement component tables
were inspected, but were not linked through to the render-angle fields in
this audit; they are not sufficient to establish that conversion.

**Implementation consequence:** replace the file-length heuristic with
verified per-sequence native frame definitions, then map native angles to
engine rotations at load time. Keep decoded pixels and native lump numbers.
Do not apply a global quarter-turn offset: it would leave incorrect strides,
missing direction resolutions, mixed sequences and multipart actors broken.
The earlier representation-cleanup assertion only proved preservation of old
loader behavior; it did not validate that behavior against retail.

### Reproduction and source limits

External source URLs and version provenance are in `REFERENCES.md`,
“7th Legion sprite direction audit”. The Quick converter corroborates the
leading offset table and scanline spans, but has no direction/animation
metadata; its sparse-frame first-word-as-width interpretation is not authority
for the engine's validated relative pixel-data offset. The iiEveOfPeace
repository is a disproven lead: its description mentions 7th Legion but its
actual source reads Relic SGA/Chunky data and its example opens Dawn of War.
No usable compass mapping was found online in this search.

Reproduce retail formulas, for example:

```sh
objdump -d --x86-asm-syntax=intel --start-address=0x427b30 --stop-address=0x427e30 data/7LEGION/legion.exe
objdump -d --x86-asm-syntax=intel --start-address=0x426f30 --stop-address=0x427198 data/7LEGION/legion.exe
objdump -d --x86-asm-syntax=intel --start-address=0x428140 --stop-address=0x4282a0 data/7LEGION/legion.exe
objdump -d --x86-asm-syntax=intel --start-address=0x428c70 --stop-address=0x428f70 data/7LEGION/legion.exe
objdump -d --x86-asm-syntax=intel --start-address=0x429870 --stop-address=0x429c50 data/7LEGION/legion.exe
env SDL_VIDEODRIVER=dummy make test-loader-7legion
```

Loader fixture verification passed after removal of temporary diagnostics.
All seven catalog files decoded successfully. No runtime mapping was changed.

Asset SHA-256 fingerprints:

| BIM | SHA-256 |
|---|---|
| LTROOP | `a0e15061af26974f76beb0421114fc73868d49692d27d2c8e34e2bac3651a55d` |
| SLAVEN1 | `dc2354c06b951181d691578a77da86056b99b45d38ea158d80baa30defc1d720` |
| SPIDER | `8a799af1d64e95c15382d77ac88872b3f4b1d149523b0ab6d99f7a8e2754483e` |
| TANKBASE | `6708aab0fdef84c7b1a8a00d4964f96f62b89f1b7b53852f310831c1990ed413` |
| ROCKMECH | `42121cbda29b70848d6178f7bd80a600ec1e4cdcfb0849b36848939763028514` |
| TRUCK | `279196008d54f32c0db86102686b4bacda0b9eecab12fd66833aa0fabe342848` |
| MOBBASE | `69f68e2c65b92d467e5196502ff5ef36bd12023b790b79fa569282eab3dd39e9` |
