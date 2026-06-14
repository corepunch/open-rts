# 008-dark-colony-r2ghidra-integration.md

## Goal

Reverse engineer DC.EXE using r2/r2ghidra, extract specific functions, rename
them to match our semantics, and build a compatibility/glue layer that maps
DC's Windows/DirectX/DirectPlay calls to SDL/POSIX equivalents.

## What We Have

- `common/` — headless game model (actors, map, pathfinding, traits)
- `client/` — SDL windowing, rendering, input, camera, UI
- `plugins/DarkColony/` — DC actor types, object pool (`DcObject` at 0xdc
  bytes), object state tables, sprite loading
- `REFERENCES.md` — FIN/SPR format specs, GAMESTAT.TXT layout, rendering rules
- `data/DCOLONY/DC.EXE` — the binary to reverse engineer
- `reverse/` — gitignored, ready for r2/r2ghidra output

## What's Missing

- r2/r2ghidra decompilation dumps (not yet generated)
- Function-level extraction workflow from the big decompiled C file
- Glue layer for DC's Windows/DirectX/DirectPlay calls
- Menu/UI rendering matching DC's pixel-perfect behavior
- Scenario/scenario script parsing (trigger.c, trigger.c references)

---

## Step 1 — Generate r2/r2ghidra output

Run the documented procedure to produce the big C file and function list:

```sh
mkdir -p reverse/dc-exe-r2ghidra
r2 -q -e bin.cache=true -A -c "afl" -c q data/DCOLONY/DC.EXE \
  > reverse/dc-exe-r2ghidra/functions.txt
r2 -q -e bin.cache=true -A -c "pdg @@F" -c q data/DCOLONY/DC.EXE \
  > reverse/dc-exe-r2ghidra/dc_exe.c
```

Also generate reference dumps for key strings and xrefs:

```sh
r2 -q -e bin.cache=true -A -c "iz~draw" -c q data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -A -c "axt @ 0x00400000" -c q data/DCOLONY/DC.EXE
```

Verify the file list in `reverse/dc-exe-r2ghidra/functions.txt` against the EXE
source file names from REFERENCES.md:

```
animate.c  engmain.c  juicel.c  mobiles.c  objects.c  vobj.c
sprite.c   sprites.c  collide.c  path.c     ai.c       trigger.c
```

---

## Step 2 — Function extraction strategy

The decompiled C file will be large (likely 50k+ lines). We need a way to
extract individual functions as we navigate them. **Two approaches, used together:**

### 2a — r2 extraction (preferred for initial extraction)

r2 can extract individual functions by address. This avoids manually parsing
the big C file:

```sh
# Extract a single function by address
r2 -q -e bin.cache=true -A data/DCOLONY/DC.EXE -c "pdf @ 0x0043a144" -c q

# Extract multiple functions at once (from functions.txt)
while read addr name; do
  r2 -q -e bin.cache=true -A data/DCOLONY/DC.EXE -c "pdf @ $addr" -c q
done < reverse/dc-exe-r2ghidra/functions.txt > reverse/dc-exe-r2ghidra/all_functions.c
```

This produces raw r2 `pdf` output (not decompiled), but is fast and precise.

### 2b — r2ghidra extraction (preferred for readable C)

For functions we care about, use `pdg` (decompiler) on specific addresses:

```sh
# Single function decompilation with renamed parameters
r2 -q -e bin.cache=true -A data/DCOLONY/DC.EXE \
  -c "pdg @ 0x0043a144" -c q

# Batch: extract functions matching a name pattern
r2 -q -e bin.cache=true -A data/DCOLONY/DC.EXE \
  -c "pdg @fcn.animate.0043xxxx" -c q
```

### 2c — Custom extraction tool (for repeated use)

Write a simple tool (`tools/func_extract.c`) that:
- Reads `functions.txt` (address + function name pairs)
- Takes a list of function names to extract
- Outputs the raw r2 commands to extract those functions
- Optionally merges with the `pdg` decompiled output

This tool would be a thin wrapper around r2, invoked as:

```sh
# Extract specific functions we've identified
./build/bin/func_extract 0043a144 0043b200 0043c000 \
  > reverse/dc-exe-r2ghidra/selected_functions.c
```

**Recommendation:** Start with 2a/2b (r2 commands). If you find yourself
extracting the same functions repeatedly, write the tool in step 4.

---

## Step 3 — Rename functions, variables, structs

We rename in layers, from raw r2 output to our semantics:

### Layer 1: r2 internal names → EXE source file names

The `functions.txt` file has r2 names like `fcn.0043a144`. Map these to the
EXE source file names from REFERENCES.md (`animate.c`, `objects.c`, etc.)

### Layer 2: EXE source file names → semantic names

Read the r2ghidra C output and rename:

1. **Function names:** `sub_0043a144` → `Animate_UpdateFrame`
2. **Struct names:** `struct_0043b000` → `DcObject` (already done in our codebase)
3. **Local variables:** `var_10` → `frame_index`, `var_14` → `facing_code`

