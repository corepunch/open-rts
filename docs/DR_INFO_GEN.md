# Dark Reign native inspection and state generation

Documented September 30, 2026. This is the existing C-tool counterpart to
[DC_INFO_CONV.md](DC_INFO_CONV.md). Dark Reign does not use DC FIN metadata,
and there is no `dr_info_conv` inspector. Its native unit animation sections
live in SPR assets, with type/weapon/part declarations in retail DEFTXT.

## Existing generator

[tools/dr_info_gen.c](../tools/dr_info_gen.c) generates the Doom-style sprite,
state and type tables and validates authored body asset names against retail
`deftxt/UNITS.TXT` and `deftxt/BUILD.TXT`.

```sh
make dark-reign-info
# Equivalent direct invocation after building the generator:
build/dr_info_gen data/REIGN/dark \
  games/dark-reign/info.h games/dark-reign/info.c
```

| Output | Content |
|---|---|
| `games/dark-reign/info.h` | Regenerated state enum between `BEGIN_GENERATED_STATENUM` / `END_GENERATED_STATENUM` markers; surrounding declarations preserved |
| `games/dark-reign/info.c` | `sprnames[]`, `states[]`, `mobjinfo[]`, `game_info` |

`states[]` is written inline, one row per run of frames (see
[ARCHITECTURE.md](../ARCHITECTURE.md)). A state's action runs on every frame of
its run, so a cycle whose first frame calls `A_Chase` or `A_Attack` is two rows:
`S_*_RUN1` for that frame and `S_*_RUN2` for the rest. `tools/info_gen.h`
writes the rows and derives the enum from them; `make test-info-gen` checks the
committed files are current.

Authored mapping and animation inputs live in the C generator's `entries[]`.
For a missing header, the generator writes a complete template with sprite/type
declarations too. Normal regeneration of an existing header replaces only the
marked state enum; adding catalog types must also keep its surrounding enums
consistent with the generator entries.
OpenDR sequence metadata is pinned to
`98079a904746440433795fe7f21c4b35eb6b3959`; source paths/provenance are in
[REFERENCES.md](../REFERENCES.md#dark-reign). Native asset names remain checked
against the retail definitions. State actions are authored C functions, not
functions extracted from sprite pixels.

## Animation and capability contract

State IDs select sprite/frame, tics, action, next state and gameplay group.
Known OpenDR animation starts use `Start/Facings` as the logical step.
Shared assets can have distinct faction/type sprite and state IDs through
`MOBILE_ASSET`/`BUILDING_ASSET`; this does not authorize guessed native aliases.

**Verified special case:** resource transfer sections use retail fixed-point
timing, with a 15-frame one-shot chain lasting 51 tics. The generator computes
successive ceiling differences from step 19456; see
[DR_TRANSPORTER_ANIMATION.md](DR_TRANSPORTER_ANIMATION.md#facing-and-section-timing).

**Fidelity limit:** other animation mappings and timing helpers are authored
from the pinned sequence reference and existing engine conversion, not a
complete instruction-level port of every native part animation. Some mappings
are explicitly marked approximate in the generator. Do not interpret successful
generation as proof that all frames, parts or special abilities are native.

Runtime balance/traits in [g_game.c](../games/dark-reign/g_game.c) remain
authoritative for ordinary spawning. `P_ApplyActorTypeDefaults()` can override
generated type defaults. The audit therefore checks flight, human, harvesting
and NoAutoTarget in both representations. Medic/Mechanic/Karoch support flags
also cause appropriate generated state-entry actions; rigs have no invented
attack actions. The full capability gap is listed in
[DR_UNIT_BEHAVIORS.md](DR_UNIT_BEHAVIORS.md).

## Native definition inspection

DEFTXT files contain legacy bytes and CRLF. Use `LC_ALL=C` for text-processing
tools that otherwise reject their encoding.

```sh
LC_ALL=C rg -n 'DefineUnitType|SetType|SetMoveMode|IsHuman|NoAutoTarget' \
  data/REIGN/dark/deftxt/UNITS.TXT
LC_ALL=C rg -n 'SetBay|SetBuildingImages' data/REIGN/dark/deftxt/BUILD.TXT
LC_ALL=C rg -n 'DefineWeapon|SetAttributes|SetOffense|CanOnlyShoot' \
  data/REIGN/dark/deftxt/WEAPON.TXT
```

The native-loading implementation is [w_spr.c](../games/dark-reign/w_spr.c).
The existing [loader catalog](../tests/loader_catalog.c) can inspect rendered
loader output; consult [tests/README.md](../tests/README.md) and Makefile targets
for its supported invocation rather than assuming DC inspector switches apply.

## Verify generated output and behavior

```sh
make test-info-gen
make build/bin/tests/dark-reign/test_unit_traits
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-reign/test_unit_traits
env SDL_VIDEODRIVER=dummy make test-dark-reign
```

`test-info-gen` regenerates tables into `build/info-check/` and compares them
with committed output. On this machine, `/usr/local/bin/cmp` has an incompatible
architecture; `env PATH=/usr/bin:/bin:/opt/homebrew/bin make test-info-gen`
selects the working system comparison tool. This is an environment workaround,
not a generator or native-format rule.

Generator, extraction and inspection tools must remain C. Do not introduce
Python generators, duplicate runtime asset tables, guessed frame metadata or
PNG assets to fill an unresolved native-data gap.
