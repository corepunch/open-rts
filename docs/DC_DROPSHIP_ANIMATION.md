> Runtime correction (2026-09-09): the historical custom movement routines below
> have been superseded by ordinary flying `P_MoveUnitTo` orders and per-tic shared
> movement. `A_DC_Fly` / `P_MoveMobjToward` are removed. See the dated engine movement
> correction in `DC_EXE_FINDINGS.md`; this is requested engine behavior, not a new
> claim about retail gameplay.

# DC.EXE Dropship Animation System

> Retail unload timing correction (2026-09-10): DC.EXE's FIN loader replaces a
> zero `DROP.FIN` frame delay with 15, then computes `((15 + 3) * 15) / 100 = 2`
> runtime animation ticks. The ten-frame `DROPTWO` unload therefore takes 20
> ticks, about 0.67 seconds at the open-rts 30 Hz simulation rate. The old
> 47-tic unload policy was too slow and is superseded; this correction is based
> on the instruction sequence at `0x00423544`–`0x0042358f` in the fingerprinted
> retail executable.

This document records the Dark Colony dropship animation system as traced through
DC.EXE disassembly and native asset analysis. Addresses refer to the executable
fingerprint below.

## Executable fingerprint

| Property | Value |
| --- | --- |
| File | `data/DCOLONY/DC.EXE` |
| SHA-256 | `008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a` |
| Size | 566,272 bytes |
| Format | PE32, little-endian, i386, Windows GUI |
| Image base | `0x00400000` |
| Compile timestamp | August 11, 1997 20:53:20 |

## DROP.FIN binary structure

**File:** `data/DCOLONY/ANIMATE/DROP.FIN` — 51,370 bytes.

### Header (8 bytes, little-endian)

| Offset | Size | Field | Value |
|--------|------|-------|-------|
| 0 | u16 | `frame_count` | 29 (total animation frames) |
| 2 | u16 | `aux_count` | 136 (per-frame metadata records) |
| 4 | u16 | `label_count` | 10 (named animation sequences) |
| 6 | u16 | `dependency_count` | 13 (referenced SPR file stems) |

### Dependencies (8 bytes each, 13 entries, offset 8)

Each is an 8-byte ASCII name, NUL-padded. These are the SPR file stems the
animation references:

`drop`, `duts`, `clod`, `glit`, `srch`, `spot`, `hubu`, `shortcit`, `hite`,
`hitd`, `hith`, `hitt`, `glat`

### Labels (20 bytes each, 10 entries, offset 112)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 16 bytes | Name (ASCII, NUL-padded) |
| 16 | u16 | `start` — first aux record index |
| 18 | u16 | `end` — last aux record index (inclusive) |

| Label | Start | End | Frames | Purpose |
|-------|-------|-----|--------|---------|
| `DROPTWO` | 0 | 9 | 10 | Unload animation |
| `SCNCPOOPBUILD` | 10 | 51 | 42 | Scenario build overlay |
| `SCNCBUILD` | 52 | 57 | 6 | Scenario build overlay |
| `NOTDROP` | 58 | 65 | 8 | Non-drop effect |
| `GLINTER` | 66 | 71 | 6 | Glint/sparkle effect |
| `DROPSTAND0` | 72 | 81 | 10 | Idle hover (loaded, unused) |
| `RIGHTSPOTDROP` | 82 | 82 | 1 | Single-frame spot |
| `START` | 83 | 83 | 1 | Single-frame start |
| `DROPMOVE0` | 84 | 93 | 10 | Flying approach/departure |
| `SCNCPODBUILD0` | 94 | 135 | 42 | Scenario build overlay |

### Aux records (164 bytes each, 136 entries, offset 312)

Only the first 4 bytes are meaningful:

| Offset | Size | Field |
|--------|------|-------|
| 0 | u16 | `part_count` — number of sprite commands in this frame |
| 2 | u16 | `raw_ticks` — frame duration encoding (0 = default 15) |
| 4–163 | 160 bytes | Padding (NUL-filled "NONAME" strings) |