### Layer 3: Cross-reference with our data structures

Use the offsets we already know (`DcObject` at 0xdc bytes, field offsets) to
verify that renamed functions access the right fields.

### Workflow:

```
1. Read reverse/dc-exe-r2ghidra/dc_exe.c
2. Search for function names matching EXE source files (animate, objects, etc.)
3. For each function:
   a. Read the decompiled C
   b. Identify what it does (update, draw, spawn, etc.)
   c. Map its parameters/locals to our struct fields
   d. Rename and adapt into a file in common/ or plugins/DarkColony/
4. Compile and verify against headless tests
```

### Search strategy:

```sh
# Find functions from a specific EXE source file
rg "animate.c|objects.c|engmain.c" reverse/dc-exe-r2ghidra/dc_exe.c

# Find references to DcObject-like structures (0xdc byte accesses)
rg "0xdc|0x2c|0xcd|0xce" reverse/dc-exe-r2ghidra/

# Find known state keywords in strings
rg "MOVE|STAND|DIEA|BUILDSTAND|BURN" reverse/dc-exe-r2ghidra/
```

---

## Step 4 — Build the glue layer

DC.EXE was compiled for Windows/DOS with DirectX and DirectPlay. We need a
compatibility layer that maps those calls to SDL/POSIX equivalents.

### 4a — Identify DC's API surface

From the EXE strings and imported symbols, identify what DC calls:

```sh
# Check imported DLLs
r2 -q -e bin.cache=true -A data/DCOLONY/DC.EXE -c "izq" -c q
r2 -q -e bin.cache=true -A data/DCOLONY/DC.EXE -c "aflj" -c q data/DCOLONY/DC.EXE
```

Likely imports (DOS-era):
- `kernel32.dll` — CreateFile, ReadFile, WriteFile, GetTickCount, etc.
- `user32.dll` — GetDC, BitBlt, CreateCompatibleDC, etc.
- `gdi32.dll` — SelectObject, CreatePalette, SetPaletteEntries, etc.
- `dxtrans.dll` / `d3d.dll` — DirectDraw/Direct3D (if any)
- `dplayx.dll` — DirectPlay (networking)
- `mmsystem.dll` — Sound (wave/midi playback)

### 4b — Create the glue layer

Create `client/glue/` directory with:

```
client/glue/
├── glue_windows.c    — POSIX replacements for Windows API calls DC uses
├── glue_windows.h    — Public API (what DC.EXE calls)
├── glue_directx.c    — DirectDraw/BitBlt → SDL_Renderer mapping
├── glue_directplay.c — DirectPlay → socket/UDP (multiplayer, if needed)
├── glue_input.c      — DirectInput → SDL event polling
├── glue_mmio.c       — mciSendString → SDL_mixer / audio playback
└── glue_resources.c  — Resource loading (icons, cursors, strings)
```

### 4c — Integration approach

There are two ways to integrate the glue layer:

**Approach A — Compile DC.EXE with our glue (preferred for 1:1 matching):**
- Compile DC.EXE source (if available) or patch DC.EXE to use our DLL
- Not applicable if we don't have source — DC.EXE stays binary

**Approach B — Hook/intercept DC.EXE calls (practical for binary RE):**
- We re-implement the *behavior* DC.EXE had, in our own code
- We don't call DC.EXE at all — we replicate its logic from the r2ghidra output
- This is what we're already doing (DcObject struct, actor tables, etc.)

**Approach B is what we do.** The "glue layer" is really just our re-implementation
of DC.EXE's behavior, written in C, calling SDL instead of Windows APIs.

### 4d — Menu/UI rendering (simpler case)

Menu drawing in DC is likely handled by `INTRFACE/` sprite files. This is
already partially handled by our `client/main.c` and `engine_core.c` text
rendering. To match DC's menus 1:1:

1. Load `INTRFACE/` SPR/FIN files (already supported by SPR loader)
2. Map menu button states to FIN frames (DC uses state keywords like
   `MOVE`, `STAND`, `DEPLOY` — menus likely use `STAND` frames)
3. Render with our SDL renderer, matching DC's palette and positioning

This is straightforward because:
- We already load SPR files with palettes
- We already render FIN-based animations
- We just need the right button frame mappings from `DEPEND.TXT`/`MAINE`

---

## Step 5 — Proof-read DC code against our implementation

For each behavior we implement, cross-reference with the r2ghidra output:

### 5a — Map loading (engmain.c / objects.c)

```sh
# Find map loading functions
rg "LoadMap|LoadScenario|LoadMapData" reverse/dc-exe-r2ghidra/dc_exe.c

# Find object spawning
rg "PutUnitAt|AddThingAt|AddBuildingAt" reverse/dc-exe-r2ghidra/dc_exe.c
```

