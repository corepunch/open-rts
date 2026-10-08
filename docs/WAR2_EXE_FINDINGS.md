# Warcraft II gathering and HUD evidence (2026-10-04)

## Static DOS executable analysis toolchain (2026-10-07)

**Confirmed / boundary.** Retail Warcraft II was not executed. The user
explicitly prohibits it because no CD key is available. Static analysis uses
radare2 6.2.2 (`ad27058877024389292fddf12e1db6e13824ba34`) and r2ghidra
6.2.2 (`1b5cba403c4c8751db8434f6790d5e0f132038f4`). The matching versions
were built for macOS arm64 under `/private/tmp/war2-analysis-tools`, installed
only to its `prefix` directory. r2ghidra embeds the native Ghidra decompiler;
Java and a full Ghidra installation are unnecessary. Its packaged dependency
is ghidra-native `483ae94bcbc661a77667e52f1eff75928cb6aa2e`, with the
r2ghidra distribution patches, and zlib 1.3.1
`51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf`. x86 Sleigh specifications and
the plugin are installed in that prefix. Configure alone does not fetch its
dependencies: run `make -C subprojects` before `make`.

**Confirmed executable chain.** Original `data/WAR2/WAR2.EXE` is 878,119
bytes, SHA-256
`a2b4b2118ec6355371b58134be8c7331d1facc5989e7188a1d1bb68fd1f26671`.
The first MZ belongs to the bound DOS loader. Its page count 123 and final
page length 116 locate the first BW header at
`(123 - 1) * 512 + 116 = 0xf474`. The Open Watcom DOS/16M structure
defines the 32-bit `next_header_pos` at BW `+0x1c`. Native links are:

| Original file offset | Header / next link |
|---|---|
| `0x000000` | bound loader MZ; extent `0xf474` |
| `0x00f474` | BW; next header `0x1e0c4` |
| `0x01e0c4` | BW; next header `0x352a4` |
| `0x0352a4` | game MZ; `e_lfanew = 0x2a50` |
| `0x037cf4` | game LE header |

The first MZ's bytes at `+0x3c` are not the game's LE pointer. Directly
using them misses the game image. Initial P3/compression speculation from
incidental signature matches is **disproven as the required extraction
path**: following the actual header links exposes the LE image without
decompression, header edits or execution. No general claim about every
bound-loader component's encoding is made.

An unchanged copy starting at `0x352a4` has 660,355 bytes and SHA-256
`8ee7971db360e2d3b5bc8089a80e75acb979473f06360bce0a5982a472f7d4f0`.
radare2 identifies it as little-endian LE, x86/80386, 32 bits, two objects,
124 pages of 4096 bytes. The LE OS field says OS/2; this is header metadata,
not evidence that the bound DOS game must run under OS/2. Object one starts
at virtual `0x10000`, object two at `0x80000`; entry EIP `0x501f8` in
object one therefore maps to virtual `0x601f8`. Stack object two has ESP
`0x2b300`. Data pages start at inner-file `0x25400`. For file-backed pages:

- Code original-file offset = virtual address + `0x4a6a4`.
- Data original-file offset = virtual address + `0x476a4`.
- Data from virtual `0x8f000` through `0xab300` is zero-filled and has no
  corresponding original-file bytes.

The LE header has a `0x226a6`-byte fixup section. Although `iI` summarizes
`relocs false`, applying fixups in the analysis cache produces relocated
data/code pointers and `RELOC 32` annotations. That summary is therefore
**disproven as evidence that this binary has no relocation information**.
Addresses below refer to the relocated virtual image, not file offsets.

**Confirmed first UI traces.** A broad static pass identifies 2,033 functions.
The assembly and successful focused r2ghidra output establish:

| Virtual routine / instruction | Native behavior |
|---|---|
| `0x4ccc4`, resource instruction `0x4ccdd` | Main Menu loads 3041, confirming REZDAT 41 |
| `0x4d1f0`, instruction `0x4d1f7` | New Campaign loads 3043, confirming REZDAT 43 |
| `0x4928d` | Game Menu resource immediate 3044 |
| `0x1755c`, instruction `0x175b4` | Scenario picker loads 3089, confirming REZDAT 89 |
| `0x58bec` | loads a resource via `0x5f7d0` / `0x5f788`, resolves root string resource `+0x3c`, then calls `0x58ad4` |
| `0x58ad4` | relocates sibling links `+0x00`, root child link `+0x40`, resolves string slot `+0x14`, installs handlers `+0x24` / `+0x28`, stores child's root pointer `+0x2c` |
| `0x59060` | runs the dialog event loop and returns the result from virtual global `0xa7e34` |

`0x58ad4` reads the **16-bit** kind at `+0x1c` (the catalog's upper
halfword is zero). Handler tables are at `0x595c0` and `0x5dd70`, indexed
by native kind. Positive picture IDs at `+0x10` go through `0x5b810`;
negative values refer to another record relative to the resource root and
set flag `0x20`. It sets root flag `0x04` and initialized-record flags
`0x41`. This confirms linked layout/string fields independently of the
archive-only inference. It does not yet prove all drawing/input flag meanings.

**Confirmed geometry mutation / unresolved effect.** At `0x58b03`,
`0x58b16`, `0x58b29`, `0x58b3c`, loading kinds 3, 4, 6, 7 respectively
sets bottom = top + global word `0xa7e42`, bottom = top + `0xa7e3a`,
bottom = top + `0xa7e40`, or right = left + `0xa7e4a`. The globals'
initialization and their relationship to explicit width/height remain
untraced. Do not use the serialized 38×38 scrollbar placeholder as final
retail geometry or introduce guessed constants for these values.

**Single Player path / confirmed additional archive.** Main Menu result 1 calls
`0x4cb8c`, then `0x4ce2c`. At `0x4ce31` the latter loads resource
**6007**, not a discovered REZDAT 33–90 record. Results 1/2/3 lead to
`0x4d1f0` (native race choice), `0x4a194`, and `0x123dc` / `0x16068`.
The resource lookup at `0x5fb24` selects an archive by resource number,
then indexes its offset table after subtracting that bank's native base.
Wargus's archive declarations identify bank 6000 as MUDDAT.CUD, bank 2000
as SNDDAT.WAR. Local headers confirm those types. MUDDAT entry 7 is the
actual SinglePlayer dialog, not a movie. This supersedes treating absence
from REZDAT as absence of the retail screen.

MUDDAT SHA-256 is
`e009678457408b593c5705e505b50dc2be518a7097999f2a8da98dcb6811f473`;
SNDDAT SHA-256 is
`a1015e38f45ac58578f2979164c603912c7ef501750e46e0c0389e4aa3dbddba`.
MUDDAT has 19 entries; SNDDAT has 49. Entry 7 at MUDDAT file offset
`0x11b9a8` is a raw 360-byte dialog (five 72-byte records), root
(0,0), 640×480, string resource **2047**, child IDs 1/2/3/-3, each
kind 2 and flag `0x0218`. Buttons are 224×28 at x208,
y240/276/312/348. SNDDAT entry 47's root string is `SinglePlayer`;
its four CP866 captions decode to «Новая кампания», «Прочитать игру»,
«Миссия пользователя», «Предыдущее меню». open-rts now loads these
records and captions directly, preserving native IDs. The engine's existing
navigation actions and keyboard bindings remain engine behavior; every
retail callback and key-binding rule has not been ported.

Entry 13 at MUDDAT file offset `0x11bb14` is a raw 4392-byte dialog,
61 records, root (0,0), 640×480, string resource **2048** (root name
`dialog`). Its localized title is «Установки сетевой игры». At
`0x15dd3` the executable loads resource 6013. The native control groups are:

| Controls | Native rectangle / flags |
|---|---|
| Text fields IDs 12–19 | x200, y48 + 24×row, 388×18; `0x0018` |
| Player label ID20 / dropdowns IDs21–27 | x36, first label y48 then dropdowns y68 + 24×row, 156×18; `0x8008` / `0x8018` |
| Checkboxes IDs36–42 | x10, y72 + 24×row, 18×20; `0x0018` |
| Scenario / Start / Cancel buttons IDs3/2/1 | x400, y368/404/440, 224×28; `0x0218` |
| Dropdown IDs10/7/9 | x44, y274/324/374, 124×24; `0x0018` |
| Dropdown IDs4/8/5 | x224, same y positions, 144×24; `0x0018` |
| Dropdown ID6 | (404,274), 180×24; `0x0018` |
| Scenario description ID11 | (8,424), 380×52; `0x8008` |
| Title / Ready captions, ID-1 | (160,8), 320×20 / (4,26), 480×14; `0x8808` / `0x8008` |
| Race / Fog / Cheats captions, ID-1 | x40, y256/306/356, 128×18; `0x8008` |
| Resources / Terrain / Placement captions, ID-1 | x220, same y positions, 160×18; `0x8008` |
| Units / Scenario captions, ID-1 | (400,256), 160×18 / (16,402), 106×18; `0x8008` |
| Runtime text IDs28–35 / 43–50 | x616 / x596, y48 + 24×row, 18×18; `0x0008` |

This adds two confirmed dialogs and 64 children to the earlier REZDAT
catalog: **60 dialogs, 700 child controls**. The adapter accepts resource
IDs in banks 3000/6000 and captions in banks 4000/2000. Strings decode on
first use so movie/music entries are not needlessly decoded as text. Both
MUDDAT dialogs pass native record/string tests; resource 6013's runtime
player and option bindings remain unported. Temporary env-gated C logging
printed all IDs, kinds, flags, rectangles and captions, then was removed.

**Calling convention / decompiler limitation.** At `0x175b4` / `0x175bf`
the scenario resource enters in EAX, the dialog argument in EDX and callback
address `0x17530` in EBX. `0x58bec` preserves EBX/EDX for the following
event-loop call. Register passing and preserved-register returns are
confirmed at these sites; a Watcom register convention is **inferred**, not
a proven compiler version. The stock decompiler labels routines
`__fastcall`, loses several call arguments, reports artificial 64-bit
`CONCAT44` returns and sometimes indexes halfword fields through int
pointers. These are analysis artifacts. Reconcile every proposed port
with exact instructions; the C-like dump is a discovery aid, not compilable
or authoritative source. Font/color selection and button baseline formulas
still require tracing the native draw-handler table.

**Reproduction / verification.** The original hash remained unchanged. Only
analysis copies and in-memory fixup caches were modified. Dumps remain in
ignored `reverse/war2-exe-r2ghidra/`, never committed:

```sh
mkdir -p reverse/war2-exe-r2ghidra
dd if=data/WAR2/WAR2.EXE of=reverse/war2-exe-r2ghidra/war2-inner.mz bs=217764 skip=1
rabin2 -I -S -e reverse/war2-exe-r2ghidra/war2-inner.mz
r2 -q -e scr.color=0 -e bin.cache=true -e bin.relocs.apply=true -A \
  -c 'afl' -c 'pdf @ 0x58ad4' -c q reverse/war2-exe-r2ghidra/war2-inner.mz
r2 -q -e scr.color=0 -e bin.cache=true -e bin.relocs.apply=true \
  -e r2ghidra.sleighhome=/private/tmp/war2-analysis-tools/prefix/lib/radare2/6.2.2/r2ghidra_sleigh \
  -A -c 'pdg @ 0x58ad4' -c q reverse/war2-exe-r2ghidra/war2-inner.mz
```

Use the disposable prefix's `bin` on PATH for these commands. Focused
decompilation of the loader, record initializer and Single Player dispatcher
completed successfully; no retail launch or runtime trace was involved.
The complete function dump also completed, with two decompiler failures
reporting unresolved instructions at `r0x80010101` and `r0x00000716`.
These are unresolved analysis boundaries, not valid game routine addresses;
the three focused UI routines decompiled without those errors. `make`, all
25 headless Warcraft II tests (with localhost UDP enabled), and the game
`--check` passed. The native Single Player BMP was rendered by the engine
and visually inspected after conversion to a temporary PNG; no runtime PNG
asset or loader was introduced. The restricted first menu test failed at
its localhost-hosting assertion, then passed with localhost networking
enabled; this was a test environment restriction rather than a menu defect.

## Native dialog resources and button text bounds (2026-10-07)

**Correction of the absence hypothesis.** The assertion that Warcraft II has
only UI pictures in WAR archives and all layout in the executable is
disproven for the installed DOS data. REZDAT entries 33–90 contain 58 linked
dialog resources, comprising 636 child controls. Wargus's extractor catalog
does not extract these entries, and its Lua menus construct their own
layouts. Absence from that extractor is not absence from the game data.
This finding supersedes earlier statements here that native scene records
had not been identified. It does not establish the Battle.net MPQ format.

**Provenance / investigation boundary.** The same WAR2.EXE and MAINDAT
fingerprints recorded below apply. REZDAT SHA-256 is
`d0fe7edd4f89f60c64bca786a944387578635d8483bc2ff165d06306bb432246`;
STRDAT SHA-256 is
`5ba75d38613852be7137c5ec4035578977eb75ce5adc37d219875ba308ce2c26`.
Only archive bytes and reference sources were examined in this correction;
the retail executable was neither launched nor disassembled. The user's
instruction prohibits further retail execution. C probes printed every
resource's size, record words, child kinds and dialog strings. The retained
`tests/warcraft-2/test_ui_scenes.c` reproduces the catalog and decoder checks.

**Confirmed serialized structure.** Every identified dialog is a multiple
of 72 (`0x48`) bytes. Record zero is kind zero; its child link is 72. Each
child's sibling link visits the next record, and the last is zero. Rectangles,
kind numbers, signed IDs, picture IDs and string references are stored in
these records, independently of executable callbacks.

| Record offset | Native value / observed role |
|---|---|
| `+0x00` | u32 next sibling offset, relative to resource start |
| `+0x04`, `+0x06` | u16 left/top; child coordinates are relative to window |
| `+0x08`, `+0x0a` | u16 right/bottom; do not reconstruct extents from these |
| `+0x0c`, `+0x0e` | u16 explicit width/height |
| `+0x10` | u32 picture resource; scenario window has 3012, matching REZDAT 12 |
| `+0x14` | u32 one-based string slot; root slot 1 is the internal dialog name |
| `+0x18`, `+0x1a` | u16 flags and signed 16-bit control ID |
| `+0x1c` | u32 native kind |
| `+0x30` | duplicate window extent in the root |
| `+0x3c` | root string resource; 4004 selects STRDAT 4, for example |
| `+0x40` | root first-child offset |

The remaining fields' complete runtime meanings are unknown. Most serialized
callback/state fields are zero. Some root right/bottom values are local
extents despite nonzero origins; using the explicit size avoids the wrong
width. Visibility bit `0x0008` and enable bit `0x0010` are working
interpretations consistent across the catalog, not executable-proven flags.
The adapter copies those bits; tests check that copy against raw records.

Kind interpretation, inferred from strings and geometry across dialogs:
1 default button, 2 button, 3 radio, 4 checkbox, 6 horizontal slider,
7 scrollbar companion, 8 text field, 9 left text, 10 centered text,
11 right text, 12 list, 13 dropdown. Type 5 does not occur in this data.
Radio grouping, slider ranges/current values, list rows, dropdown choices,
default activation, font/colour flags, palette switching and picture-resource
dispatch still require runtime configuration or static executable tracing.
Do not promote the likely font flags `0x0400` / `0x0800` to verified rules.

**Concrete native layouts.** Main Menu is REZDAT 41, WAR file offset
`0x003c2bb0`, decoded size 432. It references STRDAT 4 and contains five
224×28 buttons at x208, y240/276/312/348/384, with IDs 1/2/3/4/-3 and
string slots 2–6. NewCampaign is entry 43, file offset `0x003c2f18`, size
288: three buttons of that size at x208, y240/276/312, IDs 1/2/-3, STRDAT
6 slots 2–4. No four-button New Campaign/Load/Custom/Previous dialog was
identified among these 58 resources. Its absence here does not prove that
the executable never composes or changes a screen.

