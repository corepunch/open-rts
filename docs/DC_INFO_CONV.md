# Dark Colony FIN/SPR inspection

Build with `make dc-info-conv`. `build/dc_info_conv` replaces `dc_info_gen` and
writes JSON to stdout for inspection, with errors on stderr and a nonzero exit
status for invalid files, missing labels, and out-of-range indices.

```sh
# List exact FIN labels, inclusive ranges, and dependencies.
build/dc_info_conv --labels data/DCOLONY/ANIMATE/TRSC.FIN

# Inspect every frame/command in one exact label, or one native frame.
build/dc_info_conv --label TRSCBLOODA0 data/DCOLONY/ANIMATE/TRSC.FIN
build/dc_info_conv --frame 313 data/DCOLONY/ANIMATE/TRSC.FIN

# Inspect SPR header, palette, and one cell's size and displacement.
build/dc_info_conv --cell 0 data/DCOLONY/SPRITES/BLOO.SPR

# Without a selector, print all FIN frames or all SPR cell descriptors.
build/dc_info_conv data/DCOLONY/SPRITES/BLOO.SPR

# Retain the former generator's multigen-style raw state export.
build/dc_info_conv --states data/DCOLONY/ANIMATE/TRSC.FIN
make dark-colony-info
```

FIN output preserves the header word, dependency names, exact labels, inclusive
frame ranges, raw delay, converted native delay, all 160 unknown frame bytes
(as hex), and every command's sprite, cell, signed XY offset, remap, intensity,
layer, and flags. `remap` is the native drawing mode; it is not the object's
team translation. SPR output preserves header flags, declared payload size,
all 256 raw palette entries, and unsigned cell dimensions/displacements. It
does not decode image pixels; `dc_spr_extract` remains the atlas-export tool.

The inspector accepts the retail 22-byte FIN command layout used by the game.
Unsupported older FIN layouts are reported, not reinterpreted as retail records.
`--states` preserves raw durations and placeholder actions/terminal states; it
does not regenerate gameplay actions or the complete hand-authored `info.c`.

The damage-state reference catalog can be regenerated deterministically:

```sh
build/dc_info_conv --blood-states data/DCOLONY > games/dark-colony/blood_states.inc
python3 tests/tools/test_dc_info_conv.py
```

This scans FIN labels globally, retaining all 208 BLOOD-labelled sequences,
including labels the native A–G/numeric-suffix lookup cannot select. The catalog
contains state references and delays; command layers/pixels remain owned by the
native asset loader. Its three consumers build enum IDs, ordinary `state_t`
rows, and the exact-label lookup from the same source. Runtime selection uses
the GAMESTAT type prefix, independently of the FIN/SPR filename. Each hit spawns
an ordinary mobj with copied position/facing/team; it does not follow the unit.

The catalog follows DC.EXE's mode-1 startup advance and byte timer semantics.
Native delay is `floor(((raw ? raw : 15) + 3) * 15 / 100)`; the default 66 ms
native tick is converted to cumulative 30 Hz boundaries. See
[the executable findings](DC_EXE_FINDINGS.md#native-damage-channel-implementation-2026-09-09)
for instruction addresses and the correction to earlier timing notes.

Human city damage and death references use the retail ANIM.DAT load list:

```sh
make dc-info-conv
python3 tools/dc_building_states.py > games/dark-colony/building_states.inc
```

This exports the seven human city types' SCRCH/BURN/DIE labels from BURN,
BURN2 and HUBU, including complete fire/explosion frames. The index excludes
BURN3's obsolete duplicate Barracks death label. Runtime health selection
follows DC.EXE's signed 11/16 and 5/16 HP comparisons; see the building damage
findings for the counterintuitive BURN/SCRCH ordering and main-channel timing.