Duration conversion (`w_spr.c:1043–1045`):
```
if (raw_ticks == 0) raw_ticks = 15;
runtime_tics = ((raw_ticks + 3) * 15) / 100;
duration_ms = (runtime_tics * 1000 + 15) / 30;
```
When `raw_ticks=0`: `runtime_tics = (18*15)/100 = 2`, `duration_ms ≈ 67ms`.

### Commands (22 bytes each, 1307 total, offset 22616)

Stored contiguously. Frame N has `aux[N].part_count` commands. Walk sequentially:

| Offset | Size | Field |
|--------|------|-------|
| 0 | 8 bytes | `sprite` — ASCII name of SPR file (e.g. `"drop"`, `"glit"`) |
| 8 | i16 | `frame` — sprite frame index within the SPR file |
| 10 | i16 | `x` — horizontal offset from ship center (negative = left) |
| 12 | i16 | `y` — vertical offset (positive = below center) |
| 14 | i16 | `remap` — palette remap group (0–7, team colors) |
| 16 | i16 | `intensity` — render intensity (always 16) |
| 18 | i16 | `layer` — render layer / render_selector |
| 20 | i16 | `flags` — bit 0 = flip X |

Assertion at `0x0046fc10`: `frame_parts[i].sprite < frame_parts[i].juice_file->cells.number`
confirms each frame part references a sprite index into a loaded SPR file
(called "juice file" in native code).

## What each animation label contains

### `DROPMOVE0` (frames 84–93, 10 frames) — Flying Approach / Departure

Used for: `S_DROPSHIP_APPROACH` and `S_DROPSHIP_DEPART`.

Each frame has 5–11 parts. Every frame always has exactly 2 `drop` commands
(the ship body):
- `drop` frame=0 (upper fuselage/hull)
- `drop` frame=1 (lower hull/underside)

Additional parts per frame:
- `clod` (1 entry) — single debris/rock particle cycling frames 0–9.
- `duts` (1 entry) — dust trail cycling 0→2→4→...→16→17.
- `glit` (3–5 entries) — light glint/sparkle effects cycling frames
  5,6,7,8,10,12,14.

`remap` values: `drop` uses remap=2 (upper) and remap=4 (lower) — team-colored.
`clod`/`duts` use remap=0. `glit` uses remap=4 (team-colored) or remap=0.

### `DROPTWO` (frames 0–9, 10 frames) — Unload Animation

Used for: `S_DROPSHIP_UNLOAD`.

Each frame has 10–14 parts. Every frame has exactly 2 `drop` commands (body)
at `layer=1` (upper) and `layer=0` (lower). This is the most complex animation:
- `duts` (1 entry) — dust trail, cycles 0→2→4→...→16→17.
- `clod` (2–4 entries) — debris/rocks, cycling frames 0–9 with animated
  positions. Multiple debris chunks scatter outward.
- `glit` (4–6 entries) — sparkles/lights, cycling frames 5,6,7,8,10,12,14.
- `drop` (2 entries) — ship body.

### `DROPSTAND0` (frames 72–81, 10 frames) — Standing / Hovering

**Loaded but never used** by the current state machine. Each frame has 5–12
parts. Every frame has exactly 2 `drop` commands at `layer=0`. No `clod`
entries (no debris). Includes `glit` frames 9,10,11 not present in MOVE,
suggesting a different glow pattern.

## State machine

Defined in `info.c:324–327`:

| State | Action | Next State |
|-------|--------|------------|
| `S_DROPSHIP_APPROACH` | `A_DC_DropshipApproach` | `S_DROPSHIP_UNLOAD` |
| `S_DROPSHIP_UNLOAD` | `A_DC_DropshipUnload` | `S_DROPSHIP_REPOSITION` |
| `S_DROPSHIP_REPOSITION` | `A_DC_DropshipReposition` | `S_DROPSHIP_UNLOAD` or `S_DROPSHIP_DEPART` |
| `S_DROPSHIP_DEPART` | `A_DC_DropshipDepart` | `S_NULL` (removed) |