Game Menu entry 44 places its 256×288 window at (272,96), not (0,96).
Its controls retain local positions, e.g. Save (16,40), Options (16,74),
Help (16,108), Objectives (16,142), End (16,176), Return (16,248).
These correct both the previous horizontal placement and guessed row gaps.

Scenario entry 89 is at WAR file offset `0x003cd890`, decoded size 1080.
Its window is (144,64), 352×352, picture resource 3012, STRDAT resource
4062. Local controls include OK (-2) at (44,318), Cancel (-3) at
(188,318), both 106×28; list (1) at (22,122), 300×112; dropdowns
type (2) at (138,38), players (4) at (138,66), size (3) at (138,94),
all 204×24. The text in STRDAT 62 slots 6–8 identifies Type, Players,
Map. The middle control is therefore bound to player-count choices in
open-rts, rather than the earlier BNE-derived directory selector. The exact
retail filtering callback remains untraced. Custom directories and a parent
entry are engine filesystem behavior through the list.

The hidden kind-7 record has ID `0x8001`, flags zero, and a 38×38
placeholder rectangle. Our adapter builds its scrollbar from the list's
explicit rectangle and native GFU arrow dimensions. This is an inferred
composite, not confirmation of the retail construction formula. The BNE
JPEG places/orders controls differently and does not override DOS resource
geometry. In particular, the previous y50 panel shift, widened directory
dropdown, reordered OK/Cancel and combined detail label were authored
approximations; they are removed for the DOS picker.

**Complete discovered catalog.** Counts include the root. STR column is
the zero-based STRDAT entry; a dash means no serialized string resource.

| REZDAT | STR | Dialog | Origin; size | Records |
|---|---|---|---|---|
|33|0|TEXTBOX|176,464; 448×16|8|
|34|–|Top text|176,0; 448×16|4|
|35|43|INFOBOX|176,0; 448×16|6|
|36|–|Right text|624,0; 16×480|2|
|37|–|Unit info|0,160; 176×176|28|
|38|–|Command buttons|0,336; 176×144|10|
|39|2|STAT_F10_DLG|0,0; 176×24|5|
|40|3|Title|0,0; 640×480|2|
|41|4|MainMenu|0,0; 640×480|6|
|42|5|MultiPlayer|0,0; 640×480|6|
|43|6|NewCampaign|0,0; 640×480|4|
|44|7|GameMenu|272,96; 256×288|9|
|45|8|HelpMenu|272,96; 256×288|5|
|46|9|GameMenu|272,96; 256×288|9|
|47|10|Options|272,96; 256×288|6|
|48|11|SND_DLG|272,96; 256×288|20|
|49|12|Screen|272,96; 256×288|13|
|50|13|Speed|272,96; 256×288|16|
|51–56|14–19|Quit / Restart|272,96; 256×288|6 each|
|57|20|Win_Mission|256,176; 288×128|5|
|58|21|Lose_Mission|256,176; 288×128|4|
|59–62|22–25|Mission_Stats|0,0; 640×480|31/47/63/78|
|63|26|Savegame|208,112; 384×256|8|
|64|27|Loadgame|208,112; 384×256|6|
|65|28|Loadgame|0,0; 640×480|6|
|66|29|Ally_Filters|272,132; 256×224|19|
|67|30|Message_Filters|272,132; 256×224|20|
|68|31|Ok_Cancel_Dialog|256,176; 288×128|4|
|69|32|BigOK|192,88; 288×256|3|
|70|33|Ok_Cancel_Dialog|256,176; 288×128|3|
|71|34|Cancel_Dialog|256,176; 288×128|3|
|72|35|DirectLink|0,0; 640×480|13|
|73|36|Modem|0,0; 640×480|16|
|74|37|ModemConfig|0,0; 640×480|14|
|75|38|Viewgame|0,0; 640×480|10|
|76|39|dialog|0,0; 640×480|3|
|77|41|Tips_Dialog|256,112; 288×256|6|
|78|48|Connect|176,176; 288×128|6|
|79|49|CMsg|144,64; 352×352|5|
|80|44|Custom setup|0,0; 640×480|51|
|81|52|Objectives|272,96; 256×288|5|
|82–83|54–55|Human / Orc_Dispatch|0,0; 640×480|6 each|
|84|56|Credits|0,0; 640×480|3|
|85|46|dialog|192,96; 256×288|12|
|86|47|HelpScreen|224,64; 352×352|18|
|87|51|Game_Name_Dialog|256,176; 288×128|5|
|88|–|Two centered texts|0,0; 640×480|3|
|89|62|Scenario picker|144,64; 352×352|15|
|90|–|Text|0,0; 640×480|2|

Dispatch 82/83 also stores Continue at (456,448), 106×28; prose area
(72,80), 320×200; objectives (372,330), 252×108; level title
(12,28), 480×20; Objectives caption (372,306), 252×18. The serialized
background resource is zero; its runtime assignment/narration is unknown.

**Button centering.** FONT 281 has a 17-pixel canvas. `M`'s descriptor is
14×14 at (0,0), but its RLE emits only 12 occupied rows; the last two
descriptor rows are skipped. `p` has descriptor height 13 at y4 and emits
12 rows. The previous loader gave every glyph the full 17-pixel bounds,
which placed button text above its visual center. The intermediate
hypothesis “descriptor height equals occupied height” was also disproven.
Temporary `OPEN_RTS_DEBUG_W2_ALIGN` logging printed descriptor and RLE span
values; it was removed after verification.

The decoder now records vertical bounds while consuming native RLE commands,
without scanning pixels or introducing an offset. Shared `V_TextBounds`
unions those bounds across the actual caption, including native displacement
and newlines. Shared menu vertical/bottom alignment uses that result;
top-aligned text keeps its native origin, and authored pressed shifts follow
alignment. This is the user's requested centering behavior; the retail
text-placement formula is not executable-proven. A native `M` in a 28-high
button occupies rows 8–19, leaving eight rows above and below.

Decoded font pixels, cell rectangles, advances and palettes are unchanged.
An FNV byte comparison against the pre-change loader gave matching hashes
`7e61c028e244c6ad` (281) and `e6492cfdf472397b` (282), each with 209
cells. Hash input: glyph indices, advances, source palette, each cell
rectangle and its indexed pixel storage; seed 1469598103934665603, multiplier
1099511628211. Bounds metadata is intentionally excluded because that is
the corrected value. A distinct-background pixel test disproved the concern
that black shadows were merely hidden by a black test background.

**Implementation and remaining fidelity.** `w_scene.c` validates record
spans/links before decoding directly into the final engine item table. It
retains native IDs, geometry and label slots; no retained parallel scene
graph or new per-game input/drawing loop is introduced. Main, race choice,
in-game menu and picker consume these records. `MI_SLIDER` adds the missing
shared control with bounded drag/key input and authored track/thumb art.
All present native kinds have an engine representation. Loading all records
is not a pixel-identity certificate for all scenes, and Wargus rendering its
own Lua menus cannot supply one.

**Correction.** The sentence that left the other front-end screens authored
is superseded by “Retail dialog screens” below. Font and colour flags, the
default-button rim, and the checkbox, radio, slider and field drawers are
traced there. Single Player, setup, options, save/load, briefings, credits
and the connection list now open their own REZDAT or MUDDAT records.

Reproduce with `make`, `env SDL_VIDEODRIVER=dummy make test-warcraft-2`
(local UDP permitted for network-menu tests), and `env SDL_VIDEODRIVER=dummy
W2_MENU_SHOTS=/private/tmp build/bin/tests/warcraft-2/test_menu`. The native
catalog test checks all 58 imports plus malformed lengths, links and
capacity; shared tests exercise slider endpoints, dragging, keyboard input,
disabled input, authored drawing and text alignment.

Verification at this checkpoint: `make` and all 25 Warcraft II regression
executables pass without new warnings, as do shared menu-item tests for
Dark Reign, Dark Colony, 7th Legion and KKnD. Open-rts `--check` passes.
The imported picker was rendered at 640×480 and 1280×960 and inspected,
including custom/built-in filters and directory navigation. These are
engine/data-contract checks, not comparison against an executed retail game.

## Forest borders after harvesting (2026-10-07)

**Confirmed engine defect:** the user screenshot `Screenshot 2026-10-07 at
09.41.45.jpg` shows a square stump tile inside unchanged winter tree art.
This is an open-rts screenshot, not evidence from a retail executable.
`p_harvest.c::exhaust` replaced only the harvested cell with megatile 126.
A temporary diagnostic on a solid forest fixture printed unchanged `0070`
in all eight neighboring cells after the center became runtime slot `09e0`.

**Confirmed reference behavior:** Wargus defines forest solid PUD slots
`0x70..0x7f`, mixed slots `0x700..0x7df`, and special graphic tiles 121
(top of a narrow tree), 122 (middle), 123 (bottom), 126 (removed tree).
Its Stratagus engine's `CMap::ClearWoodTile` clears the harvested field and
calls `FixNeighbors`. That routine visits E, W, S, N, NW, SW, NE, SE in
order, so diagonal corrections see the updated cardinal borders.
`FixTile` selects art with `CTileset::getTileBySurrounding`; a field with no
valid tree shape loses forest/blocking flags and its resource value.

The corner mask uses SW=1, SE=2, NE=4, NW=8. Forest/ground group masks in
ascending PUD group order are `{8,4,12,1,9,5,13,2,10,6,14,3,11,7}`; solid
forest has mask 15. The current tile's NW corner requires the north tile's
SW and west tile's NE; the other corners use the corresponding touching
pairs. Off-map neighbors supply mask 15. Narrow top/middle/bottom trees
have masks `3|32`, `15|32|16`, `12|16`. A bottom tree below or top tree above
can connect to adjoining side corners. With no corner intersections, tree
art above/below selects a narrow bottom/top/middle, or neither clears the
fragment. These are reference-derived rules, not pixel-shape guesses.

**Native data:** the existing MAINDAT fingerprint below applies; no
WAR2.EXE instructions were examined. All four supported era decoders expose
the mixed forest groups and special megatiles above. The native 42-byte
mapping records continue to choose each era's art; the new runtime slots
`0x9e1..0x9e3` expose megatiles 121..123 which have no ordinary PUD slots.
`0x9e0` remains the removed-tree slot. No generated tile pictures or inferred
pixel masks are used.

**Warcraft 2000 comparison:** `Nature.cpp::TakeResource` calls
`CreateSurface` on the four surfaces sharing a depleted resource corner.
`CreateSurface` forms a four-bit mask from `ramap`, looks up
`RDS[rk].Tiles[mask][variant]`, unlocks empty ground and calls `ClearRender`
when its tile changes. This corroborates rebuilding neighboring surfaces,
but its corner-resource grid and random tile variants are not Warcraft II's
tile-resource model and were not adopted.

**Implementation and checks:** `w2_remove_tree` updates the eight neighbors
using their current forest corners. Cleared fragments also deactivate and
zero their lumber deposits and clear terrain blocking (preserving separate
solid blockers). Valid edge tiles retain their remaining lumber. The
harvesting tests now use an actual two-column grove: a bottom cut also
removes its unsupported top; a neighboring column remains harvestable.
The existing navigation snapshot detects the changed blocked grid.

`test_forest` fails before the fix and checks the exact eight replacement
slots, unchanged distant tiles, narrow columns, repeated cuts, resource
deactivation, all four map corners, and native tile lookup in every era.
Reproduce a winter old/new BMP (single-cell replacement on the left,
corrected neighbors on the right) with:

```
make build/bin/tests/warcraft-2/test_forest
env SDL_VIDEODRIVER=dummy build/bin/tests/warcraft-2/test_forest /private/tmp/w2-forest.bmp
```

Verification: `make`, the forest and harvest tests, headless Warcraft II
`--check`, and ALAMO screenshot passed. The native winter comparison was
visually inspected: the square cut becomes a continuous forest boundary.
23/25 Warcraft II test binaries passed; the same pre-existing `test_menu`
and `test_net_menu` failures recorded above remain.

**Disproven:** changing only the harvested tile, or basing the replacement
solely on neighboring forest occupancy, suffices; corner shape matters.
**Unknown:** the exact retail WAR2.EXE update order, variant selection and
fragment-resource accounting. The implementation follows the named reference
engine, without claiming an executable trace or retail pixel equivalence.

## Vehicle movement frames (2026-10-07)

The MAINDAT and WAR2.EXE fingerprints recorded below still apply. This
investigation decoded native GRPs and compared the pinned Wargus and Warcraft
2000 sources; WAR2.EXE was not disassembled or run.

**Confirmed reference behavior:** Wargus `scripts/human/anim.lua` and
`scripts/orc/anim.lua` select these `Move` pictures. Divide Wargus frame
operands by five to obtain our logical frame (five facings per row):

| Units | Wargus Move operands | Logical movement frames |
|---|---|---|
| Ballista, catapult | 0, 5 | 0, 1 |
| Both tankers, transports, destroyers, battleship, juggernaught | 0 | 0 |
| Gnomish submarine, giant turtle | 0 | 0 |
| Flying machine (`animations-balloon`) | 5, 0 | 0, 1, repeating |
| Zeppelin, Eye of Kilrogg (`animations-eye-of-vision`) | 0 | 0 |

Siege movement uses the same poses in both EnhancedEffects branches; the
enhanced branch adds a turn wait. Surface ships use operands 5 and 10 for
**death**, while submarines/turtles use those operands for **attack**.
Ballista/catapult attack uses operands 10 and 15. None belongs in movement.

**Confirmed defect:** `w2_limit_walk` assumed frame 1 followed by
`min(phases - 1, 4)` pictures was a walk cycle. Temporary diagnostics on the
native ALAMO asset load printed `ballista: phases=4 frame=1 count=3`.
Thus a rolling siege unit showed rows 1, 2, 3 (including firing art) and a
three-row ship showed rows 1, 2 (sinking/attack art) instead of row 0.
Before loading art, every mobile type incorrectly had four frames starting
at 1. **Disproven:** GRP row count determines movement sequence semantics.
Native examples are MAINDAT entries 49/50 (ballista/catapult), 59 (human
tanker), and 41 (battleship). The five-facing decode itself was unchanged.

**Implementation:** `w2_build_states` authors the vehicle movement ranges
above both at initialization and after GRP loading. Single-pose vehicles
retain a looping movement state with `A_Chase`, separate from standing.
Native frame counts bound ranges but no longer define vehicle animation.
Infantry retains its existing rows 1–4. The flying machine uses a cyclic
0/1 sequence; Wargus starts at 1 and has uneven waits within its move script.

**Warcraft 2000 comparison:** `Nation.cpp::OneObject::LoadAnimation` selects
`MoreAnimation` by `WhatFor` and `Kind`; land movement in `Nation.cpp` and
water movement in `Water.cpp` call `LoadAnimation(1, AnmGoKind, 0)`.
`LoadCurAnm` then selects one of five directions and mirrors the other
three. This confirms purpose-specific movement selection in that reference,
but its different asset format does not establish Warcraft II frame numbers.

**Remaining limits:** exact retail movement timing and speed conversion are
unknown. This correction preserves the existing four-tic presentation cadence
and movement speed; it does not reproduce Wargus's move/wait script timing,
enhanced turning pauses, or bobbing. The existing combat builder also groups
three-row surface ships with submarines and four-row scouts with siege;
those attack/death classifications remain a separate known limitation.

**Reproduce:** `env SDL_VIDEODRIVER=dummy make test-warcraft-2` includes
`test_movement`: 17 types, three complete state cycles and return to stand,
before and after native GRP loading, plus eight-direction availability for
every selected movement row. The new test failed on the old frame selection.
Verification: `make`, the movement regression, headless `--check`, and an
ALAMO world screenshot passed. Of 24 Warcraft II test binaries, 22 passed;
`test_menu` (`find(S(38, 5))`) and `test_net_menu` (English LAN labels)
failed identically when relinked with the original pre-fix state builder.

## Button decorations and footprint selection correction (2026-10-04)

