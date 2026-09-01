# DC.EXE Rendering and Animation Findings

This document records the Dark Colony executable behavior established while
investigating sprite placement and unit animation in open-rts. It is a focused
technical report, not a complete decompilation. Addresses refer to the exact
executable fingerprint below.

## Executable fingerprint

| Property | Value |
| --- | --- |
| File | `data/DCOLONY/DC.EXE` |
| SHA-256 | `008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a` |
| Size | 566,272 bytes |
| Format | PE32, little-endian, i386, Windows GUI |
| Image base | `0x00400000` |
| Compile timestamp | August 11, 1997 20:53:20 |

The file has the normal DOS-compatible PE header, but it is a 32-bit Windows
executable, not a DOS game binary. It is not stripped, although the available
symbols are not sufficient to recover original function names. Embedded assert
strings expose source-unit names including `animate.c`, `juicel.c`, `mobiles.c`,
`objects.c`, `vobj.c`, `engmain.c`, `sprite.c`, `sprites.c`, `collide.c`,
`path.c`, `ai.c`, and `trigger.c`.

The radare2 installation may print a missing `dplayx.sdb` warning while loading
the executable. This only means its DirectPlay type database is unavailable; it
does not prevent code or data analysis.

## Evidence labels

- **Confirmed** means exact instructions and native data agree, or a focused
   test reproduces the result.
- **Inferred** means multiple observations support a conclusion but one native
   boundary remains untraced.
- **Disproven** records an attractive hypothesis contradicted by executable or
   asset evidence.
- **Unknown** identifies behavior that still requires a controlling caller,
   consumer, or runtime trace.

Decompiler variable names and signatures are never evidence by themselves.
The findings below use instruction addresses and call-site register flow where
r2ghidra's inferred parameters are unreliable.

## Rendering architecture

DC.EXE imports `DirectDrawCreate`, but calls the remaining DirectDraw API through
COM vtables. DirectDraw manages display mode, surfaces, palette, presentation,
and some surface-to-surface composition. Normal game sprites are decoded and
rasterized by the CPU into locked surface memory.

```mermaid
flowchart LR
    A[Game object and FIN frame] --> B[Visual builders<br/>0x00436290 / 0x00436a44]
    B --> C[28-byte render record<br/>0x00432dec]
    C --> D[Sorted render queue<br/>maximum 800 records]
   D --> E[Queue consumer<br/>0x0044f95c]
   E --> F[Specialized world handlers<br/>0x0045c060 and variants]
    F --> G[Raw rasterizer<br/>0x0044b2f0]
    F --> H[RLE rasterizer<br/>0x0044b45c]
    G --> I[Additional DirectDraw surface]
    H --> I
    I --> J[Back buffer]
    J --> K[Primary surface flip]
```

### DirectDraw objects

Graphics setup begins around `0x0042bbf0`. Surface setup at `0x0042bdc4`
creates and stores:

| Address | Object |
| --- | --- |
| `0x004745ec` | `IDirectDraw` |
| `0x004745f0` | Primary surface |
| `0x004745f4` | Attached back buffer |
| `0x004745f8` | Additional rendering surface |
| `0x004745fc` | Palette |
| `0x004c1120` | Start of 32 auxiliary-surface pointers |

Confirmed DirectDraw v1 vtable slots are:

| Interface offset | Operation |
| --- | --- |
| `IDirectDraw +0x18` | `CreateSurface` |
| `IDirectDraw +0x50` | `SetCooperativeLevel` |
| `IDirectDraw +0x54` | `SetDisplayMode(640, 480, 8)` |
| `IDirectDrawSurface +0x1c` | `Blt` |
| `IDirectDrawSurface +0x2c` | `Flip` |
| `IDirectDrawSurface +0x30` | `GetAttachedSurface` |
| `IDirectDrawSurface +0x44` | `GetDC` |
| `IDirectDrawSurface +0x64` | `Lock` |
| `IDirectDrawSurface +0x68` | `ReleaseDC` |
| `IDirectDrawSurface +0x74` | `SetColorKey` |
| `IDirectDrawSurface +0x7c` | `SetPalette` |
| `IDirectDrawSurface +0x80` | `Unlock` |

