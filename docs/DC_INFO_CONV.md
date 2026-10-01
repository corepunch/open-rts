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

## Gameplay states

Gameplay states are authored by hand in `games/dark-colony/info.c`; there is no
state generator, `tools/dc_states.txt`, or `animate/*.inc` any more. One row is
a run of consecutive frames:

```c
/* sprite, first logical frame, frames, tics per frame, action, next state, group, tics list */
[S_TRSC_RUN1] = { SPR_TRSC, 225, 8, 3, A_Chase, S_TRSC_RUN1, 2, NULL },
[S_TRSC_ATK1] = { SPR_TRSC, 297, 2, 2, NULL, S_TRSC_ATK3, 3, NULL },
[S_TRSC_ATK3] = { SPR_TRSC, 299, 1, 2, A_Attack, S_TRSC_ATK4, 3, NULL },
[S_REAP_RUN1] = { SPR_REAP, 124, 8, 0, A_Chase, S_REAP_RUN1, 2, TICS(4,3,3,4,1,3,3,1) },
```

The action runs on entering every frame of the run, so a frame with its own
action (the Trooper's firing frame above) is its own row. A row's name keeps
the number of its first frame. Frames are engine logical FIN indices (raw SPR
cell count plus native FIN frame), not image pixels or per-direction metadata.
Add the state's name to `statenum_t` in `info.h` when adding a row.

Native BLOOD, SCRCH, BURN and DIE label families are one row each, named
`S_<LABEL>_<first FIN frame>`. `p_blood.c` maps exact labels to their rows and
enters them at frame 1 (`P_SetMobjStateFrame`), as the native channel skips its
reset frame; `dc_building_sequences` in `info.c` names each human building's
SCRCH/BURN/DIE row. Building families follow ANIM.DAT's load set, where the
requested labels are unique.

Native family timing: delay is the low byte of
`floor(((raw ? raw : 15) + 3) * 15 / 100)`, zero underflows to 256 ticks,
and cumulative 66 ms boundaries are rounded to 30 Hz, which is why many rows
carry a `TICS(...)` list instead of one duration. Blood's skipped reset
frame and building death's initial one-tick presentation remain separate,
explicit timing policies. See [the native findings](DC_EXE_FINDINGS.md).
Use `build/dc_info_conv --label` to read a label's frames and delays when
authoring a row.