The MAINDAT and WAR2.EXE fingerprints below still apply. WAR2.EXE was not
disassembled in this correction. REZDAT.WAR SHA-256:
`d0fe7edd4f89f60c64bca786a944387578635d8483bc2ff165d06306bb432246`.
The three supplied 640×480 JPEGs are recorded in `REFERENCES.md`; their
distribution/version is unknown and JPEG compression prevents exact palette
identification from their pixels.

**Confirmed native records:** MAINDAT GFUs 354/355 contain four full
176×176 frames at origin (0,0). Frame 0 starts at decoded entry offset 38
and is plain stone. Frames 1/2 alias offset 31014 and carry the surrounding
info-panel rim. Frame 3 at offset 61990 also contains the progress-bar
surround. The previous first-frame-only plate decoder discarded these
authored decorations. Decode all frames with the existing GFU decoder;
choose 0 for empty/group selection, 1 for single selection, and 3 for
training. The separate max-box composition is unnecessary for these full
native frames and has been removed.

REZDAT human/orc GFUs 0/1 contain the blue/red 128×20 thin button in frames
4/5 (normal/pressed), at decoded offsets 6342/8902. Palette 14 supplies
their colors. The supplied screenshots place the Menu control at (24,2);
the MAINDAT 293/294 stone backing is a separate 176×24 image. Previously
we drew only that backing and the text. Draw the REZDAT button on it, with
the existing indexed palette remapping and shared menu state dispatch.

**Confirmed screenshot geometry:** command icons occupy 46×38 rectangles
at the existing (9/65/121,340/387/434) positions. A bright one-pixel rim
lies one pixel outside the icon: the first command's upper-left rim is
(8,339). The surrounding dark edge adds another pixel, yielding a 50×42
decoration extent at (7,338). Group/single portraits and the training
icon use the same surround. The screenshots show a green active-command
rim. open-rts draws these rims in shared `hud/m_menu.c` from table fields,
including focus, press, and a command waiting for a world target. It uses
native-palette black/white/green; exact pre-compression RGB values and
retail hover/press dispatch remain unknown. All icon pixels remain native
GRP pixels, and native Menu pressed artwork is used directly.

**Disproven sprite placement:** the former GRP pivot `(width/2,height)`
shifted each sprite above the simulation footprint by half its native
canvas height. A 72×72 worker's selection then appeared below its body.
Stratagus `src/unit/unittype.cpp::DrawUnitType` centers its sprite canvas
on the occupied tiles: subtract `(sprite_size - tile_size)/2` from the
tile origin. With our object already stored at the footprint center,
the equivalent pivot is `(width/2,height/2)`. This is a reference-derived
placement correction, checked against the supplied centered building
selection; it is not a WAR2.EXE instruction trace. Native canvas and
frame pixel data, bearings, and team remaps are unchanged. Carrier GRPs
use the same loader and pivot rule.

**Explicit user presentation contract:** draw selection before all world
sprites, using `mobjinfo[type].w2.footprint * app.cell`, centered on the
object's planar position. Do not use the GRP canvas, opaque bounds, or
the reference `BoxSize`. Thus a Ballista still selects 1×1 tiles despite
its 63×63 reference box; ships select 2×2, barracks 3×3, and halls 4×4.
This overrides any reference selection-size differences. The shared
renderer runs the existing overlay callback family in a ground pass before
world sprites; Warcraft II supplies the native footprint rule there.
Sprite rectangles and floating health bars are suppressed for this game,
matching the supplied HUD-only health presentation. Hidden/invisible
objects do not draw marks. The global thinker/object ownership is unchanged;
DOOM `r_things.c` was consulted for its sorted sprite drawing separation.

Reproduction (headless):

```sh
env SDL_VIDEODRIVER=dummy make test-warcraft-2
env SDL_VIDEODRIVER=dummy build/bin/tests/warcraft-2/test_hud /private/tmp/w2-human.bmp /private/tmp/w2-orc.bmp
env SDL_VIDEODRIVER=dummy build/bin/tests/warcraft-2/test_selection
```

HUD tests cover both races, native menu states, all four info frames,
single/group portraits, and training decorations. The selection regression
independently checks 1×1/2×2/3×3/4×4 extents, camera/cell scaling, hidden
objects, and sprite occlusion through both world draw entry points. The
PUD test checks the native Footman's 72×72 canvas pivot at (36,36).

This investigation uses the retail DOS data in `data/WAR2`. It does **not**
claim an instruction trace of WAR2.EXE or a complete pixel comparison against
a running retail game. The executable is DOS4GW/LE; no r2 installation or
retail runtime was available during this work. Function addresses, movement
conversion, exact gathering delays and the original minimap renderer remain
unknown. See `REFERENCES.md` for source URLs and pinned reference versions.

## Inputs and fingerprints

- `DATA/MAINDAT.WAR`: 10,403,193 bytes, SHA-256
  `791bae4480d564f017122a82c9481dabd952424151f2b5d20245793e654ad3bb`.
- `WAR2.EXE`: 878,119 bytes, SHA-256
  `a2b4b2118ec6355371b58134be8c7331d1facc5989e7188a1d1bb68fd1f26671`.
- `reference/wargus`, commit
  `cde1a0718a0058cc651ecd56ff8149fc39f624e9`: format and behavior reference,
  not copied source. `reference/DOOM/p_tick.c` supplies the shared thinker
  ownership and deferred-removal model already used by Dark Colony.
- `reference/warcraft2000`, commit
  `4d12ad3e62ba03c59b2dbec2a989f58d744018ee`: gathering and interface
  comparison only. Its formats, resource multipliers and HUD are not DOS
  Warcraft II rules.

## Resource records and worker behavior

**Confirmed native records.** PUD `UNIT` records are eight bytes: x/y at
0/2, type/player at 4/5, data at 6. Gold mine type 92 becomes actor 93.
`ALAMO.PUD` has 11 mines and 2,787 forest cells (`SQM & 0x0080`). The
loader now creates a level-owned deposit for each. Mine reserves use
`UNIT.data * 2500`, independently checked against `wargus/pud.cpp`.
`test_pud` matches every mine back to its PUD coordinate and reserve.
The format interpretation is supported by Wargus, not a WAR2.EXE trace.

**Confirmed published behavior.** Blizzard's Peasant/Peon guide specifies
51 chops per tree, with progress belonging to each worker. The first worker
to finish takes the wood; a competing worker loses its progress and starts
another tree. The guide's worker stats are HP 30, damage 1–5, armor 0, sight
4, speed 10 and range 1. The general resource guide specifies 100 gold per
trip and increased yield after hall upgrades. These are authored C values;
neither unit stats nor timing are loaded from Lua or retail balance tables.

**Reference-derived implementation.** One tree contributes 100 wood.
Workers finish an axe state chain 51 times, carry one load, enter the nearest
reachable owned hall or (wood only) lumber mill, then resume gathering.
Reserves decrease at pickup; the player's stock increases at delivery.
Gold and wood cargo remain typed when retargeting. Manual Return Goods,
Stop, Move, routes and context clicks use the shared tic-command pipeline.
No owner-15 mine is treated as an enemy attack target when the selected
worker can harvest it. Invalid moves preserve the current gathering state.

Mine/depot entry waits are 150 state tics, from Wargus worker `units.lua`.
The axe chain uses logical frames `{5,6,7,8,9,5,5}` and tics
`{3,3,3,5,3,7,1}`, from its worker animations. **Unknown:** these delays
have not been traced in the DOS executable; our engine runs at its shared
tic rate. Wargus animation numbers alone do not prove retail elapsed time.

Wargus `ImproveProduction` describes player income improvements: gold
10/20 from Keep/Stronghold or Castle/Fortress, wood 25 from a lumber mill,
oil 25 from a refinery. We use the greatest living owned improvement,
never sum duplicate buildings. It applies when unloading at any eligible
depot and disappears when the improving building is removed. **Inferred:**
bonus credit and rounding are derived from the reference model; the exact
retail pickup/depletion accounting is untraced. Oil extraction is not
implemented by this change. Missing or removed depots preserve cargo and
workers retry when an owned reachable depot becomes available.

**Confirmed GRP metadata / corrected hypothesis.** Entries 124/122 are
human gold/wood carriers; 125/123 are orc gold/wood carriers. All four
native entries decode to **65 records / 13 temporal frames**, with five
authored facings mirrored into eight directions. They are not 25-record
strips: wartool.h's trailing `25` describes its repair-frame combination,
not the native GRP header count. We retain every record and use the first
five temporal frames for stationary/carried walking poses. `test_pud`
checks all four counts and direction definitions.

Wargus tileset tables designate megatile 126 as `removed-tree` in summer,
winter, wasteland and swamp. Our tileset lookup adds one internal slot
pointing to that native megatile; felling a tree clears its forest collision
and uses the stump. **Unknown:** retail neighboring forest-edge updates
are not reproduced. Exhausted mines release their footprint and use normal
deferred mobj removal; native collapse/rubble animation is unimplemented.

## Warcraft 2000 comparison

**Confirmed reference behavior, not retail Warcraft II evidence.**
`Nature.cpp:203` (`TakeResource`) depletes cells and rebuilds neighboring
surfaces; `TakeResFromCell` at 229 visits a 2×2 group of native cells.
`FindNearestBase` at 446 filters by country and accepted resource and uses
Manhattan distance. `TakeResLink` at 492–611 switches between gathering and
return phases, uses work animation 2 and the resource's portion capacity,
retries a missing/dead base, and credits
`RAmount * RDS.Multi + RESADD[owner][rtype]` plus taxation before resetting
cargo. This supports typed loads, owned depots and recoverable delivery,
but its distances, capacities and numeric rules were not adopted.

**Rejected reference shortcuts.** `OneObject::TakeResource` at 466 changes
the resource type even with an existing load; its rejection is commented
out. We preserve the old load's type on retargeting instead. `Build.cpp:42`
withdraws resources before checking building placement; a failed placement
can therefore lose resources. Our training commands check both gold and
wood before either is withdrawn. Construction still needs the separate
tic-command correction listed below.

`Interface.cpp:1525` (`GSSetup800`) uses a 160-pixel column and 48×40 icons,
with the map starting at (160,0). `Nature.cpp:1017` (`ShowRMap`) displays
three text resource rows. These are disproven candidates for the native
DOS Warcraft II HUD, whose assets use 176 pixels and 46×38 icons.
`mapa.cpp:827` (`CreateMiniMap`) samples native 32×32 terrain at byte 33
(pixel 1,1), reducing 2×2 cells; its marker at 924 uses yellow brackets
and health. Only the native-pixel sampling idea applies here; our sampling
and green owned markers use the Wargus/Blizzard references below.

## Native HUD loading and layout

**Confirmed native assets.** MAINDAT entries 293/294 (menu), 295/296
(minimap frame), 297/298 (command panel), 287/288 (resource strip), 291/292
(status strip), 289/290 (right filler), 354/355 (info GFU) provide the human
and orc chrome. Existing 640×480 geometry uses a 176-pixel left column,
16-pixel top/bottom strips and a 16-pixel right filler. Viewport:
`{176,16,448,448}`. GFU 187 supplies native 14×14 resource icons; command
and portrait icons come from native GRP 356/357/358. No generated or PNG
art is bundled. The HUD test compares more than 10,000 untouched panel
pixels per race and every nontransparent gold/wood/oil icon pixel against
the decoded native entries. This proves asset use, not full retail layout.

FONT 282 is the game font; 283 is the smaller HP font. A native FONT pixel
stores an ink category `control & 7`; transparency comes from skipped
pixels, not ink category zero. We store categories as indices 1–8 so the
engine's transparent index zero remains separate. Native glyph x/y
bearings are preserved; advance is `xoff + width`. For `M`, width 10 and
xoff 1 give advance 11. **Disproven:** dropping the x bearing and treating
category zero as transparent loses native spacing and dark/shadow ink.

**Reference-derived color/layout.** Wargus `scripts/fonts.lua` maps white
ink categories to `{239,246,246,246,104,239,239,239}`, yellow to
`{246,200,199,197,192,239,104,239}` in the native palette. Game counters
and stats use white, Menu uses yellow. The old synthetic grayscale/gold
tint was not native. These mappings are tested against the retail palette;
the executable's ink dispatch is untraced.

`scripts/human/ui_pandora.lua` and `scripts/ui.lua` supply reference
coordinates for the resource icons (x 176/251/326), portrait (9,169),
centered name (114,171), 50×7 HP bar (8,211), small HP text (35,221),
three command columns (9/65/121) and rows (340/387/434), multiple selected
portraits, training icon (110,241) and progress bar (12,313,152,14).
Larger windows anchor/stretch the existing chrome via the shared menu
table, including a tested width anchor in `hud/m_menu.c`.
Stratagus `src/ui/contenttype.cpp:60` resolves `~|` as a prefix-width
anchor: the text starts at `Pos.x - font.Width(prefix)`. Stat labels end
at x 100 before their colon, production at 85, and `Level ` at 154.
**Disproven:** placing every stat label at x 60 loses that alignment.

Armor and minimum/maximum damage text now comes from the C unit catalog
rather than displaying armor zero and minimum damage one for every unit.
Blizzard's published stats override mismatches in the reference, notably
Footman 2–9/armor 2, Catapult/Ballista 25–80/armor 0, Destroyer 2–35,
Battleship 50–130 and Submarine 10–50. Remaining catalog values and hero
labels are reference-derived, not an executable table extraction.

**Reference-derived minimap.** Stratagus `src/map/minimap.cpp`,
`GetTileGraphicPixel`, samples native terrain at x `7 + phase * 8`,
y `6 + phase * 8`, with scale precision 100. The HUD borrows the driver's
tileset and applies these samples rather than selecting artificial colors
for terrain categories. Unexplored cells are black. `DrawUnitOn` supplies
native footprint scaling, a one-pixel inset, green owned markers and bright
player colors; mines are yellow. Tests compare terrain samples and every
pixel of a single worker footprint independently of the world rendering.
Blizzard's DOS manual also explicitly identifies owned minimap units and
buildings as green. It lists gold, lumber, oil and food in the resource
bar; the previous synthetic score zero was removed. Wargus `widgets.lua`
uses 46×38 command icons with a black border and the same artwork through
hover/click. The supplied retail screenshots now supersede that reference
for icon rims; see the correction below.

**Still unverified / incomplete 1:1 claim.** The original executable's
minimap sampling and viewport marker remain untraced. Food alignment,
hover/pressed command treatment, exact stat text pixels, health color
thresholds and world selection/cursor overlays have not been compared
pixel-for-pixel against this DOS retail build. Wargus layouts can contain
enhancements. The current HUD uses native artwork and reference placement,
but a complete 1:1 retail match must not be claimed from these tests.

## Code review against Dark Colony

| Finding | Result |
| --- | --- |
| Neutral mines had no runtime resource reserve; forests were only collision | Added level-owned deposits, ordinary worker thinker state chains, depletion and delivery tests. |
| Right-clicking a mine dispatched Attack because its owner is 15 | Context picking now recognizes accessible resource sources; native HUD test exercises an actual right mouse event and tic command. |
| Lumber was spent in a HUD callback, while gold was spent through production | Both costs are checked and withdrawn when the training tic command executes; insufficient gold or wood changes neither stock. |
| Warcraft hid its state/type tables behind static game-prefixed storage | Uses `states`, `mobjinfo`, `sprnames` and shared stable mobj ownership, as Dark Colony/Doom do. Carrier definitions are built from native GRPs by the loader. |
| Empty `VER`/`ERA` sections could read payload byte zero without a span | Both now require nonempty payloads. |
| Building placement still directly spawns/spends from the HUD | **Remaining P1:** move construction into deterministic tic commands before Warcraft multiplayer is supported. Dark Colony queues simulation work separately from HUD input. |
| Combat flags/actions and campaign AI remain absent | Fighters carry `MF_ATTACK` and attack/death state rows built from the retail GRP phase layout with the Wargus waits; damage is Stratagus' `CalculateDamageStats` roll with research applied; range is Warcraft's tile distance to the target footprint (shared `mobjtype_t.footprint`); destroyed buildings free their cells. Research (sword/axe, arrow/throwing axe, shield lines) and hall upgrades (keep/castle, stronghold/fortress) run through the shared production queue with the pinned Wargus costs, times and dependencies. `test_combat`, `test_research`, the shared retaliation suite and HUD button checks cover it. **Remaining P1:** no skirmish AI features; AI shared suites stay excluded. |
| Patrol, stand ground, repair, save/load/options are visible placeholders | **Remaining P2:** no retail behavior is claimed; Dark Colony has real native menu actions and mission state. |
| Exact retail delays, minimap and several HUD states lack original-runtime evidence | **Remaining P2:** native asset tests and a Wargus layout are insufficient to certify complete 1:1 fidelity. |