Presentation routine `0x0042b54c` blits the additional surface to the back
buffer, optionally composites one of the auxiliary surfaces, and flips the
primary surface. This is distinct from per-sprite drawing.

### Render queue

Gameplay visual builders at `0x00436290` and `0x00436a44` submit 28-byte
records through `0x00432dec`. The queue has a native limit of 800 records. This
matches the broader executable convention of fixed-capacity object storage;
open-rts preserves `DC_MAX_OBJECTS == 800` and the observed object stride
`DC_OBJECT_SIZE == 0xdc` in its Dark Colony model.

The builder combines the object's fixed-point position with each FIN command
before submitting it. On the normal, non-shaking path, the relevant arithmetic
in `0x00436290` is:

```text
queue_x = object_x + FIN.runtime_x
queue_y = object_z + FIN.runtime_y
```

The secondary visual-object builder at `0x00436a44` performs the same
composition at `0x00436baf..0x00436bd2`: it adds the object's two position
words to the command's runtime X/Y fields before calling `0x00432dec`.

`0x00432dec` converts those values from eighth-pixels and reverses the native
bottom-up Y axis when writing the queue record. Consumer `0x0044f95c` selects a
specialized renderer from record byte `+0x17`. For selector zero it calls
`0x0045c060` normally and `0x0045c41c` for the mirrored form. Together with the
FIN loader's `runtime_x = FIN.x * 8` and `runtime_y = FIN.y * -8`, normal
top-down placement is:

```text
draw_x = object_screen_x + FIN.x + cell.disX
draw_y = object_screen_y + FIN.y - cell.height
```

At `0x0045c0bd..0x0045c0c4`, the normal handler loads `[cell+4]` (`disX`)
and adds it to X. At `0x0045c0df..0x0045c0e3`, it loads `[cell+2]`
(`height`) and subtracts it from Y. It never reads `[cell+6]` (`disY`).
The mirrored handler `0x0045c41c` instead adds cell width to its initial X at
`0x0045c479..0x0045c47f` and supplies a negative horizontal step to the raster
path. Its covered left edge is the FIN X coordinate; an SDL port mirrors within
a width-sized destination at `object_screen_x + FIN.x`, without adding `disX`.
It uses the same `FIN.y - cell.height` top edge at
`0x0045c49a..0x0045c49e`.

The confirmed portion of each 28-byte record written at `0x004dc0ac` is:

| Record offset | Observed value |
| --- | --- |
| `+0x00` | Selected runtime SPR cell pointer |
| `+0x08` | Descending queue sequence key |
| `+0x0c` | Draw X after fixed-point division by eight |
| `+0x0e` | View-height-minus-draw-Y after fixed-point division |
| `+0x12` | Negated object Z after fixed-point division |
| `+0x14..+0x18` | Packed command/render fields |

`0x00432de0` initializes the sequence key from a value rounded down to a
multiple of eight. `0x00432dec` stores that key and decrements it after each
part, preserving order among parts submitted by one animation frame. Consumer
`0x0044f95c` culls, sorts, and dispatches records. Record `+0x17` has observed
values zero through five; nonzero selectors reach scaled or remapped handlers
around `0x0045c7b0`, `0x0045cc04`, `0x0045d334`, `0x0045d358`, `0x0045d6d4`,
and `0x0045d6f8`. Selector 2 enters `0x0045d334`, which delegates to
`0x0045d358`. Its setup at `0x0045d3b5..0x0045d3db` adds cell `disX` to the
queued X coordinate and subtracts cell height from the queued Y coordinate.
Thus selector 2 uses the same destination origin formula as selector zero:

```text
draw_x = object_screen_x + FIN.x + cell.disX
draw_y = object_screen_y + FIN.y - cell.height
```

The later body implements the selector's specialized pixel composition. The
complete semantic names and packed-field mapping for selectors one through
five remain **unknown**.