Verify:
- [ ] Map dimensions match (width/height from .MAP file)
- [ ] Starting units placed at correct coordinates (bottom-up, matching DC)
- [ ] Starting buildings placed correctly (rows 16-22)
- [ ] Petra-7 vents spawn with correct resource rates

### 5b — Sprite rendering (sprite.c / sprites.c / animate.c)

```sh
# Find rendering functions
rg "Draw|Render|Blit|DrawObject" reverse/dc-exe-r2ghidra/dc_exe.c

# Find animation state machine
rg "Animate|UpdateFrame|StateMachine" reverse/dc-exe-r2ghidra/dc_exe.c
```

Verify:
- [ ] 8/16 direction handling matches DC (frame_loop_index + direction_index * 8)
- [ ] Health-based color overlays match DC (not our replacement shapes)
- [ ] Selection cursor (pentagon) renders at correct position
- [ ] Multi-part sprites (buildings) render correctly
- [ ] FIN command offsets produce correct sprite layout

### 5c — Unit logic (mobiles.c / ai.c)

```sh
# Find movement and AI
rg "Move|Path|AI|UpdateUnit" reverse/dc-exe-r2ghidra/dc_exe.c

# Find collision detection
rg "Collide|Collision|CheckOverlap" reverse/dc-exe-r2ghidra/dc_exe.c
```

Verify:
- [ ] Movement speed matches DC (8.8 fixed point)
- [ ] A* pathfinding grid matches DC's walkability rules
- [ ] Collision detection uses same overlap logic
- [ ] AI state machine transitions match DC (MOVE → STAND → DIEA, etc.)

### 5d — Game logic loop (engmain.c)

```sh
# Find the main game loop
rg "GameLoop|MainLoop|GameMain|RunGame" reverse/dc-exe-r2ghidra/dc_exe.c
```

Verify:
- [ ] Tick rate matches DC (likely 30Hz based on FIXED_DT)
- [ ] Input polling matches DC's keyboard/mouse handling
- [ ] Game state transitions (menu → game → game over) match DC

---

## Step 6 — Write the plan document (this file)

Update plan checkboxes as implementation progresses.

---

## File structure for r2 integration

```
reverse/dc-exe-r2ghidra/
├── functions.txt       — r2 function list (address + name)
├── dc_exe.c           — Full decompiled output (pdg @@F)
├── functions/          — Extracted individual functions (by our naming)
│   ├── animate.c       — Animation state machine
│   ├── objects.c       — Object management (spawn, update, destroy)
│   ├── mobiles.c       — Unit movement and AI
│   ├── sprite.c        — Sprite rendering (blitting)
│   ├── sprites.c       — Sprite sheet management
│   ├── collide.c       — Collision detection
│   ├── path.c          — Pathfinding
│   ├── ai.c            — AI state machine
│   ├── trigger.c       — Scenario triggers
│   ├── vobj.c          — Visual object rendering
│   ├── juicel.c        — FIN file loading
│   └── engmain.c       — Main game loop
└── notes/              — Reverse engineering notes per function
    ├── animate_notes.md
    ├── objects_notes.md
    └── ...

tools/
└── func_extract.c      — Tool to extract specific functions from DC.EXE
```

## Tools to build

### tools/func_extract.c

A simple CLI tool that:
1. Reads `reverse/dc-exe-r2ghidra/functions.txt`
2. Takes function names or addresses as arguments
3. Outputs extracted C code (from r2 or r2ghidra)
4. Optionally renames parameters/locals based on a mapping file

Usage:
```sh
# Extract specific functions by name
./build/bin/func_extract animate_UpdateFrame objects_SpawnUnit > selected.c

# Extract by address
./build/bin/func_extract 0043a144 0043b200 > selected.c

# Generate extraction commands for r2
./build/bin/func_extract --r2 0043a144 0043b200 > r2_commands.txt
```

## Timeline

| Phase | Task | Estimated effort |
|-------|------|-----------------|
| 1 | Generate r2/r2ghidra dumps | 1 hour |
| 2 | Identify and extract key functions | 2-4 hours |
| 3 | Rename and map to our structs | 4-8 hours |
| 4 | Build glue layer (Windows API → SDL) | 4-8 hours |
| 5 | Menu/UI rendering matching DC | 2-4 hours |
| 6 | Proof-read map loading, sprites, AI | 8-16 hours |
| 7 | Write func_extract tool | 2-4 hours |
| 8 | Update plans with progress | Ongoing |

Total: ~25-45 hours of reverse engineering work.

## Success criteria

- [ ] r2/r2ghidra output generated and browsable
- [ ] Key functions (animate, objects, mobiles, sprite, engmain) extracted and renamed
- [ ] Glue layer compiles and links
- [ ] Menu UI renders pixel-identical to DC.EXE
- [ ] Map loading produces identical starting state to DC.EXE
- [ ] Unit movement and AI match DC.EXE behavior
- [ ] Sprite rendering (health overlays, selection, multi-part) matches DC.EXE
- [ ] Headless tests pass for all verified behaviors
