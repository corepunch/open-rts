# DKREIGN.EXE transporter animation and docking

Documented September 30, 2026; transport implementation verified September 26.
Executable fingerprint: [DR_DISASSEMBLY.md](DR_DISASSEMBLY.md#executable-fingerprint).
This is the Dark Reign counterpart to the focused DC dropship animation report.
Full historical evidence and corrected hypotheses remain in
[DR_EXE_FINDINGS.md](DR_EXE_FINDINGS.md#retail-transporter-docking-correction-2026-09-26).

## Native type data

**Confirmed from retail definitions and parser instructions.** FG ground/hover
transporters are native 13/14; Imperium equivalents are 1006/1007.
Both factions use `ucfrgst0.spr` for ground and `uchfrst0.spr` for hover.
Hover does not imply Fly.

| Unit-type field | Meaning | Native value |
|---|---|---|
| `+0x500 + resource*12` | Capacity | Water 750; Taelon 50 |
| `+0x504 + resource*12` | Load batch | Water 270; Taelon 10 |
| `+0x508 + resource*12` | Unload batch | Water 270; Taelon 25 |
| `+0x608` / `+0x60c` | Load facing / animation section | 135 degrees / section 1 |
| `+0x610` / `+0x614` | Unload facing / animation section | 135 degrees / section 1 |

The source/destination records are authored by `SetBuildingSrcAndDst`;
`0x0044661d` parses them. `0x00446745` stores resource transport records.
`0x0046b580` checks unit/building compatibility and contains its diagnostic
at `0x0046b5c2`. Current destination compatibility is water to either faction's
Launch Pad, Taelon to either faction's Power Generator, subject to the shared
simulation's living/allied receiver selection.

## Bay lookup and coordinate representation

**Confirmed:** the building parser compares `SetBay` at `0x004a04e5` and
stores both arguments at type `+0x22c` in `0x004a056c..0x004a059c`:

```text
bits 0..3: first argument & 15
bits 4..7: second argument & 15
```

The route consumer sign-extends the four-bit coordinates. `0x0049bbb0` combines
them with building origin `+0x70/+0x74`, creates waypoints through `0x004754d0`
and starts order 6 through `0x004b7b10`. Order dispatcher `0x004baac0` uses
`0x00422550` to resolve a building at the exact bay cell, then invokes
`0x0049c010` on arrival. Near a building edge is not enough.

| Building | Authored bay | Engine M01F destination |
|---|---|---|
| Launch Pad `fglp` / `implp` | `(3,2)` | Water `(7.5,55.5)` at the starting FG pad |
| Power Generator `fgpp` / `imppp` | `(1,3)` | Taelon `(6.5,42.5)` at the starting FG generator |
| Water/Taelon extractors `impww` / `impmn` | `(1,1)` | Center of each extractor's authored bay |

Native placement uses 24 pixels/cell at `0x00448860` and `0x00445180`;
building-region bounds at `0x004968d0`/`0x00496a10` begin at `cell*24-12`.
The engine represents building anchors and unit cell centers differently, so
its destination is `building anchor + cell_center(SetBay)`. The half-cell is
a representation conversion, not a visual compensation.

## Collision footprint

**Confirmed:** `OVLEFF.TXT` supplies dimensions and row-major effect/altitude
pairs. `0x0042ca50` parses them; `0x00481090` owns effect definitions.
Effect -1 preserves terrain, effect 2 is walkable and effect 3 is solid.
The parser encodes -1 as `0x1f`. `SetBuildingImages` resolves through
`0x0049eda0` and sprite lookup `0x00481120`.

```text
Launch Pad nclnc1l0.spr, 5x4   Generator ncpow1l0.spr, 4x5
-1 -1 -1 -1 -1                -1 -1 -1 -1
-1  2  3  3  2                -1  2  2 -1
 3  3  3  2  2                 2  3  3  3
 2  3  3  2 -1                 2  2  3  2
                               2  2  2  2
```

Both extractor masks are 3x3, entirely effect 2. The engine consumes the native
footprints rather than blocking guessed rectangles. Altitude entries are
parsed, but their full height behavior is outside this collision implementation.

**Disproven:** a solid 4x3 pad or 3x4 generator footprint. Those old rectangles
blocked the bay and forced a fallback destination even after reading `SetBay`.

## Facing and section timing

**Confirmed from `0x0049c010`:** compare the part facing `+0x80 >> 16`, turn
through `0x004a8480`, start section 1 with multiplier `0x10000` and one-shot
mode 2 through `0x004a8920`, then wait for `0x004a8aa0` before transferring.
The native heading uses `atan2(-dy,dx)`, east-zero and counterclockwise.
135 degrees maps to engine `ANG90+ANG45`, disk rotation slot 6.

Degree conversion at `0x004467e8..0x00446807` uses float `0x47360b61`
at `0x005901f4`, approximately `2^24/360`. `SetRotationRate(10)` is converted
at `0x00447ab4`, stored at part-type `+0x54` at `0x00447ac4`, and consumed
by shorter-path/clamped turning in `0x004a8e00`. The engine uses the truncated
24-bit step `floor(2^24*10/360) << 8` in its 32-bit angle representation.
This finding does not certify all other unit turning rates.

| Sprite | Travel section 0 | Transfer section 1 | Rotations / hotspots |
|---|---|---|---|
| `ucfrgst0.spr` | Frames 0..2, rate 65536 | Frames 3..17, rate 19660 | 16 / 6 |
| `uchfrst0.spr` | Frame 0, rate 65536 | Frames 1..15, rate 19660 | 16 / 6 |

`0x004a8920` multiplies shifted fixed operands; `0x004a96f0` advances the
cursor and completes at `(last_frame+1)<<16`:

```text
step = (65536 >> 8) * (19660 >> 8) = 19456
elapsed(n) = ceil(n * 65536 / 19456)
duration(n) = elapsed(n+1) - elapsed(n)
duration of 15 frames = 51 simulation tics
```

The generated frame durations are `4,3,4,3,3,4,3,3,4,3,4,3,3,4,3`.
Retail game speed is configurable 1..60 through `0x004048b0`, global
`0x006ccac0`. Therefore 51 tics is not a universal retail wall-clock duration.

## Transfer lifecycle

The verified engine sequence is:

```text
Travel to source bay -> face 135 degrees -> play section 1 once
  -> load one bounded batch -> repeat until full
Travel to compatible destination bay -> face 135 degrees
  -> play section 1 once -> unload one bounded batch
  -> repeat until empty -> return to source
```

Water loads/unloads as 270,270,210 for a full 750-unit cargo. Taelon loads as
five 10-unit batches and unloads as two 25-unit batches. Transfer is limited by
capacity and stock; credits must not arrive before animation completion.

**Confirmed engine ownership:** ordinary allocated mobjs and shared thinker
updates own movement, cargo and the destination reference. The active level's
`dr_mission_t` stores native bays. `DR_HarvestDropoffMatches` supplies resource
compatibility and the whole destination point. Generated state chains terminate
at standing; another pass starts only if another transfer batch is needed.
Destroyed, incompatible or blocked receivers cannot receive cargo. New orders
and stop/resume retain cargo; thinker removal clears borrowed references.

**Engine behavior, not verified retail avoidance:** an arriving transporter
yields to one leaving a shared bay using an unoccupied neighboring cell.
Docking actors are protected from ordinary separation. This prevents stalled
opposing traffic; it does not port retail local arbitration.

## Superseded rules and remaining unknowns

- Capacity 100, continuously looping transfer animation and six-cell delivery
  proximity were incorrect transporter rules.
- OpenDR docking offsets establish a separate reference implementation, not
  retail bay coordinates; none are substituted for `SetBay`.
- Complete extractor regeneration/stock, per-building receiving storage and
  launch/economy timing remain unported. Current delivered batches enter the
  existing resource balance directly.
- Passenger transports, Phase Runner and Hostage Taker are distinct capability
  families; this resource-cargo lifecycle does not establish their behavior.

## Verification and reproduction

`test_harvest_build` drives all four types through two full water and two full
Taelon trips each. It checks exact bays, facing, all 15 frames, 51-tic passes,
batch sizes, conservation, return trips, footprint masks, destroyed/replaced/
blocked destinations, shared-bay traffic and stop/resume.

```sh
rg -n 'SetBay|SetBuildingImages' data/REIGN/dark/deftxt/BUILD.TXT
rg -n 'SetTransport|SetRotationRate' data/REIGN/dark/deftxt/UNITS.TXT
r2 -q -e bin.cache=true -c 'af @ 0x49bbb0' \
  -c 'pdf @ 0x49bbb0' -c q data/REIGN/dkreign.exe
r2 -q -e bin.cache=true -c 'af @ 0x49c010' \
  -c 'pdf @ 0x49c010' -c q data/REIGN/dkreign.exe
r2 -q -e bin.cache=true -c 'af @ 0x4a96f0' \
  -c 'pdf @ 0x4a96f0' -c q data/REIGN/dkreign.exe
make build/bin/tests/dark-reign/test_harvest_build
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-reign/test_harvest_build
```