**Disproven:** the queue was previously reported as reaching dispatcher
`0x0044b5e4` through a callback installed by constructor `0x004297e4`. That
dispatcher exists and its own displacement arithmetic was read correctly, but
it belongs to a separate generic sprite API. It is not the selector-zero world
record consumer. The mistake was plausible because indirect dispatch hides
call references and both paths ultimately use the same raster machinery.

## Native SPR behavior

The SPR loader at `0x0044b048` reads:

1. Header flags, cell count, and payload size.
2. A palette of 256 RGB triplets.
3. One 8-byte descriptor for every cell.
4. Raw or chunked pixel payloads.

An on-disk cell descriptor is:

```c
struct DcSprCellDisk {
    uint16_t width;
    uint16_t height;
    uint16_t dis_x;
    uint16_t dis_y;
};
```

The loader expands this to a 24-byte runtime descriptor containing the four
words, data pointers, a used marker, and decoded or chunk byte counts. Header
flags masked by `0x180` select the chunked/RLE path.

### Generic dispatcher versus world records

Dispatcher `0x0044b5e4` validates the frame index, obtains the runtime cell,
adds both displacement fields to the requested point, clips, and selects the
raw or RLE rasterizer:

```text
draw_x = input_x + cell.disX
draw_y = input_y + cell.disY
```

The relevant instructions load `[cell+4]` for `disX` and `[cell+6]` for
`disY`, then add each to the corresponding draw coordinate before clipping.
This formula is **confirmed for that generic API only**. It must not be applied
to queued world records, whose selector-zero handlers use `disX` and height as
described above. The original purpose of `disY` outside `0x0044b5e4` remains
**unknown**.

The raw rasterizer at `0x0044b2f0` and RLE rasterizer at `0x0044b45c` advance
source and destination scanlines forward. The world handlers establish the top
edge by subtracting height before rasterization; the rasterizers do not perform
a later vertical inversion.

### Native screenshot comparison

Native 640x480 gameplay captures from MobyGames (Dark Colony screenshot
`395046`), My Abandonware's Dark Colony gallery, and the Dark Colony Wiki's
"Active Exploiter" image were compared with production-renderer captures. They
disprove the earlier `FIN.y + disY` implementation: it tears apart the human
landing structure and separates the visible Petra-7 plume from the deployed
Exploiter. Restoring per-cell height subtraction produces the native
top-to-bottom city silhouette and Exploiter relationship. This is supporting
visual evidence; the coordinate rule itself comes from `0x0045c060` and
`0x0045c41c`.

## Native FIN behavior

The FIN loader at `0x004230ac` loads files from `animate/%s`, resolves their SPR
dependencies under `SPRITES/`, reads animation labels and frame metadata, and
expands 22-byte draw commands. Internal error strings call FIN files "juice
files" and label ranges "angles".

Each draw command contains:

```text
char sprite_name[8]
i16  sprite_cell
i16  x
i16  y
i16  remap
i16  intensity
i16  layer
i16  flags
```

The loader converts authored FIN coordinates to fixed render units:

```text
runtime_x = FIN.x * 8
runtime_y = FIN.y * -8
```

The render path later converts these fixed values back while preserving the
authored integer offsets. A displayed part is therefore not defined by its SPR
cell alone. Its identity is the combination of SPR cell, FIN x/y, flip flags,
remap, intensity, and layer.

The exact semantic names of layer values `0`, `1`, `3`, and `5`, and of FIN flag
value `1`, have not yet been proved from every consumer. They should remain
native numeric fields until those paths are traced.

### Frame timing

FIN frame word `+2` is a raw delay. At `0x00423544`, DC.EXE replaces zero with
`15`. Instructions at `0x00423563..0x0042358f` then calculate:

```text
runtime_ticks = ((raw_ticks + 3) * 19) / 100
```

This uses integer arithmetic. For example, the eight frames of `REAPMOVE0`
store:

```text
20, 13, 13, 20, 6, 13, 13, 6
```

They become:

```text
4, 3, 3, 4, 1, 3, 3, 1
```