## Reproduction

```sh
make
env SDL_VIDEODRIVER=dummy make test-warcraft-2
env SDL_VIDEODRIVER=dummy make test-dark-colony test-layout test-network
env SDL_VIDEODRIVER=dummy build/bin/warcraft-2 --check
env SDL_VIDEODRIVER=dummy build/bin/tests/warcraft-2/test_hud \
    /private/tmp/w2-human-hud.bmp /private/tmp/w2-orc-hud.bmp
```

Harvest regressions cover both races, partial/depleted gold, 51-chop wood,
stumps, repeat gathering, cargo retargeting, orders, invalid moves, missing
or destroyed depots, tree competition, multiple miners, enclosed deposits,
player-wide bonuses and atomic
training costs. The Warcraft target also runs shared facing, speed,
image, menu-item, navigation, retaliation, video and waypoint tests. Tests
that require Warcraft skirmish AI or the engine fallback menu are excluded
explicitly.
LAN tests require permission to bind local sockets in restricted runners.

## Complete base unit catalog audit (2026-10-04)

The WAR2.EXE and MAINDAT fingerprints at the start of this report still
apply. This audit read reference definitions, not executable instructions;
WAR2.EXE behavior remains untraced. Wargus is pinned to
`cde1a0718a0058cc651ecd56ff8149fc39f624e9`; Warcraft 2000 to
`4d12ad3e62ba03c59b2dbec2a989f58d744018ee`.

**Confirmed in reference:** Wargus `pud.cpp::UnitScriptNames` enumerates
105 PUD slots, 100 defined and five empty (native 34, 36, 37, 48, 54).
Every defined entry resolves in `scripts/human/units.lua`,
`scripts/orc/units.lua` or `scripts/units.lua`. This covers both races'
regular and upgraded units, all named heroes, summons, naval and flying
units, buildings, tower variants, neutral resources, special structures,
start markers and walls. Extra Wargus corpse/dead-vision/super-unit types
are not PUD slots and were not added to the native roster.

`info.h` now names every slot. `info.c::mobjinfo[]` is the sole authored
balance catalog, replacing the separate small `w_units.c` table and its
partially populated runtime copy. Numeric and capability fields include:

- HP, armor, raw speed, basic/piercing and displayed damage range;
  sight, attack minimum/maximum and person/computer reaction ranges.
- Footprint/box dimensions; gold, lumber, oil and time costs; repair HP,
  cost, range and automatic range; food supply/demand; mana maximum,
  initial value and regeneration; points, priority, AI annoyance and level.
- Decay, transport capacity, movement domain, targets, storage and resource
  provision, income improvements and each resource's capacity/step/waits
  and gathering flags.
- Organic/undead/hero/volatile, cloak/detection, indestructible, coward,
  ground/side attacks, selection/fog, shore construction, builder-outside,
  elevated, attack, neutral, teleporter and harvestable flags; projectile
  and spell identifiers. The native GRP lookup remains with the definition.

HP and maximum damage remain in Doom's `spawnhealth` and `damage`; they
are not duplicated in `w2_stats_t`. Existing ActorType projections and
state/rotation views are derived from this catalog. Zero-HP resource
markers preserve authored zero although the current engine projects HP 1.
The engine's speed divisor and sight clamp remain presentation choices.
The HUD reads base levels, food and prices from the table; training reads
`Costs.time` as seconds rather than deriving a duration from gold cost.
**Superseded by the 2026-10-07 audit:** that seconds conversion was incorrect;
the pinned reference uses six cycles per cost unit, now implemented below.
Gathering reads capacity and entry waits; depot choice reads storage masks;
income reads living owned structures' catalog improvements.

**Corrections and explicit differences:**

- Wargus gives Ballista armor 5. Blizzard's Catapult/Ballista guide gives
  armor 0; the authored table preserves 0. Published damage minima already
  in the catalog remain the HUD values; they are not guessed from a random
  damage formula or claimed to have been traced in the executable.
- `scripts/spells.lua::DefineVariables` defaults Level to 1 and Mana to
  `{Max=255, Value=84, Increase=1}`. Paladin, Ranger and Berserker set Level
  2. Ogre Mage uses a variable table with `Value=2`, rather than a scalar;
  treating that table as a number would incorrectly import 0. All are now
  parsed and authored correctly.
- Blizzard's Mage and Death Knight guides give initial mana 85, maximum
  255. Enabled mana uses 255/85/1 rather than Wargus's 84; applying the
  initial-value override to other enabled casters is an inference from
  the shared variable. Spell identifiers are metadata, not spell execution.
- Wargus Critter has BasicDamage 80, Demand 1, a critter-explosion missile,
  random movement and click-to-explode behavior. The existing engine choice
  keeps damage/demand 0 and leaves that special behavior unimplemented.
  The reference projectile and targeting metadata are preserved.
- **Disproven assumption:** a land-looking Daemon sprite does not establish
  movement domain. Wargus explicitly specifies `Type="fly"`, `AirUnit=true`;
  the old unsupported land classification is replaced with that reference
  rule. Retail dispatch remains unknown.
- Walls are buildings even though this engine skips wall object spawning.
  Numeric `Indestructible=1` is equivalent to `true` in the reference:
  oil patches, circle of power and both start markers now retain that flag.
  Domain is separate from `SeaUnit`: naval resource structures can have
  `Type="naval"` without being ship units.
- Deathwing has complete reference stats (HP 800) but its native GRP lookup
  is still unknown, `{0,0,0,0}`. No Dragon graphic is silently substituted.

**Warcraft 2000 comparison:** `MapDiscr.h::GeneralObject` groups capability
flags, `cost`, `delay`, `capMagic`, `ResourceID[]` and `ResAmount[]`;
`Visuals` adds life, shield, damage and productivity. This supports storing
related rules together, but Warcraft 2000's numerical rules and object
storage are not Warcraft II data and were not imported. Doom remains the
object/state ownership reference.

**Verification:** the C `test_catalog` independently parses the pinned PUD
mapping and balanced unit tables, then compares every defined type's base
numeric fields, dimensions, resources, capabilities, projectile and spell
list, with the explicit overrides above. No reference is loaded by the
game. Without the ignored reference checkout only that comparison skips;
105-slot coverage, five reserved slots, all 96 object spawns, persistent
authored stats across initialization, hero/mana/transport checks and
stat-backed production still run. Reproduce with:

```sh
env SDL_VIDEODRIVER=dummy build/bin/tests/warcraft-2/test_catalog
env SDL_VIDEODRIVER=dummy make test-warcraft-2
```

The reference audit covers base definitions, not upgrade effects or a
completed simulation of combat, spellcasting, repair, transport or oil.
Original attack/recovery, mana timing and training clock fidelity remain
unknown until retail runtime or executable evidence establishes them.

**Audit outcome:** `make all`, all eleven headless Warcraft tests and the
Warcraft game `--check` passed. The independent reference audit checked
all 100 defined types; the roster test spawned all 96 object definitions.
ALAMO still loads 64 units and 2,798 resource deposits. Native human/orc
HUD captures and pixel regressions passed after routing stats to this table.

**Verification outcome (2026-10-04):** `make all`, all ten Warcraft tests,
the complete Dark Colony suite, native SPR/FIN layout and network suite
passed. All five game binaries passed `--check` with the dummy SDL driver.
The final lossy/reordered network run completed 550 tics with 296 packets
dropped, 145 duplicated and 130 reordered. Human/orc HUD captures were
rendered and inspected at 640×480. These outcomes verify this engine's
implementation; they do not remove the original-runtime unknowns above.

## Native sound playback (2026-10-07)

**Evidence and scope.** WAR2.EXE SHA-256 remains
`a2b4b2118ec6355371b58134be8c7331d1facc5989e7188a1d1bb68fd1f26671`;
this task did not disassemble or execute it. The reference pins in
`REFERENCES.md` apply. These are confirmed archive facts and reference
assignments, not a claim of verified retail sound dispatch.

**Confirmed native data.** `DATA/SFXDAT.SUD` is 6,809,845 bytes, SHA-256
`05645c6efb4f38acbff955b20bfd06a6da42942434f37f771f7c792f1e62fb95`.
Its header is WAR magic 0x19 at offset 0, 293 entries at offset 4 and
archive type 5000 at offset 6; its first record starts at 0x49c.
The existing WAR extractor handles these records unchanged once type 5000
is accepted. All 184 referenced SFX records decode as RIFF WAVs, as does
MAINDAT entry 432 (the 1,524-byte UI click). The sample bytes are read from
the native archive, decoded in memory, and freed after conversion to the
shared mixer's mono signed-16 representation. There are no extracted
runtime files or reference-checkout dependencies.

**Disproven initial assumption.** SNDDAT.WAR is not the gameplay sound
bank. Wargus `wartool.h` places gameplay effects/voices in SFXDAT.SUD,
UI click/highclick/statsthump at MAINDAT entries 432/435/436, and campaign
speech in SNDDAT.WAR. `wartool.cpp::ConvertWav` preserves the extracted
WAV bytes; no additional codec or guessed raw-PCM header is required.

**Confirmed reference assignments.** Wargus `wartool.h`,
`scripts/sound.lua`, and `scripts/{human,orc}/units.lua` establish the
entry identities, group membership and type/event assignments authored in
`games/warcraft-2/sounds.c`. Examples (zero-based native entry indices):

- Human selection: 5,7,9,11,13,15; orc selection: 6,8,10,12,14,16.
  Human acknowledgements: 32,34,36,38; orc: 33,35,37,39.
- Peasant selection: 271–274, acknowledgements: 275–278, ready: 263.
  Peon selection/acknowledgements share the grunt voice, but ready is 115.
- Swords: 60–62; bow throw: 66; axe throw: 77; peasant attack: 81;
  lightning: 111; touch of darkness: 112; catapult/ballista: 55.
- Building destruction: 52–54; chopping: 56–59; ship sinking: 51.
  Dragon/Deathwing deaths use explosion 31, not orc infantry death 50.
  Eye of Kilrogg has selection click but no assigned acknowledgement/death.
- Buildings retain their distinct selection samples (e.g. farm 74,
  pig farm 75, blacksmith 69). Worker construction completion uses human
  peasant 42 or orc 41; research completion uses player-side 40 or 41.
- The base bank ends at 292; it lacks expansion hero voices. The authored
  hero assignments use Wargus's explicit non-expansion mappings, including
  Teron selection sharing basic orc voices and acknowledgement using the
  death knight. No guessed sound aliases are used.
- `scripts/{human,orc}/anim.lua` puts tree-chopping on source frame 40,
  which is logical frame 8 after the five-direction GRP conversion. The
  fourth state of the existing seven-state work cycle now invokes
  `A_W2_Chop`; attack audio uses the existing attack action event.

**Reference comparison / implementation policy.** Warcraft 2000
`GameSound.cpp::LoadSounds` and `PlayEffect` group WAV alternatives and
keep playback state separate from game simulation. `AddWarEffect` and
`AddWorkEffect` check visibility and pan relative to the viewport. Its
DirectSound buffers, fog thresholds, pan/attenuation constants and RNG
are not Warcraft II rules and were not imported. Dark Colony's shared
`sound/s_sound.c` and `sound/i_sound.c` provide sample ownership, channel
handles, stereo positioning, volume, ownership-filtered barks, fog checks
for enemy world sounds, origin unlinking and level/shutdown cleanup.
Original Doom `s_sound.c` was consulted for the sound/channel ownership
boundary. No second mixer, simulation storage or sound RNG was added.

Native samples live only in the engine sfx table. Initialization failures
free partially loaded samples before closing the audio device, so a retry
starts with a clean table. Training, construction and research completion
use the shared single-voice bark channel; level start stops old playback.

**Unknown / deliberately not inferred.** Retail annoyance thresholds,
under-attack warning cooldowns, distance attenuation, per-sample limits,
hero expansion dispatch, and tower attack assignments remain unknown.
The existing engine group randomization/no-repeat and voice interruption
are engine policy, not proven Warcraft II behavior. Warcraft retains shared
stereo panning with no new distance curve. Wargus itself marks ship attack
assignments uncertain; its fireball-throw assignment is used as reference
behavior, not retail proof. Music, campaign narration, annoyance escalation,
alerts, and sounds for unimplemented spells/transport are not implemented.

**Reproduction.** Build and run `build/bin/tests/warcraft-2/test_sounds`
with SDL video/audio dummy drivers. It checks 185 loaded WAVs and 35 groups,
plays/stops every loaded sample, checks voice/weapon/building assignments,
volume, fog, ownership, unchanged combat RNG, level cleanup, malformed WAV
rejection, partial-load failure/retry, shutdown/reinitialization and nosound.
Temporary `OPEN_RTS_DEBUG_W2_SOUND` logging recorded archive name, entry,
extracted size, RIFF signature and decode result; it was removed after
verification. The native menu screenshot also renders correctly.

**Verification outcome.** `make -j4 all` passed without compiler warnings;
all 22 `test-warcraft-2` executables passed (the network menu tests require
local UDP socket permission). Warcraft II and Dark Colony dummy-audio sound
tests passed, as did both games' headless `--check`. The Warcraft native
menu BMP was rendered and visually inspected. Playback validation used the
SDL dummy audio driver; this does not constitute a listening comparison
against the original game.

### Engine audio ownership clarification (2026-10-07)

The playback system belongs to the engine's `sound/` directory. Dark Colony
is a client of it, as is Warcraft II; there is no cross-game implementation
dependency. Warcraft's native loader now passes WAV bytes to `S_LoadSound`
instead of calling `I_LoadSampleMemory` and assigning a mixer sample pointer.
The engine validates the sfx slot and owns decoding, caching and cleanup.
This follows the `S_`/`I_` split in the local Doom reference. The sound
behavior and native assignments above are unchanged.

Generic channel, bark, visibility, attenuation, invalid-WAV and lifecycle
tests now live in `tests/shared/test_sound.c`, using a synthetic WAV with
no retail assets. They run for all five game builds through Makefile source
discovery. Native Dark Colony tests retain SOUND2/SLIST/GAMESTAT/AMB evidence;
Warcraft tests retain archive playback, event mappings and partial-bank
failure recovery. Run `build/bin/tests/<game>/test_sound` with dummy SDL
video/audio drivers for the shared ownership checks.

**Ownership refactor verification:** all five game binaries build, the
shared synthetic-WAV suite passes under every game, all 23 Warcraft II and
68 Dark Colony regression executables pass, and both games' headless smoke
checks pass. No game source calls the I_ sound/sample API.
## Scenario picker controls and screenshot comparison (2026-10-07)

**Sources.** The user supplied `/Users/igor/Desktop/Screenshot 2026-10-07 at
12.00.30.jpg`, 1290x968, SHA-256
`1a2bd92f8884822c589b62995f55527b68d1b13bcd1e4dfeb4967e3784a0a7fb`.
Its logo identifies Battle.net Edition; the installed localized DOS data is a
different edition. WAR2.EXE retains fingerprint
`a2b4b2118ec6355371b58134be8c7331d1facc5989e7188a1d1bb68fd1f26671`;
it was not disassembled or run for this change. REZDAT.WAR SHA-256 is
`d0fe7edd4f89f60c64bca786a944387578635d8483bc2ff165d06306bb432246`;
STRDAT.WAR is `5ba75d38613852be7137c5ec4035578977eb75ce5adc37d219875ba308ce2c26`.
MAINDAT's fingerprint and the pinned Wargus provenance remain as recorded
below and in REFERENCES.md.

