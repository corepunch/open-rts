# DKREIGN.EXE HUD disassembly

Documented September 30, 2026. Engine baseline: `047477a`.
Executable fingerprint: [DR_DISASSEMBLY.md](DR_DISASSEMBLY.md#executable-fingerprint).
Evidence comes from the existing broad r2ghidra dump, focused disassembly,
retail BMP/PCX/palette files and prior supplied screenshots. The detailed
dated record is [DR_EXE_FINDINGS.md](DR_EXE_FINDINGS.md#hud-assets-coordinates-and-draw-order).

## Native image descriptor table

**Confirmed:** table `0x005be94c` has 29 records, stride 12, with 16-bit
width/height, ID and filename pointer. Names begin at `0x005beac4`.
`0x004957b0` iterates the records; `0x00495660` validates loaded dimensions.
Assets live under `dark/graphics/INTFACE/IGI/`.

| Image | Native dimensions | Role |
|---|---|---|
| `TOPBTNS.BMP` | 882x32 | Top-button states |
| `TOPBITS.BMP` | 154x32 | Top edges and money housing |
| `MFDBTNS.BMP` | 576x64 | Six page buttons and their states |
| `MINIMAP.BMP` | 140x138 | Radar housing |
| `RESOBARS.BMP` | 104x104 | Resource-gauge housing/states |
| `BUISOBOX.BMP` | 192x50 | Production slot normal/hover/pressed |

## Layout and draw order

**Confirmed:** static-frame drawer `0x00494d70` places radar chrome at
`(448,342)`, TOPBITS' six-pixel left edge at `(0,0)` and its seven-pixel
right edge at `(441,0)`. Top button zones are 49x32 at x=6,55,104 and
x=294,343,392. The money housing occupies x=153..293, using TOPBITS source
x=6,width=141.

BUILD drawing in `0x004947b0` clears `(448,64,192,250)` and draws BUBLDBIT
at `(448,314)`. MFDBAC1 is a communications background; it must not be treated
as the BUILD grid's native background.

| Logical region | Geometry / rule |
|---|---|
| Page buttons | Three columns of 64 pixels, two rows of 32 |
| BUILD grid | `(448,64,192,250)` |
| Production cell | 64x50, three columns and five visible rows |
| Menu image placement | Slot origin plus `(9,2)`, native dimensions |
| Slot frame | Draw BUISOBOX over the image |
| Small footer buttons | 71x22; disabled source starts at x=213 |

List constructor `0x00468300` supplies height 50, `0x004b4990(64)` supplies
width 64, and drawer `0x004b4fa0` invokes slot callbacks even for empty slots.
Unit callback `0x0048d900` and building callback `0x0048dd30` place the image
without scaling to fit, then draw the frame.

**Disproven:** four 62x61 production cells, gray substitute slot rectangles,
or putting SCROLL over the second tab row.

## Page and slot states

**Confirmed:** page drawer `0x00490770` selects source:

```text
source_x = column*64 + state*192
source_y = row*32
state: 0 normal, 1 hover, 2 active
```

Active BUILD starts at source x=384. BUISOBOX cell states use x=0,64,128.
COMMS, MENU, ORDERS, PATHS and SPECIAL are actual pages, not infantry/vehicle
categories or aliases for stop/move/attack.

**Confirmed engine behavior:** BUILD lists units unless a Construction Rig is
selected, in which case it lists structures. This works for FG and Imperium.
Lists follow loaded definition order and scenario tech limits. Every authored
production entry has exactly one retail menu icon, from `SetMenuImage` or
the third `SetBuildingImages` argument. Availability uses the shared producer,
technology, money and queue rules.

**Unported:** COMMS/ORDERS/PATHS/SPECIAL contents, full MENU behavior, upgrade
and decoy interactions. MENU currently opens an engine resume/quit popup.
Those interactions are not certified retail page behavior.

## Money and font decoding

**Confirmed:** `0x00490900`, money case `0x00490f13..0x00490fae`, formats
`%0.9d`, substitutes ':' for up to eight leading zeroes, and draws font 2 at
panel+(25,6), screen `(178,6)`. ':' is a dark digit placeholder in this font.

`0x0049594a..0x00495979` loads FONT16.PCX as font 2. FONT12W/FONT12T supply
other native fonts. Loader `0x00469530` uses the first row's first pixel as a
delimiter; starts begin at x=1, and delimiter positions define successive byte
character widths. The engine's general BMP/PCX decoder lives in
[driver/w_image.c](../driver/w_image.c); delimiter interpretation belongs to
[games/dark-reign/hud/sb_bar.c](../games/dark-reign/hud/sb_bar.c).

The embedded PCX palette is not the active GUI palette. Native chrome-palette
translation gives cyan money digits. `0x00477e00` initializes translation
tables from `0x005cc9c0` entries `2,1,4,3,5,6,7,0,2`; indices 32..41 receive
`entry*8`, so normal text maps those indices to 48..57.

**Disproven:** white money from the PCX palette and purple footer labels from
untranslated text indices.

## Unavailable-red palette

**Confirmed:** palette loader `0x0048b030` reads PALS version `0x102`, then
six 256-byte channels and a 32,768-byte RGB555 lookup at file offset 1544.
`0x0048ad10` builds unavailable-red table `0x006d6a00`:

```text
red_translation[i] = lookup[((R[i] + G[i] + B[i]) / 6) << 10]
```

The first three native channels provide R/G/B. Production callback
`0x0048d900` selects this translation for an unavailable unit. The engine uses
the recovered lookup rather than an arbitrary RGB tint.

## Radar

**Confirmed:** `0x0048fac0` uses one cell/pixel, clips to native extents and
centers inside the housing:

```text
w = min(map_width, 130); h = min(map_height, 127)
x = 453 + (130-w)/2; y = 348 + (127-h)/2
M01F: (488,381,60,60)
```

The old 128x126 stretch was incorrect. Current radar shows object markers and
the camera outline and supports camera panning when radar capability exists.

**Identified but unported:** setup calls `0x0047ad10`, which uses tile
information from `0x004190c0`/`0x00413d20` and brightness offsets at
`0x005b9410` into palette lighting lookups. Exact terrain-color output has
not been implemented; no sample-pixel or guessed-color substitute is used.

## Resource gauges

**Confirmed in prior focused analysis:** `0x0048f340` smooths supply/demand
and maximum per-building water stock using signed delta/16, with a minimum
one-unit step. Power scale changes in five-step increments. Fill is x=596,
width=17, height=`supply*81/scale`, bottom-aligned at y=473, with a demand
tick. Native colors are `0x16`, `0x18`, `0x8a`, selected by supply/demand.

**Unknown/unported:** full economy inputs and the controlling dynamic scale.
Only the gauge frame is drawn; a plausible fill from engine credits would
not reproduce the native supply/demand model.

## Implementation and verification

The game owns one active `sb_state_t` through `G_CustomUI*`; the generic HUD
does not become a second owner. `hud/sb_bar.c` supplies image/icon lifetimes,
while the game HUD owns native layout, fonts, palette and drawing/input.
See [DR_ARCHITECTURE.md](DR_ARCHITECTURE.md) for source boundaries.

`test_mission_hud` checks M01F start, native minimap geometry, completed-building
layer pixels, combined faction icon coverage, FG production availability, MENU
and Imperium rig structure dispatch. The Imperium check deliberately narrows
the fixture's tech list; it is not an entire Imperium campaign certification.

```sh
r2 -q -e bin.cache=true -c 'af @ 0x4957b0' \
  -c 'pdf @ 0x4957b0' -c q data/REIGN/dkreign.exe
r2 -q -e bin.cache=true -c 'af @ 0x48d900' \
  -c 'pdf @ 0x48d900' -c q data/REIGN/dkreign.exe
r2 -q -e bin.cache=true -c 'af @ 0x48f340' \
  -c 'pdf @ 0x48f340' -c q data/REIGN/dkreign.exe
make build/bin/tests/dark-reign/test_mission_hud
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-reign/test_mission_hud
env SDL_VIDEODRIVER=dummy build/bin/dark-reign --screenshot /private/tmp/open-rts-dr-hud.bmp
```

The focused test also writes `/private/tmp/open-rts-mission01-hud.bmp` and
`/private/tmp/open-rts-imperium-hud.bmp`. Native game assets remain BMP/PCX/SPR;
temporary inspection conversions do not add PNG assets or loading dependencies.