The varying delays are part of the animation. Replacing them with a uniform
duration makes the cycle look incomplete even when every timeline step is
present. `EXPLMOVE*` uses zero raw delays; zero normalization gives those frames
three runtime ticks.

### Barracks Trooper production

**Confirmed:** Trooper product row 9 in `gamestat/depend.txt` has cost 350,
UI id 89, product class 1, product type 0, and prerequisite `{1}`. The native
dependency path checks affordability before emitting the build request. In
open-rts, clicking the darkened Trooper icon with less than 350 Petra-7 is
therefore expected to do nothing; hovering the icon shows `Trooper 350` in red.

The release visual is label `TRSCBUILD0` in `ANIMATE/HUBU.FIN`. It contains 22
frames, each with raw delay 6. Applying the executable's conversion above gives
one native 19 Hz runtime tick per frame, so the complete release lasts 22 native
ticks, approximately 1.16 seconds. Open-rts runs at 30 Hz and represents the
same elapsed time with 35 cumulatively rounded simulation ticks. Copying raw 6
directly into each state made the release 4.4 seconds long; treating the 22
native ticks as 22 open-rts ticks made it too fast at approximately 733 ms.

The FIN-authored sequence is:

| Release frames | Visible parts | FIN positions |
| --- | --- | --- |
| 1-3 | HUBU cells 12, 13, 14 | `(-36,27)` |
| 4-9 | HUBU cell 15 plus TRSC cells 72, 16, 24, 32, 40, 48 | HUBU `(-36,27)`; TRSC `(-156,36..58)` |
| 10-12 | HUBU cells 14, 13, 12 plus TRSC cells 56, 64, 72 | HUBU `(-36,27)`; TRSC `(-156,57..59)` |
| 13-18 | TRSC cells 16, 24, 32, 40, 48, 56 | `(-156,68..79)` |
| 19-22 | TRSC cells 8, 1, 2, 18 | `(-155,78)`, `(-154,80)`, `(-153,79)`, `(-143,79)` |

The normal south-facing Trooper standing command is at `(-159,0)`. The live
unit handoff must preserve the final release image on screen, so its world
offset from the Barracks origin is the command delta:

```text
x = -143 - (-159) = +16 pixels
y = -(79 - 0)      = -79 pixels
```

The Y negation is the existing FIN-top-left to Y-up world conversion. This is
the source of the open-rts spawn offset; it is not a guessed free-cell radius.

At `0x00438c95..0x00438d16`, the native object-type loader searches first for
`BUILDSTAND`, then `BUILD`, and stores the resulting animation pointer at type
offset `+0x98`. At `0x00441430..0x00441450`, the city/object setup path can
initialize an object's animation directly from that `+0x98` pointer and return
before ordinary city-slot placement. This confirms a dedicated build-animation
initialization path rather than a generic sprite overlay. Open-rts must replace
the Barracks standing state with this chain and return to the standing state at
completion; drawing the chain as an effect leaves the closed Barracks underneath
and produces two overlapping doors. The exact caller-side
condition that distinguishes every construction and troop-release case remains
**inferred** because the large scenario loader supplies its arguments through
registers and several call sites.

The player/AI dependency path calls `0x0040bbe8` after cost deduction. That
routine creates a 7-byte network message with opcode `0x0a`; the literal 7 is
the message length, not an action type. Treating it as “action type 7” is
**disproven**.

**Unknown:** no executable-backed Trooper training countdown was established in
this pass. Open-rts currently uses `cost * 10 ms`, or 3.5 seconds for a Trooper;
that is a heuristic and must not be described as a DC 1:1 value. Likewise, the
open-rts 2.75-cell crowd selection and 1.5-cell doorway-clearing move are not
yet supported by this disassembly. Native queue progression, blocked-exit
behavior, and any rally/spacing command require tracing the opcode `0x0a`
consumer and the completion callback.

## Directional animation

Function `0x00423a50` constructs directional animation sets:

1. It concatenates the unit stem and state keyword using `%s%s` at
   `0x0046fccc`.