All states have `tics=1` and sprite=`SPR_DROP` (placeholder — real rendering
is via effect slots).

## Script command parsing

**Confirmed from disassembly:** The reinforce command is parsed in `fcn.0043ae2c`.

- `"reinforce"` string at `0x00471b14`, referenced at `0x0043b2c8`
- `"reinforce2"` string at `0x00471b20`, referenced at `0x0043b3df`

The handler stores dropship data in global slots at `0x4f7448`–`0x4f7464`:
- `0x4f7448` — payload count / index (dword, read+written extensively)
- `0x4f744c` — team/side byte
- `0x4f7452` — word field (origin cell or offset)
- `0x4f7464` — dword field (destination or timer)

String comparison helper `fcn.00467480` (119 cross-references) is called
to match the command keyword.

## Rendering: multi-part compositing

The dropship actor itself (`MT_DROP_LINK`) has an empty `mobjinfo` entry.
The state table sprite is just a placeholder. All visual rendering comes from
**effect slots** managed by `sync_dropship_parts`.

### `sync_dropship_parts` (p_spec.c:434)

1. Calls `dropship_frame_at()` to get current frame index from animation.
2. Gets the `DropshipFrame` struct (part_count + array of `DropshipPart`).
3. For each `DropshipPart`:
   - **Skips** `duts` and `clod` sprites when animation is NOT `unload`
     (debris/dust render only during UNLOAD).
   - Allocates/updates an `effect_t` in a slot (`DROPSHIP_MAX_PARTS=24`).
   - Sets: position (ship actor), frame index, x/y offset, team color,
     intensity, sprite name, render_selector, flip flag.
4. Clears excess slots when parts disappear between frames.

### `dropship_frame_at` (p_spec.c:424)

```c
if (animation->duration_ms > 0) elapsed_ms %= animation->duration_ms;
for (int i = 0; i < animation->frame_count; ++i) {
    frame_end_ms += animation->frames[i].duration_ms;
    if (elapsed_ms < frame_end_ms) return i;
}
return animation->frame_count - 1;
```

Animation loops via modulo. Since all `raw_ticks` are 0, every frame is ~67ms,
so the ten-frame `DROPTWO` unload runs for ~670ms.

## Complete lifecycle

### 1. Trigger

A `reinforce` command in the `.TRO` script calls the reinforce parser at
`fcn.0043ae2c`. On first call for a given delivery, a free `Dropship` slot
is allocated (max 8 concurrent), the flight vector is set from off-screen
start to origin cell, and state is set to `S_DROPSHIP_APPROACH`.

### 2. Approach

`A_DC_DropshipApproach` moves the ship actor toward `movement.goal` via
`P_MoveMobjToward`. The animation uses `DROPMOVE0` (fly-in with dust trail
and sparkles). State auto-transitions to `S_DROPSHIP_UNLOAD`.

### 3. Unload

The ten-frame `DROPTWO` sequence runs for 20 runtime ticks (~670ms). The
animation switches to `DROPTWO` — the complex multi-part
composition with scattered debris, dust clouds, and sparkles. This is when
the ship visually "deposits" units.

### 4. Reposition

`A_DC_DropshipReposition` spawns the actual unit at the ship's current cell.
If more payload remains, it moves to the next drop position (using
`drop_formation` offsets) and loops back to UNLOAD. If all payload is
delivered, it transitions to DEPART.

### 5. Depart

`A_DC_DropshipDepart` sets the goal to the continuation of the original
approach vector and uses `DROPMOVE0` again (fly-out). State transitions
to `S_NULL`.

### 6. Cleanup

When state is `S_NULL`, `P_TickMobjState` sets `actor.remove = true`.
`clear_dropship_parts` destroys all effect slots. `ship->active = false`.