**Confirmed native records.** REZDAT's header at offsets 4/6 reports 91
entries, type 3000. Native GFUs 0/1 contain 50 widgets in this distribution.
Orc frame 46 is a 300x18 red list/pulldown bar; 45 is its disabled artwork.
Frames 29/30 are normal/pressed up arrows, 32/33 down arrows (19x20),
28/31 their disabled states, 40 the 17x17 knob, and 41/42 the disabled/normal
19x124 vertical track. No folder-up frames 50–52 exist in this GFU, although
the pinned Wargus catalog describes them for other distributions. REZDAT 12
is the 352x352 orc scenario panel. MAINDAT FONT 281 is 17x17, with M advance
14; FONT 282 is 14x14, with M advance 11. The front end previously used 282
as its large font. Gameplay HUD fonts are separate and unchanged.

**Confirmed scenario data.** MAINDAT entries 220–247 are 28 valid PUDs,
matching the pinned `wartool.h` skirmish range. Entry 220's DESC is
"Gold separates east from west", matching the map description behind the
reference dialog. Header DIM/OWNR supply dimensions and playing-slot count
(owners 4/5); extracted archive scenarios load through the ordinary PUD
path. The eight loose scenarios remain available under Custom scenario.

**Screenshot-derived layout, not executable-derived behavior.** Normalizing
the supplied image to 640x480 gives the centered 352-pixel panel at roughly
(144,50). The picker now uses that placement, type/size/directory selectors,
six 18-pixel list rows at (166,186), native arrows and knob, dimensions/player
count, Cancel on the left and OK on the right. Setup remains visible behind
the modal panel and cannot receive clicks or hotkeys. Directory names carry
a slash because no verified folder icon was found in these DOS widgets.
Directories are listed even under a size filter, so filtering cannot trap
navigation. Built-in maps and custom directories are distinct functional
sources; empty directories disable OK. These are implemented engine behavior,
not a claim about the retail executable's dispatch or sorting.

The shared engine now owns dropdown popup drawing, hit testing, pending
selection, keyboard/wheel input, Escape/outside-click cancellation, native
list-row pictures, authored disabled states, fixed-size scrollbar knobs,
Home/End/Page navigation, and double-click list activation. A dropdown draws
over the complete screen, clips its label before the arrow, and consumes
outside clicks. Menu transitions discard popup/pressed input. This keeps
games as tables and action routines, consistent with the existing Doom menu
lifecycle; `reference/DOOM/m_menu.c` was consulted for selection, Enter,
Escape, and menu transitions, not Warcraft widget layout.

**Disproven assumptions.** Native chrome alone did not make the old generic
list faithful: its brown selection fill and bare rectangular scrollbar
replaced artwork already present in REZDAT. Using the game font as the menu
font also reduced text size. The localized multiplayer test failure seen
during the initial run was caused by denied UDP socket creation in the
sandbox, not a missing STRDAT label; it passes with local sockets allowed.

**Unknown / incomplete fidelity.** No Battle.net MPQ or executable exists
in the inspected data tree. No verified native scene-layout record was
identified or imported. This change loads native graphics/fonts and PUDs at
runtime but authors the control layout in C. REZDAT records beyond the
identified graphics were not comprehensively classified. English Battle.net
logo, localization, folder artwork, cursor placement, exact RGB/geometry,
race/opponent setup controls, and retail dropdown/scroll dispatch still need
the corresponding edition's files and further verification. The parent
setup is the current engine setup, not a pixel-exact Battle.net scene. Do not
replace these unknowns with screenshot-generated assets or tuned constants.

**Verification.** `make` builds all five binaries without new warnings.
All 24 `test-warcraft-2` executables pass with SDL's dummy driver and local
UDP permitted. Shared menu tests also pass for the other four games.
`test_menu` compares 4,320 unlettered list pixels against decoded GFU 46,
including the selected row, and checks native font dimensions, 128x128
filtering, empty DATA-directory browsing, all 28 built-in maps, archive-map
selection, and normal launch/campaign behavior. Shared tests cover popup
overlap, cancel-without-commit, outside-click consumption, disabled input/art,
upward opening, and keyboard/double-click list activation. The picker also
renders and accepts popup input at 1280x960 (UI scale two); multiplayer still
lists only the original eight loose PUDs, independent of picker filters.
Headless check and
gameplay BMP generation pass. Picker, open size popup, built-in list and
empty-folder BMPs were rendered and visually inspected. These checks certify
native asset rendering and engine controls, not whole-screen equivalence to
the supplied JPEG.

Reproduce:

```sh
make
env SDL_VIDEODRIVER=dummy make test-warcraft-2
env SDL_VIDEODRIVER=dummy W2_MENU_SHOTS=/private/tmp build/bin/tests/warcraft-2/test_menu
env SDL_VIDEODRIVER=dummy build/bin/warcraft-2 --check
```

## Single-player campaign entry correction (2026-10-07)

**Source and correction.** The user's second screenshot,
`Screenshot 2026-10-07 at 13.10.15.jpg`, SHA-256
`cab6de19f435ec47d0e906f0ffc25bbf6cefb31ebb32df457ac1d644e76bbb6f`,
shows our Orc campaign list, not a retail reference. Its native panel and
font did not establish native behavior: `open_pick(PICK_ORC)` authored a
14-mission browser with a generic brown highlight and rectangular scrollbar.
The previous scenario-picker change did not correct that screen. The
comment claiming other front-end screens followed Wargus was particularly
misleading for retail fidelity. Wargus's pinned `RunCampaignSubmenu` is its
own campaign/unlock selector and cannot establish the retail entry flow.

**Confirmed published behavior.** Blizzard's Battle.net Edition manual,
printed page 6 (PDF page index 5, URL recorded in REFERENCES.md), specifies
Single Player Game → New Campaign → race selection → assignment briefing →
first mission. It does not direct the player to a campaign mission browser.
This establishes the target flow for the Battle.net Edition reference;
exact DOS/Battle.net screen differences and button positions remain unknown.

**Confirmed native text/data.** The same WAR2.EXE/STRDAT/REZDAT/MAINDAT
fingerprints recorded above apply. Temporary `OPEN_RTS_DEBUG_W2_SINGLE`
logging printed decoded dialog labels. STRDAT entry 6 string 0 is the
internal name `NewCampaign`; strings 1/2/3 are the localized Orc Campaign,
Human Campaign and Previous Menu labels. STRDAT 27/3 is the load-screen
label; 27/1 is only its Load action, which the previous single-player screen
incorrectly reused. STRDAT 9/4 supplies the localized custom-scenario label.
No visible New Campaign label was identified in these dialog records.
The engine uses the manual's English label as an explicit fallback and
retains native localized labels for the other controls. It does not display
the internal `NewCampaign` identifier as a caption or invent a translation.

Static investigation also found STRDAT entry 1 to be an 8,828-byte general
string table with native u16 count 428, including unit names and orders.
The current loader's 96-string limit rejects it; temporarily raising that
limit decoded it but did not reveal the missing New Campaign caption.
That temporary change and all diagnostic logging were removed. Supporting
that general table is separate work; its exclusion is not evidence of a
missing dialog scene. STRDAT entries 64/65 contain the two first-mission
briefing texts, and the pinned wartool catalog corroborates alternating
human/orc briefing records. The existing 160-byte `w2_text_t` would truncate
long briefings. No briefing layout or narration dispatch was verified.

**Runtime investigation boundary.** Before the user instructed us not to
run retail, DOSBox was launched against a disposable copy of the installed
DOS data. Only the animated Blizzard introduction was observed; no retail
menu was reached or captured. Both retail emulator processes were confirmed
absent after the user's instruction. No further retail execution, CD-key
entry, or licensing workaround was performed. Introduction observations
supply no evidence about single-player layout. Subsequent verification runs
only open-rts and its own headless tests.

**Implementation.** Single Player now has New Campaign, Load Game, Custom
Scenario and Previous Menu. New Campaign opens only the three native race
choice/back buttons. Selecting either race extracts its first campaign PUD
through the existing loader and records campaign level 1. The campaign
browser, pick-mode enum and campaign-specific scenario-list branches were
removed. The true scenario picker retains its native dropdown/list controls.
Menus use the existing engine-owned Doom-style menu transitions and escape
callbacks; no per-game responder or drawer was introduced.

**Unknown / incomplete fidelity.** These screens reuse the existing native
REZDAT title and full-button column geometry; their final placement/order,
English fallback localization and complete Battle.net artwork were not
verified against retail pixels. Native scene records are still not imported.
Race selection currently starts mission one directly: the manual's briefing
and campaign chapter screens/narration remain unimplemented. This is a
correction of the known wrong browser and entry structure, not certification
of full retail equivalence. Do not replace the missing briefing with another
invented screen or present the open-rts screenshot as retail proof.

**Verification.** `make` completes without new warnings; all 24 Warcraft II
regression executables pass with `SDL_VIDEODRIVER=dummy` and local UDP
permitted. The focused menu regression asserts four single-player controls,
three race-selection buttons, no list/scrollbar on either screen, both back
paths, mission-one extraction for both races with native 32x32 dimensions
and player side, and subsequent human mission-two progression. Existing
scenario controls, save/load, results and shared widget tests still pass.
Open-rts headless `--check` passes. The two corrected front-end screens were
rendered as BMPs and inspected; this verifies engine output, not pixel
identity to retail. Reproduce with `make`, `env SDL_VIDEODRIVER=dummy make
test-warcraft-2` (allow local UDP), and `env SDL_VIDEODRIVER=dummy
W2_MENU_SHOTS=/private/tmp build/bin/tests/warcraft-2/test_menu`.
## Campaign startup, native units and result transition (2026-10-07)

**Evidence boundary.** The user's `Screenshot 2026-10-07 at 13.29.41.jpg`
(SHA-256 `c6408ce3e98f350f7aac65cbb056ce75b2bfeee681499798964242d9914fabc8`)
shows open-rts, not retail Warcraft II. Its premature victory and oversized
dialog are engine defects, not evidence of retail behavior. Retail was not
executed. Static findings use the same 878,119-byte WAR2.EXE fingerprint
`a2b4b2118ec6355371b58134be8c7331d1facc5989e7188a1d1bb68fd1f26671`
and unchanged inner LE image/toolchain documented below. Native archive
fingerprints recorded elsewhere in this document continue to apply.

**Confirmed defects.** The old `p_victory.c::alive` counted buildings and
harvesters only. Mission one's enemies consist of infantry, so it reported
victory while the enemies were alive. It also applied elimination to a
construction objective. The old result popup was an authored 288x256 options
panel and skipped the actual result screen. Its next-level action passed
`launch_path` as both source and destination of `snprintf`; this overlap is
undefined and can destroy the queued filename. The driver independently
created six invented actors whenever native startup spawned zero objects.
That fallback is now removed, including for other games using the driver.
Defeat restart also used the last menu launch buffer, which could refer to a
different map after direct loading. It now queues the active `level.map_path`
using the same data-root normalization as ordinary launches and save loading.

**Confirmed native map contents.** MAINDAT 192/193 are human/Orc mission one;
194/195 are human/Orc mission two, corroborated by pinned Wargus `wartool.h`.
Each PUD `UNIT` record is eight bytes: u16 tile x/y at +0/+2, type at +4,
owner at +5, u16 data at +6. Start locations 94/95 are markers, not actors.
Coordinates below are the record's top-left tile, before footprint centring.

Orc mission one is a native winter 32x32 map with 14 records and 12 actors:

| Owner | Native type | Positions |
|---|---|---|
| 0 (player) | Peon 3 | (25,18) |
| 0 | Great Hall 75 | (22,22) |
| 0 | Grunt 1 | (18,23), (28,24), (20,27) |
| 0 | Pig Farm 59 | (27,26) |
| 1 | Footman 0 | (22,2), (2,17) |
| 1 | Archer 8 | (11,6), (2,29) |
| 15 | Gold Mine 92 | (5,3), (26,13), data 2 and 4 |
| 1 / 0 | Human / Orc start markers 94 / 95 | (13,7) / (22,21) |

Orc mission two is also winter: 40 records, 37 actors, nine player-owned
grunts (UNIT indices 23–29, 34, 38), one peon at (50,58), Great Hall at
(41,56), farms at (48,54)/(40,54). Sharp Axe type 53, owner 2, is at
(51,14); the Circle of Power type 100, owner 15, is at (2,40). Other
infantry belong to opposing/rescuable owners. Thus a larger army in mission
two is native placement, not justification to remove or replace actors.
Human mission one has 14 records/12 actors; human mission two 46/44.
The regression compares the complete actor multiset of all 28 campaign maps
against native records, with exact type, owner and footprint-centred position.
No startup actor is synthesized. A derived test fixture with an empty UNIT
section also remains empty through both the loader and real engine driver.

**Confirmed objective evidence; implementation consequence.** STRDAT 53
indices 0/1 both require four farms and a barracks. Pinned Wargus
`campaigns/{human,orc}/level01*_c.sms` tests the corresponding type counts
against 4/1 and defeats the player at total unit count zero. Its generic
`SinglePlayerTriggers` likewise counts all units, not only buildings/workers.
The engine now counts surviving owned non-neutral actors and checks the
first campaign's race-specific building objective. It requires construction
to finish before satisfying that objective; that completion detail is an
implementation interpretation, not a traced retail instruction. Campaign
identity and outcome belong to `level.mission`, freed by the level owner;
the queued next campaign is consumed on load. Save extras retain their
existing 16-byte representation and restore the active mission identity.
The objectives menu now selects STRDAT 53 at `2*(mission-1)+orc`, instead
of displaying generic objective 34 for every campaign mission.

**Reference comparison.** Wargus's Orc mission two uses rescue/return-to-circle
conditions, and mission three requires a shipyard and four oil platforms.
These disprove a universal campaign elimination condition. Wargus loads the
converted campaign map; its separate custom-game `CreateUnit` wrapper can
add peasant-start units and is not copied into our native startup.
Warcraft 2000's `mapa.cpp::PostLoadExtendedMap` (line 641 in the pinned tree)
reads its own unit count, then type/owner/x/y and calls `CreateUnit` once per
record. Its MPF format is different and supplies no Warcraft II PUD layout
or campaign-objective evidence. `Nation.cpp::WinnerControl` (3262) counts
existing non-UFO objects on both sides; `mapa.cpp::ShowWinner` (1509) displays
an outcome when either count is zero. This is a comparison of ownership and
counting behavior only. No reference source was copied.

**Confirmed native result scenes.** REZDAT 57/resource 3057 is the victory
acknowledgement, linked to STR resource 4020. Root (256,176), 288x128;
Congratulations and You Won occupy (288,192)/(288,212), each 224x18.
Save button ID 1 is (288,234), 224x28; default Victory button ID -2 is
(288,268), 224x28. REZDAT 58/resource 3058 supplies defeat. Both now use
the matching native 288x128 dialog panel rather than the options panel.

REZDAT 59/resource 3059 supplies the 640x480 two-row result scene, 31
records, STR resource 4022. Its Continue ID -2 is (456,448), 106x28.
Result/rank/score headings are at y60; runtime result ID 1 is (12,80),
192x50. Player slots IDs 4/5 are (40,236)/(40,340), 560x36. Seven native
column headings IDs 71–77 are at x=`4+90*column`, y180, 90x18; runtime
statistics start at x=`10+90*column`, y212/y316, 80x24. These rectangles
are loaded from native records, not recreated by an authored table.

Static result routine VA **0x48318** loads byte **0x8032d** at **0x48354**
and selects resources 3059/3060/3061/3062 at **0x4835e/0x4836a/0x48376/
0x4837d**, for values <=2/<=4/<=6/larger. It calls scene loader **0x58bec**
at **0x48382**, then **0x59060** at **0x48391** with handler **0x4822c**.
Interpreting that byte as participant count is inferred from the row counts;
the exact producer of the byte is not yet traced. Code VA maps to original
file offset `VA + 0x4a6a4` as documented in the static-analysis section.