2. It probes 16 numbered labels using `%s%d` at `0x0046fcd4`.
3. The probed suffix is `(12 - index) & 15`.
4. It expands the 16 results into a 32-entry heading table.
5. Missing labels use a nearby available angle selected through the fallback
   table at `0x00474500`.

This establishes that odd-numbered angle labels are real runtime inputs, not
discarded editor data. State keywords visible near the unit-definition loader
include `MOVE`, `STAND`, `DIEA`, `DIEB`, `DIEC`, `DEPLOY`, `FUNK`,
`BUILDSTAND`, `BUILD`, `SCRCH`, `BURN`, and `BLOOD%c`.

### Reaper

`REAP.FIN` has eight principal movement ranges with eight timeline frames each:

| Label | Inclusive FIN frames |
| --- | --- |
| `REAPMOVE0` | `16..23` |
| `REAPMOVE14` | `24..31` |
| `REAPMOVE12` | `32..39` |
| `REAPMOVE10` | `40..47` |
| `REAPMOVE8` | `48..55` |
| `REAPMOVE2` | `56..63` |
| `REAPMOVE4` | `64..71` |
| `REAPMOVE6` | `72..79` |

Odd `REAPMOVE1/3/.../15` labels contain single intermediate-angle poses. Fire
ranges contain five frames. Death ranges vary and contain 10, 11, 14, 15, 26,
or 27 frames depending on direction.

The even-direction walk cycles deliberately reuse cells with different flags
and offsets. `REAPMOVE0` contains:

```text
9, 17, 25, 33, 9 flipped, 17 flipped, 25 flipped, 33 flipped
```

The flipped half has x offsets around `-24`, while the unflipped half has offsets
around `-157`. Counting unique SPR cell numbers yields only four, but DC.EXE
displays an eight-step cycle because flip state, FIN offset, and timing are all
part of each step. Mirroring an already-normalized canvas around its center is
not equivalent to this behavior.

### Direction-code orientation

**Inferred:** Dark Colony's 16-way FIN direction codes use `0 = south` and
increase counterclockwise: `4 = east`, `8 = north`, and `12 = west`. Treating
code zero as north reverses only vertical sprite facings while horizontal
facings remain correct, matching the observed Trooper failure. Open-rts maps
these native codes to its Doom-style angles with south as the first angle and
counterclockwise progression; movement vectors remain in native Y-up world
coordinates.

The focused cardinal regression and Human01-Human03 model scenarios reproduce
the result:

```sh
make build/bin/test_game_model_headless
build/bin/test_game_model_headless
```

The exact DC.EXE routine that converts movement deltas to FIN direction codes
remains **unknown** and should be traced before promoting this from inferred to
confirmed.

### Exploiter

`EXPL.FIN` also supplies all 16 directional poses:

- Even `EXPLSTAND*` labels provide the principal standing facings.
- Odd `EXPLSHUF1/3/.../15` labels provide intermediate poses.
- Even `EXPLMOVE0/2/.../14` labels provide two-frame movement cycles.
- Later odd `EXPLMOVE1/3/.../15` labels provide one-frame poses.

Open-rts emits all 16 direction codes for Exploiter stand and run states. The
standing state alternates the even `EXPLSTAND*` and odd `EXPLSHUF*` labels;
these resolve to `EXPL.SPR` frames `0,1,2,3,4,5,6,7,8,7,6,5,4,3,2,1`, with
the final seven slots flipped as directed by the FIN commands. Run state 1 uses
frames `0,1,2,3,4,5,6,7,8,7,6,5,4,3,2,1`; run state 2 uses
`9,1,10,3,11,5,12,7,13,7,12,5,11,3,10,1`. Thus even `EXPLMOVE*` headings
animate through two frames while each odd heading retains its native one-frame
pose. Deploy remains eight-way because harvesting explicitly rotates the unit
to direction code 6 before entering that animation.

It remains unresolved whether DC.EXE selects `SHUF` specifically during a
turn-in-place transition and the later odd `MOVE` set during translation, or
uses another gameplay distinction. That question belongs to callers of the
animation-set constructor, not to the generic renderer.

## Consequences for open-rts