## Key addresses summary

| Component | Address / Location |
|-----------|--------------------|
| FIN loader | `fcn.004230ac` |
| `"animate/%s"` format string | `0x0046fad4` |
| `"rb"` mode string | `0x0046fae0` |
| `"Animation Headers"` | `0x0046fae4` |
| `"Animation Frames"` | `0x0046faf8` |
| `"juicel.c"` source hint | `0x00472954` |
| frame_parts bounds assertion | `0x0046fc10` |
| frame_parts NULL assertion | `0x0046fc50` |
| `"reinforce"` string | `0x00471b14` |
| `"reinforce2"` string | `0x00471b20` |
| Reinforce parser | `fcn.0043ae2c` |
| String compare helper | `fcn.00467480` (119 xrefs) |
| Dropship global slots | `0x4f7448`–`0x4f7464` |
| `"Whoa batman, sprite %d..."` | `0x0046e684` |
| `"Whoa Batman, I don't have..."` | `0x0046fa8c` |

## Evidence labels

- **Confirmed** — exact instructions and native data agree, or a focused test
  reproduces the result.
- **Inferred** — multiple observations support a conclusion but one native
  boundary remains untraced.
- **Disproven** — an attractive hypothesis contradicted by executable or asset
  evidence.
- **Unknown** — behavior that still requires a controlling caller, consumer,
  or runtime trace.

| Finding | Label |
|---------|-------|
| DROP.FIN header/label/command format | **Confirmed** from binary parsing and `w_spr.c` loader |
| `DROPMOVE0`, `DROPTWO` part composition | **Confirmed** from FIN binary data |
| `DROPSTAND0` is loaded but unused | **Confirmed** — `dropship_animation()` maps non-UNLOAD states to `move` |
| Reinforce string at `0x471b14` | **Confirmed** from `rabin2 -zz` and `axt` |
| frame_parts assertion at `0x46fc10` | **Confirmed** from disassembly of `fcn.004230ac` |
| Global dropship slots at `0x4f7448–0x4f7464` | **Inferred** from multiple read/write patterns in reinforce parser |
| Exact dropship altitude in retail | **Unknown** — not established from disassembly |
| `DROPSTAND0` intended use case | **Unknown** — loaded but never selected by state machine |

## Reproducing the analysis

```sh
# Re-verify FIN structure
python3 -c "
import struct
data = open('data/DCOLONY/ANIMATE/DROP.FIN', 'rb').read()
hdr = struct.unpack_from('<4H', data, 0)
print(f'frame_count={hdr[0]} aux_count={hdr[1]} label_count={hdr[2]} dep_count={hdr[3]}')
"

# Re-verify reinforce string references
r2 -q -e bin.cache=true -A -c "axt @ 0x00471b14" -c q data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -A -c "axt @ 0x00471b20" -c q data/DCOLONY/DC.EXE

# Re-verify frame_parts assertion
r2 -q -e bin.cache=true -A -c "pdf @ fcn.004230ac" -c q data/DCOLONY/DC.EXE | grep "0x46fc10"

# Re-verify FIN loader string references
r2 -q -e bin.cache=true -A -c "axt @ 0x0046fae4" -c q data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -A -c "axt @ 0x0046faf8" -c q data/DCOLONY/DC.EXE

# Full reinforce parser disassembly
r2 -q -e bin.cache=true -A -c "pdf @ fcn.0043ae2c" -c q data/DCOLONY/DC.EXE | wc -l
```




---------

