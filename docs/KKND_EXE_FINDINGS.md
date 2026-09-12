# KKnD loader evidence

## Representation cleanup (2026-09-09)

**Confirmed by asset-loader comparison to `46f826a`.** All 47 installed LVL map
candidates and MOBD members 0–99 match, including rejected candidates. There
are 44 successful maps and 57 successful MOBD members. Every map comparison
includes the terrain-atlas pixels as well as map metadata and initial mobjs.
Default screenshots are byte-identical. This task did not inspect KKND.EXE and
makes no new executable-address or retail-behavior claim. See
[loader verification](LOADER_REFACTOR_VERIFICATION.md) for commands and scope.

Representative SHA-256 fingerprints:

- `data/KKND/LEVELS/640/SPRITES.LVL`:
  `3e7dbe10624c706afd963e18f54f780052e6ee0b415fc8008add6e37de669ae6`.
- `data/KKND/LEVELS/640/SURV_01.LVL`:
  `66243cfff0f49e22684be6b7074a4bdf71d370f54e22a96d025b0fdcb7af85f7`.

The existing MAPD decoder reads the layer count, layer-offset table, palette
count and palette, then LRCS dimensions and tile offsets. It still skips the
four-byte Gen1 tile prefix and treats index zero as transparent only on upper
layers. Atlas frame numbering, including the gap before the first upper layer,
is unchanged. Palette conversion now writes the final terrain atlas directly;
upper-layer visibility is accumulated from those same pixels while decoding.
There is no need to retain expanded per-layer pixel images or scan them again.

MOBD animation ordering still follows the existing timing/offset traversal.
Only frame offsets are retained until cell allocation; each image then decodes
directly into its own texture. TRPS bit 0 still mirrors the decoded image.
Displacement remains `(width/2,height/2) - authored offset`. Raw and RLE images
retain index-zero transparency. Oversized transparent RLE runs now reject like
oversized literal runs; a fixture covers this malformed-input correction.

`MUTE_07.LVL`, `SPRITES.LVL`, and `SUPSPR.LVL` remain rejected as map candidates.
The manifest deliberately includes non-map containers and unsupported MOBD
members; these failures are not interpreted as corrupt retail assets. Animation
selection limits, member-name aliases, and the retail meaning of unsupported
members remain **unverified here** and were not changed.

## Runtime sprite catalog and native animation channels (2026-09-12)

**Confirmed from assets**, using the SPRITES.LVL fingerprint above. No executable
was examined. `R_InitSprites` previously returned success without loading any
actor sheets, so the interactive renderer reused its fallback rifleman sheet.
It now loads each actor's catalog member once and binds the common sprite table;
subsequent calls retain existing textures when production introduces new types.

The native pointer table before the first image record has seven channels of
sixteen 32-bit animation offsets (448 bytes). Zero slots retain their position.
Compacting nonzero offsets and grouping the result into blocks of sixteen is
**disproven**: it combines different channels on sparse actors. Limiting the
result to four groups also loses authored frames. Complete sixteen-direction
sets now share logical temporal frames; sparse channels retain each sequence
individually. Image pixels, TRPS flip flags, and authored displacement formulas
are unchanged. No missing poses are synthesized.

Two reproducing cases (offsets relative to the MOBD segment):

- Survivor mobile Derrick, member 65: member at 4132641, pointer table at
  4133117, first image record at 4133565. Channel 4 contains sixteen one-frame
  idle sequences, channel 5 one simple frame, channel 6 sixteen two-frame move
  sequences. Logical movement starts at 2, corresponding to flat frame 17.
- Dire Wolf, member 19: member at 1661004, pointer table at 1662396, first image
  at 1662844. Sparse channel 3 contains separate 16- and 24-frame sequences;
  channel 4 has sixteen idle poses, channel 5 sixteen five-frame attacks, and
  channel 6 sixteen seven-frame moves. Logical idle/attack/move starts are
  40/41/46 (flat indices 40/56/136). Treating sparse channel 3 as one rotational
  sequence with maximum length 24 loses sixteen frames; that intermediate
  hypothesis was rejected. The resulting definition has 53 logical frames.

Pinned OpenKrush sequence definitions corroborate those movement/attack starts
and the following building idle selections. Native channel 4 begins at the
listed logical frame; the preceding simple frames include construction art.
Selecting frame zero displayed flags or incomplete buildings.

| Building | Member | Idle frame |
|---|---:|---:|
| Survivor drill rig | 75 | 7 |
| Survivor power station | 74 | 5 |
| Survivor outpost | 52 | 170 |
| Survivor machine shop | 37 | 5 |
| Survivor repair bay | 56 | 20 |
| Survivor research lab | 57 | 5 |
| Evolved drill rig | 50 | 5 |
| Evolved power station | 49 | 5 |
| Evolved clan hall | 13 | 132 |
| Evolved blacksmith | 8 | 5 |
| Evolved beast enclosure | 3 | 5 |
| Evolved menagerie | 42 | 1 |
| Evolved alchemy hall | 0 | 5 |

**Remaining limitations:** tower bases and turrets are distinct native
sequences. OpenKrush's base idle flat indices are guard tower 16, missile
battery 26, cannon tower 64, machinegun nest 16, grapeshot 32, rotary cannon 80;
their normalized indices are 1, 11, 4, 1, 2, 5 respectively. Changing the
single current state to those bases alone would discard the turret, so tower
composition remains unresolved. Air members 82/83 do not use channel 4 for
idle. OpenKrush contains asset-specific missing-frame patches and presentation
offsets; none were adopted. Retail timing and animation dispatch remain
**unknown**; sequence names are corroboration from that pinned port, not a new
executable trace.

Reproduce with `make kknd-info` and
`env SDL_VIDEODRIVER=dummy make test-kknd`. The runtime sprite regression loads
all 57 catalog sheets, checks distinct textures and cache retention, verifies
Derrick/Wolf frame counts and corrected idle selections, and checks every
generated state's frame bounds. `build/bin/kknd --screenshot /private/tmp/kknd.bmp`
under the same dummy video driver verifies initial building presentation.