The executable evidence gives several implementation rules:

1. For selector-zero queued world sprites, apply `disX` only when unmirrored
   and subtract cell height from Y; do not apply `disY`.
2. Preserve FIN x/y independently for every body and overlay part.
3. Treat a frame as cell plus flags, offsets, remap, intensity, and layer.
4. Preserve converted per-frame FIN timing rather than assigning one duration
   to an entire sequence.
5. Preserve 16-angle labels where the assets provide them.
6. Implement missing-angle selection as animation-table behavior, not as a
   sprite-name-specific rendering exception.
7. Keep the fixed render queue and native object-layout limits visible while
   reconstructing behavior.

These findings rule out moving terrain, gameplay coordinates, or city slots to
compensate for sprite misalignment. Visual corrections should reproduce the
specialized queue handler selected by the native render record.

DC.EXE does contain literal city-slot geometry at `0x00475b64`:
`(-64,15)`, `(0,0)`, `(32,64)`, `(64,10)`, `(-32,65)`, `(0,32)`, and `(0,0)`.
Function `0x004412d4` multiplies each component by eight and adds it to the city
anchor before writing object `x_pos` and `z_pos`. These scattered coordinates
are the native gameplay and occupancy positions, not the FIN animation origins.

The native city object anchor is the second `%AISlots` pair. The scenario loader
stores that pair in team fields `+0x2c/+0x30`; `0x00441468..0x004414b3` shifts
both values left by eight and adds the corresponding slot-table component
shifted left by three. The values are object origins, not cell centers. For
Human02 this makes the native object origin `(56,55)`.

This is a building-specific coordinate convention, but it is **not** a
city-only `Y + 0.5` adjustment. The mobile-object constructor in `mobiles.c`,
function `0x00419d44`, converts both integer cell coordinates to cell centers:

```text
0x00419f1a..0x00419f29: x_pos = (cell_x << 8) + 0x80
0x00419f33..0x00419f3e: z_pos = (cell_z << 8) + 0x80
```

The city constructor instead omits `0x80` from both axes. City buildings are
therefore special because gameplay uses exact object origins and the city slot
table, while ordinary mobiles use centered cells. The visual path has an
additional city-specific step: for object indices below 120 whose slot modulo
15 is below six, `0x0043654f..0x00436567` calls `0x00441080` to recover that
slot's table offset multiplied by eight. Queue construction then computes:

```text
0x00436662..0x00436675: draw_z = object_z - slot_z * 8 + FIN.runtime_y
0x0043667c..0x00436687: draw_x = object_x - slot_x * 8 + FIN.runtime_x
```

Because construction added the same offsets, all city FIN animations share the
second `%AISlots` origin while their gameplay objects remain scattered. The
sort-key setup at `0x004365a7..0x004365c0` still receives raw object Z before
the subtraction. `TOWR.FIN` then supplies its ordinary pixel command offset
`(-36,-9)` relative to the shared origin.

**Disproven:** retail city location `(x, y + 0.5)`. For Human02, DC.EXE stores
the TOWR city object at `(56,55)` before its slot offset, not `(56,55.5)` or a
mixed `(56,53.5)` anchor. This can be reproduced with:

```sh
r2 -q -e bin.cache=true -A -c "pd 30 @ 0x00441457" -c q data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -A -c "pd 24 @ 0x00419f10" -c q data/DCOLONY/DC.EXE
```

Open-rts previously replaced the native shared render origin with a mixed anchor
assembled from the second `%AISlots` X and centered first `%AISlots` Y. For
Human02 that produced `(56,53.5)`. That mixed anchor had no executable basis and
has been removed. Open-rts now preserves each scattered object coordinate and
subtracts only its native slot offset for rendering, leaving the shared visual
origin at the native `(56,55)`. A one-tile downward sprite offset aligns that
integer boundary with the terrain row without rewriting the model coordinate.

The final one-row presentation offset is **inferred** from the renderer boundary
and retail screenshot comparison, not an additional subtraction found in the
city constructor. It is the same class of bottom-up row/point mismatch as the
Human02 vent: scenario and model coordinates remain native while rendering
accounts for the integer boundary convention.