MAINDAT images 359/360/361/362 and palettes 363/364/365/366 are respectively
human victory, Orc victory, human defeat and Orc defeat, corroborated by
the pinned extractor catalog. The engine decodes these native images with
their own palettes. Acknowledgement now opens the result scene; only its
Continue action queues mission two or a restart/return. Next-level extraction
uses a separate local path buffer. Engine screenshots of both victory
backgrounds were inspected; they establish engine rendering, not exact
retail pixel identity.

**Unknown / incomplete.** Mission 2–14 rescue, region and target objectives
are not implemented; they deliberately cannot fall through to generic
victory, though losing all owned actors still causes defeat. Briefings,
chapter transitions and final campaign ending remain incomplete. Runtime
result fields for player names, historical counters, score and rank remain
unbound; only the outcome is populated. The engine currently selects the
two-row stats scene rather than tracing the retail participant-count field.
Retail palette composition for controls over the result artwork and full
result callback behavior are unverified. Do not claim that native scene
decoding alone implements all scene behavior or proves pixel equivalence.

**Verification / reproduction.** `make` succeeds; all 25 Warcraft II
regression executables pass headlessly with local UDP allowed. `test_menu`
checks both first missions stay active at startup, unfinished barracks do
not win, completed 4-farm/1-barracks objectives win, acknowledgement opens
native stats, and Continue loads the correct second mission for each race.
It also checks skirmish victory/defeat transitions. `test_pud` checks all
28 native campaign actor multisets and creates the empty-map fixture.
Headless checks of the per-game Dark Reign, Dark Colony and 7th Legion
binaries also pass after removing the shared driver's empty-map fallback.

```sh
make
env SDL_VIDEODRIVER=dummy make test-warcraft-2
env SDL_VIDEODRIVER=dummy W2_MENU_SHOTS=/private/tmp build/bin/tests/warcraft-2/test_menu
env SDL_VIDEODRIVER=dummy build/bin/warcraft-2 --check
env SDL_VIDEODRIVER=dummy build/bin/warcraft-2 --check --map "$PWD/build/test-user/empty-campaign.pud"
# With the unchanged inner image and disposable r2 toolchain prepared:
/private/tmp/war2-analysis-tools/prefix/bin/r2 -q -e bin.cache=true -e bin.relocs.apply=true -A -c 'pdf @ 0x48318' -c q reverse/war2-exe-r2ghidra/war2-inner.mz
```

## Native dialog drawing, scenario picker and custom setup (2026-10-07)

**Evidence.** Static analysis only of the unchanged inner LE image
(`reverse/war2-exe-r2ghidra/war2-inner.mz`, WAR2.EXE fingerprint above) with
the disposable r2 toolchain; retail was not run. Addresses are code VAs.
User reference: Battle.net Edition screenshots of Custom Game Setup and the
scenario picker; the installed data is the localized DOS edition.

**Confirmed: one text routine for every control.** Draw handlers are table
`0x5dd70`, indexed by native kind: buttons `0x5e83c`, left/centre/right
captions `0x5ed78`/`0x5eda4`/`0x5ede8`, scroll bar `0x5ebd8`, list `0x5a9ec`,
dropdown `0x5b5e8`→`0x5b4d0`. All call `0x5e148(item, x, y)` with an alignment
byte at `0x995c6` (low nibble 1 left / 2 centre / 4 right, high nibble 1 top /
2 centre). Layout `0x4fee8`: block width is the widest line, height is
`lines × (FONT byte 7 + 3)`; horizontal centre subtracts width/2, vertical
centre subtracts `(height − 3)/2`. Every line starts at the block's left x.
Glyph walk `0x50108` / widths `0x4fbcc`: advance = glyph width + x offset + 1,
space = FONT byte 6 / 2 + 1. Our decoder's advances lack that one pixel.
Glyph pixel n writes colour map[n]; n = 0 writes palette index 0.

| Control | Native placement |
|---|---|
| Button | mode `0x22` at `left + s + (w+1)/2`, `top + s + (h+1)/2`; s = 1, pressed (`0x4000`) 3 (`0x88cf8`/`0x88cf4`). Kind 1 (default) adds a `0xf7` rim (`0x5e77c`) |
| Caption 9/10/11 | top-aligned at left / `left+(w−1)/2` / `left+w−1` |
| List | rims `0x5dfa8`: outer `0xf8`, inner `0xfb` focused (`0x1000`) else 0; rows of frame 46 from `left+2, top+2`, cropped not stretched; text mode `0x11` at row `+2,+2`, cut until it fits the control width; selected colour 4, normal 2 |
| Dropdown (closed) | frame 46/45 at `+2,+2` cropped to `w−4 × row`; arrow frame 32/31 at `left+w−1−arrowW, top+1`; rims on `w × (row+4)`; text at `+4,+4` |
| Scroll bar | arrows 29/30 (28 disabled) and 32/33 (31); track 42/41 (frame y offset 20); knob 40. Knob position routine `0x64548` untraced |

Row height and widget metrics are the widget frames' sizes (`0x5910c`):
row = frame 45 height (18), arrow = frame 28 (19×20), knob = frame 40.
Lists keep `(h − 3)/row` whole rows and their scroll bar (ID `id ^ 0x8000`)
spans the list at `right+1` (`0x5ae28`).

**Confirmed fonts and colours.** Dialog redraw `0x58e8c` selects MAINDAT 282
for every control; flag `0x0800` picks 281 and `0x0400` 283 (`0x5e148`,
fonts loaded at `0x2913c`). Colour: flag `0x0002` (disabled) → 5, `0x0080`
→ 4, else 2; captions with `0x8000` → 4. Maps (`0x10c20`, front-end mode):
2 = c8 c7 c5 c0 ef, 4 = f6 f6 6c 68 ef, 5 = 6c 6c 69 66 ef, 3 = bf bf a8 a7 ef.
Flag `0x0008` is visibility: `0x5a568` clears `0x0008`/`0x4000` to hide.