Objective
- Replicate Dark Colony's dropship animation and positioning behavior in open-rts by reverse-engineering DC.EXE and native assets, specifically verifying the Z (height/altitude) coordinate system used by the retail game.
Important Details
- Retail executable: data/DCOLONY/DC.EXE, SHA-256 008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a, PE32 i386, image base 0x00400000, 566,272 bytes
- open-rts uses DROPSHIP_ALTITUDE = FIXED_ONE (1.0 grid cell, 16.16 fixed = 65536) at p_spec.c:102
- Z set at spawn: fixed3_from_fvec2(start, DROPSHIP_ALTITUDE) at p_spec.c:514
- P_MoveMobjToward (p_mobj.c:575) only moves X/Y via fixed3_add_planar — Z never changes during flight
- Rendering: r_draw.c:90 — *sy -= fixed_to_float(position.z) * app_cell_h(app) — Z=1.0 renders 1 cell height above grid position
- DC.EXE uses 16.16 fixed-point for X/Y — confirmed by sar ..., 0x10 throughout fcn.0043a144 case 0 (0x00412173, 0x0041217f)
- Rendering Z→screen conversion at fcn.00412174: sar edx, 0x10 → imul edx, ecx (cell_h from [edi + 0x10]) → sar eax, 0xb (divide by 2048). Formula: screen_offset = Z_int * cell_h / 2048
- No altitude constants found: searched for push 0x10000 (FIXED_ONE), push 0x20000, push 0x18000 — none in entire binary
- The only shl eax, 0x10 in the entire binary is at 0x004124d5 inside fcn.00412174 (distance calculation), not in dropship spawn code
- DC.EXE global dropship slots at base 0x4f6c48, stride 0x10 (16 bytes), 128 slots max
- Slot offsets: +0 (flag dword), +4 (script data ptr 1), +8 (byte counter/lifes), +C (script data ptr 2)
- Object array: 0x4f48d8, DC_MAX_OBJECTS = 800, DC_OBJECT_SIZE = 0xdc (220 bytes)
- Per-team data stride within game state: 0xe30 (3632 bytes) — NOT the object struct size
- Field at game_state + team*0xe30 + 0xbbc holds a value used for virtual dispatch via vtable at 0x474360
- DROP.FIN: 29 frames, 13 deps, 10 labels, 136 aux records, 22-byte commands
- Key labels: DROPMOVE0 (84–93, fly), DROPTWO (0–9, unload), DROPSTAND0 (72–81, unused)
Work State
Completed
- Full DROP.FIN binary structure documented in docs/DC_DROPSHIP_ANIMATION.md
- open-rts altitude implementation fully traced: DROPSHIP_ALTITUDE = FIXED_ONE (1.0 cell), never changes, applied as screen Y offset
- Confirmed P_MoveMobjToward and move_unit_if_walkable do NOT modify Z component
- Confirmed rendering path subtracts position.z * cell_h from screen Y
- Verified fcn.004390b0 is a script parser (character classification), NOT object creator
- Verified fcn.0043add4 is a keyword-matching parsing loop, NOT object creation
- Confirmed entire binary has zero shl eax, 0x10 in dropship-related code — only instance in distance function fcn.00412174 at 0x004124d5
- Mapped global slot structure at 0x4f6c48 (stride 0x10, 128 slots) and identified all consumers
- Traced fcn.0043aca4 (dropship tick handler): iterates 128 slots at 0x4f6c48, calls fcn.00439a24 (condition evaluator) and fcn.0043a144 (action executor), decrements counter
- Traced fcn.00439a24 (1646 bytes): script condition interpreter with 23-case switch at 0x4399c8. Reads byte commands from stream, validates tile coordinates, checks tile flags/unit states. This is NOT the object creator
- Traced fcn.0043a144 (2910 bytes): action executor with 22-case switch at 0x43a0ec. Processes linked-list commands (next pointer at +0x18). Case 0 writes Y to game_state + X_int*0xe30 + 0xbbc. Case 6 calls fcn.00439a24 then fcn.00419340
- Traced fcn.0043ae2c (reinforce handler): parses "reinforce"/"reinforce2" from .TRO scripts. Validates slot 0-255, parses "norm"/"trip" type, stores lifes counter, stores script data pointer in slot array
- Traced fcn.0043c2b4 (reinforce trigger parser, called from fcn.0041a61c): initializes 128 slots at 0x4f6c48 (clears counters/data), initializes array at 0x4f9048 (8 entries), loops TRO script lines
- Traced fcn.00419340: stat-tracking function (results.c), NOT unit spawner — writes to 0x490860
- Traced fcn.00419b90: virtual dispatch — reads game_state + edx*0xe30 + 0xbbc, calls vtable at 0x474360. NOT object creator
- Traced rendering function fcn.00412174 (1211 bytes): confirms Z in 16.16 fixed-point with formula screen_offset = Z_int * cell_h / 2048
- Confirmed fcn.00406178 called with (eax=0x4f48d8, edx=0x400) is a tiny wrapper, not "find free object"
- Only reference to object array base 0x4f48d8 is in fcn.0043ad4c at 0x0043ad8d
- Confirmed sync_dropship_parts in open-rts spawns visual effects from ship->actor.core.position (which includes the fixed Z altitude)
Active
- Searching for where the dropship mobj/object is actually created in DC.EXE with its Z/altitude value — the reinforce system sets up conditions and triggers actions but no explicit dropship object creation has been found in the reinforce code path
- The dropship visual may be handled by a separate animation/rendering pipeline that reads DROP.FIN, not through the object array (0x4f48d8)
Blocked
- Cannot determine the retail altitude numeric value — no explicit Z constant (e.g. push 0x10000) found in the binary. The dropship object creation path remains unidentified. The reinforce system (fcn.0043ae2c → fcn.0043aca4 → fcn.00439a24/fcn.0043a144) processes script conditions and actions but never explicitly creates a dropship mobj in the 0x4f48d8 array
- Need to determine if DC.EXE creates the dropship through a different system (e.g., animation pipeline, separate effect system) and what Z value it uses
Next Move
1. Search for the dropship creation outside the reinforce code path — look at the main game loop (fcn.00418818), the animation rendering system, and the 0x4f9048 array (8 entries, initialized in fcn.0043c2b4) which may track active dropships
2. Investigate fcn.0043ad04 (similar to fcn.0043aca4, called from 0x41503e) — this is the other dropship consumer that may create/update the actual dropship object
3. Search for where the object array at 0x4f48d8 is written to with position data (X, Y, Z) by looking for mov dword patterns with offsets divisible by 0xdc
4. If the dropship is not in the object array, trace the rendering pipeline to find where the dropship sprite position (including Z) is computed at draw time
5. Update docs/DC_DROPSHIP_ANIMATION.md with altitude findings and comparison to open-rts DROPSHIP_ALTITUDE = FIXED_ONE
Relevant Files
- docs/DC_DROPSHIP_ANIMATION.md: Dropship animation system report
- docs/DC_EXE_FINDINGS.md: Existing DC.EXE findings (fog-of-war, animation generator)
- games/dark-colony/p_spec.c: Dropship state machine, sync_dropship_parts (line 434), DROPSHIP_ALTITUDE (line 102), spawn (line 514), tick_dropship_state (line 595), spawn_dropship_part (line 418)
- games/dark-colony/w_spr.c: dropship_animation_from_sprites, load_dropship_label
- games/dark-colony/w_spr.h: DropshipAnimation, DropshipFrame, DropshipPart structs
- games/dark-colony/info.h: State enum (S_DROPSHIP_APPROACH etc.), sprite enum
- play/p_mobj.c: P_MoveMobjToward (line 575), move_unit_if_walkable (line 535)
- driver/m_vec.h: fixed3_from_fvec2 (line 75), fixed3_add_planar (line 99)
- render/r_draw.c: R_MapPositionToScreen (line 86), Z→screen Y conversion (line 90)
- data/DCOLONY/ANIMATE/DROP.FIN: Binary animation file, 51370 bytes
- data/DCOLONY/DC.EXE: Retail executable, 566272 bytes
- reverse/dc-exe-r2ghidra/dc_exe.c: Decompiled output (56221 lines)