`VENT.FIN` provides the active Petra-7 glow placement. Every `VENTSTAND0` frame
contains VENT2 cell 0 at FIN coordinate `(-40,12)`, remap `1`, intensity `16`,
layer `0`, and flags `0`. VENT2 cell 0 has width 23, height 16, and displacement
`(31,37)`. Native screenshot comparison shows that applying the selector-zero
world formula to this remapped command places the glow 29 pixels above its
terrain-baked crater. The generic displaced placement gives destination offset
`(-9,25)` from the animation origin:

```text
x = -40 + 31 = -9
y = -12 + 37 = 25
```

Open-rts's decoration API stores a pivot that is subtracted from the world
anchor. Its VENT2 adapter therefore stores pivot `(9,-25)`, the negation of
that destination offset. Treating the command as selector zero produced pivot
`(9,4)` and placed the plume 29 pixels too high. The exact native selector
chosen by remap/layer packing remains unknown; this correction is inferred from
the preserved FIN/SPR metadata and native screenshot alignment rather than a
fully traced nonzero queue handler.

The yellow smoke above an unattached vent is a separate `PUFF.SPR` part, not an
animation within `VENT2.SPR`. This is **confirmed** by `VENT.FIN`: label
`VENTSTAND0` spans FIN frames 19 through 38 and advances the puff through cells
`0, 0, 1, ..., 18`. The first puff command is at `(-39,6)`, remap `0`, while
the remaining commands use remap `2` and rise to Y `-17`. The underlying VENT2
cell remains fixed at `(-40,12)` throughout the sequence. The first two FIN
frames have raw duration 26; the remaining zero durations use the established
15-tick fallback. With the generator's FIN-to-runtime conversion, these become
five and three 30 Hz simulation tics respectively, giving an approximately
100 ms frame cadence for most of the loop. Reproduce with the FIN parser in
`tests/test_dark_colony_sprite_layout.c` against
`data/DCOLONY/ANIMATE/VENT.FIN` and the raw cell descriptors in
`data/DCOLONY/SPRITES/PUFF.SPR`.

Selector-2 disassembly confirms that each PUFF cell must use its own FIN
coordinate, `disX`, and height. Across the 20 FIN frames, the primary puff's
top edge is:

```text
-4, -4, -7, -11, -13, -17, -21, -25, -28, -31,
-33, -36, -39, -43, -45, -48, -51, -49, -51, -54
```

The two-pixel correction at cell 16 comes from the changing sprite shape; the
overall plume rises 50 pixels. Open-rts previously cycled PUFF cells against
one hardcoded pivot `(5,-19)`, derived from the generic `disY` convention. That
discarded every FIN coordinate and could make growth within the changing cell
bounds read as downward motion. The vent decoration now stores the authored
cell, world-render pivot, and converted duration for each FIN frame. The first
two steps last 167 ms and the remaining steps last 100 ms.

Retail observation shows this smoke loop only while no Exploiter is attached.
That behavior is **observed**, while the DC.EXE branch that suppresses the
smoke remains **unknown**. Open-rts therefore tracks the puff separately from
the persistent VENT2 glow and hides it whenever a live harvester targeting the
vent is in its mining phase; it becomes visible again when the harvester
leaves. This avoids incorrectly removing the vent itself during a return trip.

The routine at `0x004128c4` confirms that Dark Colony uses the shared object and
occupancy system rather than sprite bounds for interaction. At
`0x004128f6..0x00412904` it reads 8.8 fixed-point `x_pos` and `z_pos` fields at
offsets `+0x00/+0x04`, shifts both right by eight, and looks up the object in
that map cell. This is a validation/interaction path, not evidence that an
approaching unit must stop on the first sub-cell position inside the tile.

At `0x00412b0d..0x00412b20`, the same routine changes a human Exploiter object
from GAMESTAT type `6` (`EXPL`) to type `47` (`EDPLY`); the alien harvester
equivalently changes from type `14` to type `48`. It then starts the replacement
type's animation without writing either position field. Deployment is therefore
an in-place object-type/state transition. The native renderer continues to use
the preserved object origin with the new type's FIN/SPR frame parts.