**Superseded.** The earlier glyph-bounds centring (`V_TextBounds`, "M at rows
8–19 with FONT 281") was an authored rule, not the executable's. Using 281 as
the front-end font came from the Battle.net screenshot; the DOS executable
uses 282 unless a record sets `0x0800`. Measured Battle.net button text
matches 281 (132 px "Select Scenario") while its dropdown text matches 282:
that edition's records differ and do not describe the DOS game.

**Confirmed picker contents (`0x1755c`, REZDAT 89).** Type table `0x812a8`:
built-in (fill `0x1768c`, ≤28), custom (`0x177e8`, ≤512), saved (`0x178bc`,
≤32). Dropdown strings are STRDAT 63 (`0x173b4`): type 0..1 (single player;
multiplayer adds 2), size 5..9, players 10..12. Built-in names are STRDAT
63/22..49 for MAINDAT 220..247 (`0x17618`; players/size pairs table
`0x16dc0`). Custom scenarios come from `findfirst("*.PUD")` with attribute 0
(`0x4c5bc`, `0x5f0f0`) — plain files, **no directories** — named by the
lowercased file name (`0x5f0d1`). Players = slots with owner 5. The size
filter (`0x16e00`) keeps unknown sizes; the players filter applies only in
multiplayer (`0x1714c`). Outside multiplayer, handler `0x14bc8` hides control
4 and caption 9. Info lines (`0x16ed4`): ID 5 = PUD description (custom) or
name, ID 6 = 63/(size+5), ID 7 = 63/(players+12); OK disabled without a pick.

**Confirmed custom setup.** Single player loads **MUDDAT 6001** (`0x15d58`,
`0x1771`); multiplayer loads 3080 or 6013. Dropdown contents are STRDAT 45
(`0x14f70`): race 19/20/10 (default 2, Map Default), opponents 10 + 26..32,
resources 10..13, terrain 10 + 21..23, units 10/18, placement 17/16 (hidden).
ID 11 is `"%s\n%s"` of 45/type and the scenario name (`0x13e84`). The initial
scenario is built-in 0.

**Implementation.** `menu_t.drawitem` lets a game draw each item; the engine
keeps input and layout. `games/warcraft-2/w_dialog.c` ports the handlers above
and is installed on every Warcraft II screen, including the shared network
screens. `menuitem_t.flags` carries the record flags and native kind
(`W2_ITEM_FLAGS`). Setup is MUDDAT 6001; only Resources is applied by the
engine, so race, opponents, terrain and units are shown disabled at Map
Default. Custom files are sorted by name (findfirst order is directory order).
Multiplayer's host, browse and lobby pages are the engine screens, not
3080/6013. Retail reaches them through 3042 then 3075, recorded below.
This build skips that connection list: Multiplayer opens TCP create and
join directly. The lobby's per-player race and Start press are an
implementation choice shared with Dark Colony's lobby, not a trace of 6013.
`netui_t.style_list` gives its lists the native rows, rims and a scroll bar
drawing its own arrows (`W2_ITEM_ARROWS`).

**Verification.** `make`; `env SDL_VIDEODRIVER=dummy make test-warcraft-2`
(25 pass); shared menu tests for the other four games; `--check` for all five
binaries. `test_menu` checks the 6001/89 geometry and labels, built-in names,
custom file-only listing, info lines, cropped row frames and a button's text
at rows 8–19 / columns 49–58 per `0x5e83c`. Picker, setup and popup BMPs from
`W2_MENU_SHOTS` were inspected.

## Retail dialog screens (2026-10-07)

**Evidence.** Static analysis of the same inner LE image. Retail was not
run. Dialog geometry below was decoded from this install's REZDAT with
`w2_load_scene` (windows are absolute; children are window-relative in the
file and stored absolute). Widget frames were read from the drawers. This
is not a claim of pixel identity with a running DOS session.

**Confirmed widget frames.** Checkbox `0x5e8cc`: flag `0x0002` selects
frame 18; flag `0x4000` selects `state*2+20`, otherwise `state*2+19`.
Unchecked rest is 19, checked rest is 21, disabled is 18. Radio `0x5e990`:
disabled is 23; pressed adds `0x19` to `state*2` (25 off, 27 on) and the
rest path adds `0x18` (24 off, 26 on). Caption x is the width of frame 22
(checkbox) or 27 (radio) plus 4, mode `0x21`. Horizontal slider `0x5ea8c`
uses caps 34/35/36 and 37/38/39 (`0x22`/`0x25` plus enabled plus pressed),
track 43 disabled / 44 enabled at x plus the cap width, and knob 40. The
sheet's track x-offset of 20 is not stored by the GRP loader, so the drawer
adds the cap width itself. Text field `0x5ed24` draws rim `0xfb` only when
flag `0x8000` is set and the control is focused. Save's field is `0x0018`,
so it has no rim; the caret is an underscore while that field is focused.

**Confirmed, then untraced: knob travel `0x64548`.** For kind 6 the prefix
is `(right−left) − knob_width − 1 − 2×cap_width` (globals `0xa7e44` and
`0xa7e3e`). The multiply that turns a value into a pixel was not traced.
The grab width of 57 is an implementation choice: two 20px caps plus the
17px knob, so the engine drag centre matches a knob drawn inside that grab.
It is not a constant from the executable. Focus rims are the control rect
in `0xf7` (check) or `0xfb` (slider); the rest of `0x5de94` was not traced.

**Confirmed text.** Long strings (credits STRDAT 57, briefings 64+,
objectives 53) do not fit `w2_text_t`. `w2_label_copy` keeps the same
hotkey strip and writes a caller buffer. Wrapped prose uses the dialog
advance (glyph width + 1) and the dialog colour map, and honours
`first_row`. Briefing index is `64 + 2*(level−1) + (orc ? 1 : 0)`, string
0. Human chrome is REZDAT 82 (prose at 72,80 320×200; title 12,28 480×20);
orc chrome is 83 (prose 264,80 320×200; title 224,28 400×20). Objectives
caption is at 372,306 and the body at 372,330 252×108. Continue uses
the chosen side's widget sheet. The picture behind that chrome is the
mission introscreen, recorded below. The dimmed title was a stand-in.

**Confirmed navigation, from the result switches.** Game menu 3044
(`0x48dfc`, window 272,96 256×288, panel GAME): 1 save 3063, 2 load 3064
in a level and 3065 when no level is loaded (retail tests a front-end
dword; `level.width == 0` is the stand-in), 3 options 3047, 4 help 3045,
5 objectives 3081, 6 end 3046, −3 closes. Options 3047 (`0x48f6c`): 1
sound 3048, 2 speed 3050, 3 screen 3049. Help 3045 (`0x48ec4`): 1 key
pages 3086, 2 tips 3077, −3 back. End 3046 (`0x49014`): 1 confirm 3055,
3 confirm 3056, 4 confirm 3051. Results 2, 5 and 6 share `0x49380`, which
picks 3053, 3054 or 3052 from bytes `0x8127a` and `0x80331`. Init
`0x490f4` hides the two overlapping y=136 buttons that do not apply.
Who writes those bytes was not traced. A network game shows id 5, a
custom file shows id 6, otherwise id 2. That mapping is inferred from
the labels.

**Inferred confirm actions.** The openers are confirmed; the OK handlers
past them were not. OK on 3055, 3056 and 3053 leaves the match. OK on
3052 and 3054 reloads the current map. OK on 3051 quits. Cancel returns
to 3046.

**Record layouts that the menus now open.** Sound 3048 and speed 3050 are
212×18 sliders (ids 1/2/3) with tiny min/max captions (`0x0400`). Sound
checks 7, 4, 5, 6 have flags `0x0218` and no group. Screen 3049 radios
have flags `0x0a18` in three groups (5/4, 1/2, 7/8). Flag `0x0200` is on
those radios; it is not a font bit and not the checked state. Sound and
speed sliders apply on change and revert on cancel. Screen radios, mouse
speed and keyboard speed are stored on OK and are not applied: there is
no fog toggle, mouse-style switch or minimap-info switch in the engine.
CD volume is stored the same way. Tips 3077 (256,112 288×256, OPTIONS
panel, STRDAT 41) has a checkbox and an empty body id −10; entry 41 has
five short strings and no tip pages, so Next does nothing and no tip is
shown at startup. Key help 3086 (224,64 352×352, SCENARIO panel) has
thirteen caption lines filled from STRDAT 59, 13 per page. Objectives
3081 fills id −4 from STRDAT 53; a skirmish uses index 34. Save 3063
(208,112 384×256, FILE panel) has a disabled scrollbar (`0x0008`, id
−32767). In-level load 3064 enables that bar (`0x0018`). Front-end load
3065 is the full 640×480 screen with no file panel. Credits 3084 (opener
`0x4c570`) is the title plus STRDAT 57 in id 1 at 140,80 360×280.

**Connection entry.** Retail Multiplayer opens 3042 (loaded at `0x4d107`):
list id 1 at 28,274 240×96 and description id 2. STRDAT 60 supplies the
three method names at indices 2/3/4 and the descriptions at 5/6/7. Modem
opens 3073, direct link opens 3072, and IPX opens viewgame 3075. That
branch is inferred from the strings and the child layouts; the list-fill
routine itself was not disassembled. COM, baud and IRQ tables are not in
STRDAT and were not found as ASCII in the inner image. Retail setup
3080/6013 and name dialog 3087 are not used.

**Superseded as the menu path.** Those connection screens were opened, and
Connect on 3072/3073 reported that the link was unavailable. Modem config
3074 stored three fields and a tone/pulse pair and returned to 3073. That
path is not the TCP game. Multiplayer now opens the engine create/join
page. Create still chooses the map and the player count; Join browses the
LAN or takes an address. Escape from those pages returns to create/join,
and Previous Menu returns to the title. The engine pages are not restyled
into 3075 or 6013.

**Implementation choice: the shared lobby.** This is not a WAR2.EXE
instruction trace. Create still offers a map and a player count; Join
still browses the LAN. Once both are in the lobby, each joined player
owns that seat's race and presses Start, the same split Dark Colony uses
for its ready check. The host publishes 16 bytes (eight races, then eight
Start flags) with `I_SetNetSetup`. A joiner publishes two bytes (its race,
its Start flag) with `I_SetNetChoice`. The host calls `I_LaunchNetGame`
when every reserved seat has joined and every joined player has pressed
Start. There is no second host-only Start. Warcraft II names the two
races from STRDAT 45 indices 19 and 20 (Human, Orc). The Start label is
STRDAT 9 index 2. Map Default stays on the single-player setup. The host list is MAINDAT
220–247, every one of which has at least two person slots (owner 5),
then loose `*.PUD` files with two or more person slots. A built-in map
is offered as `scenario-<entry>.pud`. `G_DoLoadLevel` reads that name
back: a path that is already a PUD loads directly, and `scenario-N.pud`
extracts MAINDAT entry N when it is not. The player count starts at the
map's person-slot count and cannot go past that count or the engine's
eight seats. A seat's first race is
that person slot's SIDE byte, person slots taken in ascending order.
When the lobby launches, the game copies `M_NetPlayerRace` before the
transport wipes the session. On a net load those races are applied after
the PUD is read: person slot p moves onto seat p, a person slot past
the player count becomes a computer (owner 4), and a paired unit type
swaps when the seat's race is Human or Orc and differs from the type.
Gold mines, critters, heroes without a pair, and the other unpaired
types stay. Fog, cheats, resources, terrain, units and placement from
record 6013 are not in this lobby.

**Still not these records.** HUD dialogs 3033–3039. The in-game panel
keeps its current command buttons; 3037/3038 were not aligned and icons
were not resized. Message filters 3066/3067, message boxes 3068–3071,
3076, 3078, 3079, 3085 and 3088/3090 are not opened. Serial and modem
transfer was not implemented.

**Unknown.** The rest of `0x64548`'s multiply. The rest of focus-rim
`0x5de94`. Whether fog, mouse style and minimap info exist beyond the
dialog. Writers of `0x8127a` and `0x80331`. Confirm OK past the openers.
3042's list filler. Which instruction selects a mission's introscreen.
Baud, COM and IRQ tables.
Which of 3059–3062 a given participant count selects. HUD command-chrome
frames.

## Mission briefing picture (2026-10-07)

**Evidence.** Static analysis of the same inner image, plus the installed
MAINDAT/REZDAT bytes and the user's Zul'Dare reference. Retail was not run.
The reference is an English capture of the orc mission-1 scroll (table,
candle, unrolled scroll, knuckle-guard dagger, white lettering, dark
Continue button with a yellow rim). This install's strings stay the Russian
STRDAT text.

**Confirmed: the opener does not name a picture.** `0x4c364` runs when
`word[0x80004] − 1192` is below 28. It adds the race byte `0x8032f` to
`0xC0A` (scene 3082 human, 3083 orc), sets the callback to `0x4c2fc` and
`edx` to 4, then calls `0x58bec` and `0x59060`. Both scenes' serialized
picture id at `+0x10` is 0, so `0x58ad4` does not call `0x5b810` for the
root. `0x4c2fc` is a message handler, not a loader: message 9 with control
id ≤ 0 calls `0x4c13c`, id 1 calls `0x4c270`, and message 8 calls `0x58ff8`
only after two globals pass a threshold. Campaign flow `0x296c8` calls
`0x29dcc` (the act-map selector, REZDAT 3019–3026) and only then `0x4c364`.
`0x21b84` writes `word[0x802da]` after the briefing returns. None of those
is the scroll.

**Disproven as the image table.** Dword immediates 369–378 around `0x3b4e0`
through `0x3b952` are dialog control and string ids (`0x3ca30`, `0x596b0`,
`0x39e70`). There is no packed table of 370,371,372,373,374 or
369,375,376,377,378 in the inner image.

**Confirmed picture for orc mission 1.** MAINDAT image 370 decoded with
palette 368 is that scroll. Palette 368 index `0xf6` is `#fcf8f0` and index
`0xc8` is `#601810`. Human mission 1 is the paired book, image 369 with
palette 367 (`0xc8` is `#c4a06c`, `0xf6` is the same white). The other
eight images are 371–374 (orc, palette 368) and 375–378 (human, palette
367), the same pairs wartool names `introscreen2`–`introscreen5`.

**Inferred level bands.** The Wargus campaign scripts, not an instruction
in this executable, assign the five pictures: orc missions 1–4, 5–7, 8–10,
11–12, 13–14 and human 1–4, 5–6, 7–10, 11–12, 13–14. The engine uses that
grouping. A traced retail rule would replace it.

**Confirmed button pixels.** The first 106×28 run in REZDAT widget sheets
0 and 1 (frames 0–2) uses indices `0x06`–`0x0e`. In palettes 367 and 368
those indices are a gray ramp (`0x06` `#989898` down to `0x0e` `#0c0c0c`).
In menu palette 14 the same indices are VGA colours and `0x0e` is
`#0000fc`. The last 106×28 run is the menu chrome, which is correct on
palette 14 and wrong on 367/368. The reference Continue button is the gray
bevel plus the focus rim. `0x5e77c` draws colour `0xf7` when the kind is 1,
then colour `0xfb` when flag `0x1000` is set (`0x5e810`). Both `0xf7`
(`#a0a0a4`) and `0xfb` (`#fcfc00`) match across palettes 14, 367 and 368.
The briefing button is frames 0–2, read in the introscreen palette so those
indices are not nearest-mapped out of palette 14. Menus keep the last run.

**Confirmed text colour on this picture.** Front-end colour 2 is
`c8 c7 c5 c0 ef`. On palette 368 that is dark parchment, and those indices
are used by the scroll, so they cannot be recoloured. Colour 4 is
`f6 f6 6c 68 ef`; `0xf6` is `#fcf8f0` on palettes 14, 16, 367 and 368.
Sampled reference lettering is that white. Briefing records do not carry
flag `0x8000` (the caption bit that selects colour 4). The briefing sets
it on its static controls, and sets `0x0080` on Continue, because colour 2
is unreadable on the bevel. The prose and title rectangles stay the DOS
records. The dotted line in the reference is one moment of a longer string;
the screen draws the whole STRDAT briefing.

**Confirmed shadow index.** `0x67768` writes `map[ink]`, and colour 4's last
byte is `0xef`. FONT 282 (the prose and Continue label) puts its drop shadow
in ink 5: the bottom rows of a glyph are that ink. Palettes 2, 10, 14, 16
and 18 store black at `0xef`. Palette 368 stores `#806c50` there (2 pixels
in image 370) and palette 367 stores `#989824` (0 pixels in image 369).
Index 0 is `#000000` on both. `0x68` and `0x6c` are `#6c6c6c` and `#9c9c9c`
on the menu palette and wood colours on 367/368, and the pictures use those
indices, so the palette slots stay. Text matches the menu RGB into the
screen palette: black lands on index 0, the two greys on `0x08` and `0x04`.
FONT 281 (title and objectives) has almost no ink 5; its edge is ink 4, the
grey, not a second black.

**Superseded.** "The portrait resource is unknown; both briefings use the
dimmed title." The dimmed title is REZDAT 15, the menu parchment, and it
was only a stand-in.

Reproduce the screens with `env SDL_VIDEODRIVER=dummy make test-warcraft-2`
and `env SDL_VIDEODRIVER=dummy W2_MENU_SHOTS=/private/tmp/w2-ui-shots
build/bin/tests/warcraft-2/test_menu`. `build/bin/warcraft-2 --check`
passes. The menu test opens Multiplayer onto create and join, then the
host page, the lobby Start button, and the session browser. It also walks
both briefings, options, screen, sound, speed, help, key pages, tips,
objectives, end-mission, the restart confirm, and both load layouts.

## In-game feature audit against pinned Wargus (2026-10-07)

**Scope and evidence boundary.** This is an implementation audit, not a
certification of complete Warcraft II or Wargus parity. The native roster's
105 PUD slots and its populated capability fields did **not** mean those
capabilities were implemented. Before this change, ranged attacks applied
instant damage, spell names had no casting implementation, repairs and
patrol/stand-ground buttons were unavailable, only twelve combat researches
were actionable, tankers did not gather oil, and transports did not carry
units. Campaign objectives after mission one still require implementation.

No retail executable was executed or newly disassembled in this audit.
The WAR2.EXE and MAINDAT fingerprints recorded above still identify the
installed edition; no new executable function address is claimed. Behavior
below is **confirmed in the pinned reference source**, independently
implemented in C, and tested against the installed native assets. It is not
therefore confirmed as DOS executable behavior. Wargus commit
`cde1a0718a0058cc651ecd56ff8149fc39f624e9` and Stratagus commit
`3d87c93f7fd8c0b62ee1be5df0a6d9efc72ca6cc` were inspected locally.

### Coverage and remaining work

| System | Implemented and checked in this change | Remaining parity gaps |
|---|---|---|
| Combat | Traveling native projectiles, delayed impact, splash, bouncing fireballs/gryphon hammers/dragon breath, target-domain masks, minimum ranges, cloak detection, invulnerability, bloodlust damage | Catapult parabolic presentation, attack-ground orders, tile-wall/rock destruction, exact missile pixel anchors and obstruction rules |
| Spells | All 19 base spell/action entries have player cast orders, release-frame dispatch, mana/research checks and their effects; native sound and buff artwork | Wargus AI/autocast policies, exact summoned-unit dropout placement, exact area-selection/pixel trajectories, retail timings |
| Research | 48 paid research entries: 24 weapon/armor tiers and 24 distinct technologies; archer/knight transformations, range/sight/marksmanship/regeneration, spell unlocks, siege and naval bonuses, hero combat modifiers | Campaign-specific allowed/researched technology initialization; detailed upgraded-unit panel/training presentation |
| Training | 28 trainable land/air/naval base types plus four research conversions, prerequisites, food at release, 200 mobile-unit limit, research/upgrade cancellation/refunds, tower transformations, six-cycle time-cost conversion | Total/building limits, native producer exit placement under congestion, production-clock rounding and first-cycle phase |
| Buildings | Damage fire, paid allied repair, extra-worker construction assistance, tic-based construction, building/tower upgrades, platform placement/cancellation/destruction | Native oil-well construction stages, all shoreline separation/placement rules |
| Economy | Gold/lumber behavior retained; oil reserves from PUD, tanker/platform/depot cycle, refinery bonus, typed cargo, exhausted reserves, restored oil patches | Tanker loaded sprite variants and platform pumping animation; naval AI economy |
| Transports | Six land passengers, movement to a nearby carrier, hide while aboard, unload to free shore cells, sinking kills passengers, saveable carrier references | Automatic choice of a landing shore from an inland unload click, passenger portraits/selective unloading, allied-owner boarding |
| Orders/UI | Repair, patrol, stand ground, spell selection, board/unload, cancel production, mana/current research damage display, native status decorations | Complete native command layout/hotkeys, richer failed-order feedback, attack-ground, autocast toggles |
| Persistence/network | New commands go through ticcmd; research, buffs, mana, casts, repairs, cargo links, effects and RNG survive saves and enter consistency hashes | Multiplayer end-of-match/diplomacy parity and cross-platform determinism certification |
| Campaigns | Existing mission-one construction objectives retained | Later mission triggers, rescues, regions, mission tech restrictions, scripted reinforcements/AI, expansion campaign support |
| Computer players | Existing land economy/attack planner continues passing its ten-minute regression | Naval/air forces and spell research/casting strategies; campaign AI scripts |

The table deliberately does not equate a working manual spell or a complete
catalog with full game parity. Existing findings about native menu/result
layout and untraced retail timing remain applicable.

### Native effects: indices and disproven frame-count assumption

`wartool.h` maps MAINDAT entries 324–351 to the following effects. Native GRP
headers, decoded by `w2_decode_grp`, establish these temporal frame counts:

| Entry | Effect | Frames | Stored facings |
|---|---|---:|---:|
| 324 | Lightning | 6 | 5 |
| 325 | Gryphon hammer | 3 | 5 |
| 326 | Dragon breath | 1 | 5 |
| 327 | Fireball | 1 | 5 |
| 328 | Flame shield | 6 | 1 |
| 329 | Blizzard | 4 | 1 |
| 330 | Death and decay | 8 | 1 |
| 331 | Big cannon | 4 | 5 |
| 332 | Exorcism | 6 | 1 |
| 333 | Healing | 6 | 1 |
| 334 | Touch of death / death coil | 6 | 5 |
| 335 | Rune | 4 | 1 |
| 336 | Whirlwind | 4 | 1 |
| 337 | Catapult rock | 3 | 5 |
| 338 | Ballista bolt | 1 | 5 |
| 339 | Arrow | 1 | 5 |
| 340 | Axe | 3 | 5 |
| 341 | Submarine missile | 1 | 5 |
| 342 | Turtle missile | 1 | 5 |
| 343 | Small fire | 6 | 1 |
| 344 | Big fire | 10 | 1 |
| 345 | Impact | 6 | 1 |
| 346 | Normal spell | 6 | 1 |
| 347 | Explosion | 16 | 1 |
| 348 | Small cannon | 3 | 5 |
| 349 | Cannon explosion | 4 | 1 |
| 350 | Cannon-tower explosion | 4 | 1 |
| 351 | Daemon fire | 3 | 5 |

**Disproven:** copying exported Wargus `Frames` blindly is safe for all
native effects. Its scripts specify ten frames for exorcism/healing/impact
and twenty for explosion, while the installed GRPs have 6/6/6/16. The
implementation uses native bounds and tests every entry; five authored
facings use the existing loader's mirrored rotation definitions. No PNG,
new renderer-native callback, or duplicate pixel storage is introduced.
MAINDAT GFU 323 supplies five 16x16 bloodlust/haste/slow/invisibility/armor
icons, in that order (`wartool.h`, `scripts/ui.lua`). These remain HUD-owned
images; world decorations use the reference offsets 0/16/16/32/48 plus the
sprite's (1,1) offset, scaled with the world tile size.

Each projectile, fire, rune, shield segment and vision marker is an ordinary
allocated `MT_W2_EFFECT` mobj in `thinkercap`. Its source is Doom's ordinary
`target` reference, its destination/subject is saved mobj data, and one
one-tic action advances it. Native unit IDs remain unchanged. No effect pool
or separate ticker owns effects. Shooter removal clears the borrowed source
pointer safely; a stored damage snapshot keeps an in-flight weapon harmful.

`missiles.lua` establishes speeds in pixels/cycle: arrows/axes 32, siege
rock/bolt 8, small cannon 22, most other moving shots 16. These are divided
by the native 32-pixel tile size at the planar movement boundary. Range-zero
shots hit the selected target at arrival; range-positive shots use footprint
distance and splash divisors from the table. `missile_pointotpointwithhit.cpp`
(the filename has that spelling) keeps lightning/blizzard/touch on frame
zero in flight, plays the hit frames on arrival, and deals damage at the
end. Bounce continuation is `(tile_width + tile_height) * 3 / 4`, or 1.5
tiles here. Exact parabolic trajectories remain unimplemented.

### Damage fire and repair

`scripts/missiles.lua` and `unit.cpp::HitUnit_Burning` establish the fire
thresholds: at least 75% HP has no fire; 50% through below 75% uses small
fire; below 50% uses big fire. Integer percent is HP*100/maxHP. The source
places fire at the building center minus one tile vertically; the engine
represents this with world z=1 so depth sorting remains tied to the building.
The animation rechecks its size/removal at cycle boundaries. Repeated hits
reuse the same fire mobj; healing or destruction ends it. Unfinished sites
are excluded.

`action_repair.cpp::RepairUnit` uses the **target's** RepairHp/RepairCosts and
the worker's player resources. Worker repair follows the authored seven
poses `{5,6,7,8,9,5,5}` and waits `{3,3,3,5,3,7,1}`, totaling 25 cycles.
The final pose applies four HP for the catalog's gold/wood/oil cost. Range,
ownership, depletion, destruction and command interruption are checked.
The final partial repair is still a paid cycle. Assistance to a building
under construction uses `ProgressHp(100 * RepairCycle)`: helpers accumulate
time spent working and contribute it at their animation boundary. This
advances the same tic counter as the primary builder and preserves damage
already taken. Stratagus's default `ResourcesMultiBuildersMultiplier=0`
(`unit.cpp`) makes this assistance free. Whichever worker finishes the
site releases its primary builder and restores its finished state once.
New build, harvest, repair, board and spell orders cancel incompatible jobs.

### Spell behavior

| Spell | Mana | Range | Implemented effect / reference duration in cycles |
|---|---:|---:|---|
| Holy vision | 70 | unlimited | 12-tile sight/detection revealer, TTL 25 |
| Healing | 6/HP | 6 | Bulk healing up to missing HP and available mana |
| Exorcism | 4/HP | 10 | Bulk damage to an undead unit |
| Eye of Kilrogg | 70 | 6 | Owned eye, TTL 765 |
| Bloodlust | 50 | 6 | Doubles basic and piercing damage before armor, 1000 |
| Runes | 200 | 10 | Five cross-shaped traps, 50 damage, TTL 2000 |
| Fireball | 100 | 8 | 20 damage, five bounce segments |
| Slow | 50 | 10 | Half movement speed / doubled state waits, 1000 |
| Flame shield | 50 | 6 | Five orbiting effects, one damage, TTL 600/607/614/621/628 |
| Invisibility | 200 | 6 | Hidden from opponents, ends on attack/cast, 2000 |
| Polymorph | 200 | 10 | Stable object becomes neutral critter; orders/buffs cleared |
| Blizzard | 25 | 12 | Repeating five fields with eleven delayed shards each |
| Death coil | 100 | 10 | Split 50 damage over nearby organic enemies, heal caster at impact |
| Haste | 50 | 6 | Double movement speed / halved state waits, cancels slow, 1000 |
| Raise dead | 50 | 6 | Repeating corpse consumption, owned skeleton TTL 3600 |
| Whirlwind | 100 | 12 | Wandering damage effect, TTL 800 |
| Unholy armor | 100 | 6 | Sacrifice half current HP, invulnerability 500; volatile units die |
| Death and decay | 25 | 12 | Repeating five fields with eleven delayed effects each |
| Demolish | 0 | 1 | 400 damage in footprint range three, self destruction, clears forest |

Costs, ranges and status durations come from `scripts/spells.lua`. Mana
regeneration follows `action/actions.cpp`; the previously documented native
initial 85 overrides Wargus's 84. `spell_adjustvital.cpp` confirms mana per
restored/damaged hit point: this is not six mana for a whole heal.
`spell_spawnmissile.cpp` selects death-coil enemies in a 5x5 area, nearest
the caster first, distributes a total 50 damage, and gives the remainder
to the final target. `missile_deathcoil.cpp` returns the missile's damage
amount to a surviving caster. `spell_summon.cpp` searches a 3x3 square for
non-building corpses; an initial circular/organic-only interpretation was
corrected. Corpse consumption and summon expiry use normal thinker removal;
unholy armor cannot make a temporary summon permanent.

Runes can hurt their owner as well as allies (`CanHitOwner=true`); they
check at Sleep=5. Flame shield uses `missile_flameshield.cpp`'s 36 authored
circle positions and one hit every eight TTL cycles, excluding its bearer.
Whirlwind uses the reference TTL damage and 100-cycle direction-change
conditions, though its exact pixel-center selection remains a parity gap.
Blizzard/decay use five fields, eleven shards, delays 16/8, and synchronous
0–9 damage rolls. Retail RNG and exact area sampling are still untraced.
Effects honor target domains and invulnerability; manually cast healing and
buffs can affect valid enemies, as the reference's cast conditions permit.

Native spell sound IDs are SFXDAT 98–114 (`wartool.h` / `sound.lua`), plus
existing fireball/explosion samples. Impact sounds use 64 (fireball), 67
(arrow), and 31 (explosion). The sound regression now verifies 202 decoded
samples and the unchanged 35 voice groups.

### Research, production, oil and transports

The paid research table now covers all 48 priced technologies in the pinned
human/orc upgrade tables. Free baseline spell permissions are available on
the corresponding caster; the 24 distinct technology bits coexist with the
existing numerical weapon/armor tiers. Research transforms living archers,
axethrowers, knights and ogres in place and converts newly produced ones,
preserving object addresses. Numerical combat modifiers include the exact
hero/demolition/attack-peasant apply-to lists from the reference, rather than
silently restricting a research to regular infantry.

Training checks the authored dependencies (including both smith and mill
for siege engines, and stables/mound plus smith for cavalry). Stratagus
`action_train.cpp` checks food **at completed-unit release**, not at queue
purchase: the paid queue waits for supply, without banking negative elapsed
time to release later units instantly. Unfinished farms do not supply food.
`stratagus.lua` sets a 200-mobile-unit limit and 100% train/research/upgrade
refunds; these are implemented. The existing full construction refund is a
separately documented retail choice, not silently changed to Wargus's 75%.

**Disproven and corrected:** the old catalog note that treats
`Costs.time` as seconds describes our engine, not the pinned Stratagus
conversion. `action_train.cpp`, `action_research.cpp` and
`action_upgradeto.cpp` advance their counters after `Wait=30/6` plus the next
execution cycle; `action_built.cpp` uses cost*600 and progress 100 per cycle
at speed factor 100. A baseline cost unit is therefore roughly six cycles,
not thirty. Construction now counts those cycles directly: the cost-100
farm takes exactly 600 unassisted tics, with the 25% stage at tic 150.
Training, research and upgrade costs convert to 200 milliseconds per cost
unit at 30 Hz. Their shared production clock still rounds each tick to
milliseconds; exact reference first-cycle/retry phase remains a gap.
The construction regression now uses siege attackers: the previous three
grunts do not overcome the corrected farm growth before it completes.
This is an intentional time-basis correction, not a combat damage change.

PUD UNIT types 86/87 (platforms), 92 (mine) and 93 (oil patch) all use
`data*2500` reserves (`pud.cpp` lines 324–326). Tankers have resource-2
capacity 100, resource/depot waits 100, and use the common hidden-entry
state path. Platforms can only replace a live oil patch at its 3x3 origin;
a land worker cannot build one and a tanker cannot build land structures.
The deposit slot/remaining amount stays level-owned while its source id
changes from patch to platform or back. Cancellation/destruction restores
a patch with its remaining oil, per `BuildingRules.ontop` ReplaceOnDie /
ReplaceOnBuild. Exhaustion removes the source without restoring oil.
Owned completed shipyards/refineries accept the cargo; a completed refinery
raises oil income to 125. The completed platform's builder begins gathering.

Wargus `MaxOnBoard=6` and `CanTransport={LandUnit,only}` establish capacity
and passenger domain. This implementation currently boards the owner's own
units. Passengers remain individually allocated mobjs with carrier IDs,
stop revealing independent sight, are hidden/non-colliding while aboard,
and die if the carrier disappears. Unloading checks walkable, unoccupied
cells adjacent to the ship. A blocked passenger stays aboard; no arbitrary
teleport to a distant free cell is used. Transport and cast command fields
are serialized/checksummed along with research and combat RNG. Old save
layouts are rejected by the existing engine size/version checks.

### Reproduction and verification

Run all tests with `SDL_VIDEODRIVER=dummy`:

```sh
make
make build/bin/tests/warcraft-2/test_features
env SDL_VIDEODRIVER=dummy build/bin/tests/warcraft-2/test_features
env SDL_VIDEODRIVER=dummy make test-warcraft-2
env SDL_VIDEODRIVER=dummy build/bin/warcraft-2 --check
env SDL_VIDEODRIVER=dummy build/bin/warcraft-2 --screenshot /private/tmp/open-rts-warcraft-2.bmp
```

`test_features` checks fire thresholds and lifetime, paid repairs and
interruption, free construction assistance, delayed/surviving-shooter
projectiles, domain/detector rules,
research conversion/hero bonuses/cancellation, the 19 spell branches,
transport capacity/unloading/sinking, platform construction/restoration,
oil income, and all 28 native effect GRP frame counts. `test_save` checks
research, mana, buffs, cast/repair/carrier IDs and an in-flight projectile
alongside prior state. Catalog/research tests compare native unit metadata
and all priced reference research rows. HUD checks include the five native
16x16 status icons; sound tests decode the additional spell/impact samples.
The existing AI test runs ten simulated minutes. LAN-menu tests require
permission to bind local UDP sockets; their sandbox failure is independent
of the gameplay assertions and passes when socket access is allowed.

Verification on 2026-10-07: all 29 Warcraft II regression programs passed;
the ten-minute AI simulation made 24 purchases, launched four waves and
completed 13 harvest assignments. All five game binaries built, and the
Dark Colony/Dark Reign command regressions passed. AddressSanitizer and
UndefinedBehaviorSanitizer reported no errors in `test_features`. Headless
ALAMO smoke verification loaded 64 units, 2,798 resource vents, 372 terrain
tiles and 60 footman frames. Human/orc HUD captures and an in-game ALAMO
capture were visually checked. These checks cover the implementations
listed above, not the unimplemented features in the coverage matrix.

## Naval launch, coast and oil corrections (2026-10-08)

This is a comparison against the pinned Wargus and Stratagus sources plus
native asset decoding, not a new WAR2.EXE trace. The executable fingerprint
above is unchanged; the retail program was not executed. Source revisions:
Wargus `cde1a0718a0058cc651ecd56ff8149fc39f624e9`, Stratagus
`3d87c93f7fd8c0b62ee1be5df0a6d9efc72ca6cc`.

Native input SHA-256:

- `data/WAR2/CHANNEL.PUD`:
  `ae6bc5015d50225b096700049e0fd613611985775dc70759f3a2f96f15bd5389`.
- `data/WAR2/DATA/MAINDAT.WAR`:
  `791bae4480d564f017122a82c9481dabd952424151f2b5d20245793e654ad3bb`.

### Confirmed reference rules and reproduced defects

- Wargus `doc/pud-specs.txt`, section 14: SQM `0x0002` and `0x0082`
  mean coast; `0x0040` means water. **Disproven:** testing `0x80` first
  does not identify forest: it turns `0x0082` shoreline into harvestable
  trees, while `0x0002` becomes land. Coast now has a separate terrain
  class and creates no lumber deposits. `test_pud` compares every coast
  entry in CHANNEL against the loaded terrain and resource table.
  Its land/water counts are 3,857/3,609. SQM's shared forest/mountain
  encoding remains a separate classification limitation.
- Stratagus `src/unit/unittype.cpp::UpdateUnitStats`: normal naval units
  cannot enter coast or land; transports can enter water and coast.
  Transport movement now has its own terrain-speed row. Air can cross coast.
- `src/unit/build.cpp::HasAtLeastOneCoastTile` and `CanBuildHere`, together
  with the shore-building movement mask, require water/coast under a shore
  building, including at least one coast cell. **Disproven:** an entirely
  land footprint beside water is not the reference shipyard placement rule.
  Human/orc shipyards, foundries and refineries now use the coast rule.
  The shipyard/refinery `BuildingRules` in each faction's `units.lua` also
  require footprint distance greater than three from patches/platforms.
  Boundary, land-only, open-water-only, occupied and too-close sites are rejected.
- **Reproduced engine defect:** shared production used `L_IsWalkable`, the
  land-only blocked grid. The new naval fixture logged a tanker launched at
  `(8.5,8.5)` on land while rejecting `(13.5,5.5)` water with movement speed
  100. Production now tests the spawned unit's movement class through
  `L_MoveSpeed`. Ordinary allocated mobjs, ownership, collision occupancy
  and deferred removal remain on the existing Doom lifecycle.
- Wargus human/orc `anim.lua`: destroyers fire in frame 0, wait 119+1 cycles;
  battleship/juggernaught wait 127+102+1. Their rows 1 and 2 (native operands
  5 and 10) are sinking poses, with waits 50 and 50+1. **Disproven:** these
  rows are surface-ship attack art. Both factions now fire using frame 0
  and sink through those two poses. Submarine/turtle attack poses are
  1,2,2,1,0, with waits 10,25,25,25,29+1 and the projectile at the third
  pose. They use otherwise unused death-state slots for the recovery poses.
  This corrects the reference pose/timing sequence; it does not claim exact
  retail movement speed, bobbing or side-attack turning.

### Native oil and construction presentation

Wargus `wartool.h` maps empty tankers to MAINDAT 59/60 and loaded tankers to
126/127. Each loaded GRP has three logical rows and five stored facings
(decoded into eight rotations). Carrying tankers now use that shared native
sheet for standing/travel, returning to the empty sheet after unloading.
There are no generated or PNG runtime assets.

`anim.lua::animations-oil-platform` uses frame 0 while idle and frame 2 when
`ResourceActive >= 1`. A five-tic state action now selects the matching pose
by checking live mining tankers against the platform's level-owned vent.
Construction and pumping are distinct states; unfinished platforms reject
harvest orders. Wargus faction `constructions.lua` chooses construction
frames 0/1 at 0/25%, then the building's main frame 1 at 50%.

The native construction entries now loaded for both factions are:

| Kind | Summer/wasteland | Winter | Swamp |
|---|---|---|---|
| Shipyard | 253/254 | 263/264 | 253/254 |
| Oil well | 255/256 summer, 271/272 wasteland | 265/266 | 271/272 |
| Refinery | 257/258 | 267/268 | 257/258 |
| Foundry | 259/260 | 269/270 | 259/260 |

**Confirmed asset correction:** entry 263, the winter human shipyard site,
has three frames; the other listed construction entries have two. Requiring
exactly two frames was an invalid test assumption. Both authored construction
stages exist and the extra native frame is retained. Platforms have three
frames in all four loaded eras. Shore buildings now leave water rubble,
matching their Wargus `Corpse` definitions even though their domain is land.
The visual contact sheet also shows the existing swamp platform art fallback
has blue water whereas its wasteland construction sheet has dark water.
That fallback is preserved; no palette compensation was introduced.

### Verification and remaining limits

`test_naval` exercises both factions' paid launch, water-only tanker movement,
platform construction and automatic gathering, pumping/loaded poses, the
100-unit cargo cycle, unfinished/completed refinery bonuses, final partial
cargo and depletion reopening the footprint. It trains transports,
destroyers, capital ships and submarines, checks oil spending, hits enemy
ships, and checks sinking. Its coast fixture constructs an actual shipyard
from a land worker and boards/unloads a transport at the coast. Existing
`test_features` checks destruction/cancellation restores the patch and
remaining reserves, and sinking kills passengers.

Reproduce with:

```sh
make -s build/bin/tests/warcraft-2/test_naval build/bin/tests/warcraft-2/test_pud
env SDL_VIDEODRIVER=dummy build/bin/tests/warcraft-2/test_naval /private/tmp/war2-naval.bmp
env SDL_VIDEODRIVER=dummy build/bin/tests/warcraft-2/test_pud
env SDL_VIDEODRIVER=dummy make test-warcraft-2
env SDL_VIDEODRIVER=dummy build/bin/warcraft-2 --check
env SDL_VIDEODRIVER=dummy build/bin/warcraft-2 data/WAR2 CHANNEL.PUD --screenshot /private/tmp/war2-channel.bmp
```

The optional contact sheet has eight rows (summer, winter, wasteland, swamp;
human then orc), and six columns (empty, loaded, construction 0, construction
1, idle, pumping). All four eras' construction/carrier assets are decoded
and checked even without a screenshot path. This and the CHANNEL gameplay
capture were visually inspected. Temporary investigation logging was removed.
The new state-table size makes older saves fail the existing signature check;
new saves use the existing serialization without a parallel oil owner.

This supersedes the prior audit's loaded-tanker, platform-pumping and native
naval-construction-art gaps. Naval AI economy, automatic inland-click landing
selection, exact rectangular ship collision/pathing and full DOS retail
movement/animation cadence remain unverified or unimplemented. Wargus source
behavior is not presented as independently verified DOS executable behavior.

Final verification: all 30 Warcraft II regression programs passed (the two
LAN-menu programs require local sockets outside the sandbox). `make` built
all five game binaries. Dark Reign/7th Legion production regressions and
Dark Colony's native Barracks release regression passed. ALAMO and CHANNEL
headless checks passed with 64/17 units and 2,798/1,394 resource vents.
