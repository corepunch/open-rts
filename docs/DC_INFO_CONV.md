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

## Gameplay state export

```sh
make dark-colony-states
python3 tools/dc_states.py --check
make test-dc-info-conv
```

`tools/dc_states.txt` is the authored gameplay input, following multigen's
`state sprite frame tics action nextstate` column order with a final group.
It has explicit action names; FIN assets never supply C action functions.
For example:

```text
S_TRSC_RUN{1..8}  TRSC 225..232 3                 A_Chase             S_TRSC_RUN1 2
S_REAP_RUN{1..8}  REAP 124..131 4,3,3,4,1,3,3,1   A_Chase             S_REAP_RUN1 2
S_BRRKPOD_STND    HUBU 38       5                 A_DC_BuildingStand  S_BRRKPOD_STND_2 1
```

Numbered names and integer ranges expand inclusively. A scalar frame/tic repeats;
comma-separated values preserve authored timing. Intermediate states chain to
the following expanded state, and the last takes the explicit `nextstate`.
Frames are engine logical FIN indices (raw SPR cell count plus native FIN frame),
not image pixels or per-direction metadata. The explicitly held Exploiter WORK
pose and Reaper timing remain authored policy, not inferred from label names.

`tools/dc_states.py` contains the small `FAMILY_RULES` table for native BLOOD,
SCRCH, BURN and DIE families: action, group, terminal behavior and timing policy.
`A_DC_BuildingStand` is explicit for SCRCH/BURN; BLOOD/DIE use NULL. The
zero-tic production completion action is explicit in `TERMINAL_STATES`.
These entries explain where custom actions get assigned; their implementations
remain ordinary handwritten `void action(mobj_t *)` functions.

The exporter writes raw designated initializers to
`games/dark-colony/animate/<FIN stem>.inc`, included directly by `info.c`.
All 2,719 existing state IDs retain their values; designated initializers permit
per-FIN grouping without renumbering. `info.h` receives ordinary enum entries.
No state/label expansion macros are involved. The raw exact-label lookup is emitted directly into `p_blood.c`; the raw
SCRCH/BURN/DIE range table is emitted directly into `info.c` and shared by
building logic and native-metadata verification. Neither needs a separate include.

The blood catalog still retains all 208 BLOOD-labelled sequences from supported
FIN files, including labels the native A–G/facing lookup cannot select.
Building families follow ANIM.DAT's load set, where the requested labels are
unique. Other unsupported FIN layouts are reported and skipped as in the former
blood exporter. The inspector validates file spans and label ranges.
The two former macro exporters (`--blood-states`, `dc_building_states.py`) and
their macro-based output files have been removed.

Native family timing is unchanged: delay is the low byte of
`floor(((raw ? raw : 15) + 3) * 15 / 100)`, zero underflows to 256 ticks,
and cumulative 66 ms boundaries are rounded to 30 Hz. Blood's skipped reset
frame and building death's initial one-tick presentation remain separate,
explicit timing policies. See [the native findings](DC_EXE_FINDINGS.md).

Regenerate after editing the authored table or family rules; `--check` makes
stale output a failure. `info.c` still owns `sprnames[]` and `mobjinfo[]`;
the exporter replaces its state-array and building-range blocks, `info.h`'s
state enum, and `p_blood.c`'s label-table block.