The first-entry interpretation was **disproven** by the resulting visible
one-cell separation between the Exploiter and vent. A grid-and-anchor capture of
Human02 showed that the active crater/plume occupies the map row immediately
below the raw scenario object's row: scenario vent `(69,48)` has its visual
interaction center at `(69.5,47.5)`. Open-rts preserves `(69,48)` as the vent's
script identity and stores `(69.5,47.5)` as its attachment point. Movement and
the harvesting distance check both use that attachment point. This correction
is **inferred** from the scenario/map composition and rendered grid; the native
order/path routine that transforms a clicked vent into its stop point remains
untraced. No gameplay coordinate is derived from EXPL frame bounds.

## Known unknowns

## Combat flash, blood, and ground illumination (2026-09-01)

**Confirmed.** The reference executable is `data/DCOLONY/DC.EXE`, PE32/i386,
566272 bytes, stamped 1997-08-11 (the fingerprint was obtained with
`rabin2 -I`). Its native animation data is external: the combat visuals are
provided by the FIN command tables and the companion SPR files, rather than
embedded in the executable.

**Confirmed from native data.** The FIN fire labels use layer-3 `BLAZ` commands
for the bright muzzle attachment. The existing FIN extraction path selects the
nearest following layer-3 command after the layer-1 body command and preserves
its per-direction x/y placement. `BLOO.SPR` is a separately referenced combat
effect sprite; the animation tables also contain `HIT*` and `BLOOD%c` effect
families for other reactions. This is why the runtime uses `BLAZ.SPR` for the
short muzzle flash and `BLOO.SPR` at the damaged unit, instead of drawing a
placeholder shape.

**Implementation consequence.** Dark Colony actor defaults now name the native
flash and blood assets for every combat actor, including actors whose generated
body state table does not yet expose a missile state. Each attack also creates
a short-lived additive ground-light record at the attacker's map origin. The
light is rendered as a fading, flattened ground glow in screen space; it does
not move the unit or alter FIN attachment coordinates. A focused reproduction
is:

```sh
make build/bin/dark-colony
env SDL_VIDEODRIVER=dummy build/bin/dark-colony --check --game dark-colony
```

**Inferred.** The exact native lighting primitive is not yet isolated in
`DC.EXE`; the ground glow is therefore a renderer-side approximation of the
visible muzzle illumination, while the BLAZ/BLOO asset selection and FIN muzzle
placement are native-data-derived.

- The complete semantics and ordering rules for every FIN layer value.
- The exact meaning of all FIN flag bits beyond observed horizontal flipping.
- Which gameplay transition chooses Exploiter `SHUF` versus odd `MOVE` poses.
- The complete sort-key layout of the 28-byte render queue record.
- The role of each of the 32 auxiliary DirectDraw surfaces.
- The native order/path routine that decides the exact Exploiter stop position
   before the interaction handler runs.
- Whether `DC16.EXE` uses identical routines and addresses; this report covers
  the fingerprinted `DC.EXE` only.

## Reproducing the analysis

Follow the workflow in `REVERSE_ENGINEERING.md`. Useful starting commands are:

```sh
rabin2 -I data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -A -c "pdf @ 0x0044b5e4" -c q data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -A -c "pdf @ 0x004230ac" -c q data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -A -c "pdf @ 0x00423a50" -c q data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -A -c "pdf @ 0x0042b54c" -c q data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -A -c "pdf @ 0x00432dec" -c q data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -A -c "pdf @ 0x00436290" -c q data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -A -c "pdf @ 0x00436a44" -c q data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -A -c "pdf @ 0x0044f95c" -c q data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -A -c "pdf @ 0x0045c060" -c q data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -A -c "pdf @ 0x0045c41c" -c q data/DCOLONY/DC.EXE
```

Decompiler signatures and variable names are provisional. Verify conclusions
against exact instructions, callers, native SPR/FIN bytes, and visible game
behavior before porting them.
