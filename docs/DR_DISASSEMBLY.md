# DKREIGN.EXE disassembly documents

Documented September 30, 2026. Runtime baseline: `047477a`.
DK means the retail `dkreign.exe`; document names use the repository's existing
`DR_` prefix for Dark Reign. This is the counterpart to the DC.EXE document
set, with the same evidence labels and focused reproduction commands.

## Document map

| Dark Colony document | Dark Reign counterpart | Purpose |
|---|---|---|
| [DC_EXE_FINDINGS.md](DC_EXE_FINDINGS.md) | [DR_EXE_FINDINGS.md](DR_EXE_FINDINGS.md) | Dated findings, addresses, formulas, corrections and provenance |
| [DC_UNIT_BEHAVIORS.md](DC_UNIT_BEHAVIORS.md) | [DR_UNIT_BEHAVIORS.md](DR_UNIT_BEHAVIORS.md) | Native contracts compared with implemented unit behavior |
| [DC_DROPSHIP_ANIMATION.md](DC_DROPSHIP_ANIMATION.md) | [DR_TRANSPORTER_ANIMATION.md](DR_TRANSPORTER_ANIMATION.md) | Detailed verified transport animation and cargo lifecycle |
| [DC_ARCHITECTURE.md](DC_ARCHITECTURE.md) | [DR_ARCHITECTURE.md](DR_ARCHITECTURE.md) | Current code boundaries, ownership and native lookup paths |
| [DC_DEVELOPMENT_STATUS.md](DC_DEVELOPMENT_STATUS.md) | [DR_DEVELOPMENT_STATUS.md](DR_DEVELOPMENT_STATUS.md) | Verification handoff and remaining work |
| [DC_INFO_CONV.md](DC_INFO_CONV.md) | [DR_INFO_GEN.md](DR_INFO_GEN.md) | Existing native inspection/state-generation workflow |
| HUD sections in DC executable findings | [DR_HUD_DISASSEMBLY.md](DR_HUD_DISASSEMBLY.md) | Native image tables, layout, fonts, palette, radar and gauges |

The transporter report covers a verified Dark Reign subsystem rather than
inventing a Dark Reign equivalent of DC's dropship. The generator report
documents existing C tools; there is no `dr_info_conv` executable.

## Executable fingerprint

| Property | Value |
|---|---|
| File | `data/REIGN/dkreign.exe` |
| SHA-256 | `3e089777cea09b0fa7cb772c72c871677594515f3508baa04fc13d4dad84a965` |
| Size | 2,478,592 bytes |
| Format | PE32, little-endian, i386, Windows GUI |
| Image base | `0x00400000` |
| PE timestamp | `0x340c98ca` (September 2, 1997) |
| Linker version fields | 4.20 |
| Section count | 7 |
| Entry-point RVA | `0x000ccb20` |
| Entry-point VA | `0x004ccb20` |

**Confirmed:** file hash and PE fields, checked with `shasum`, `rabin2 -I`
and `rabin2 -H`. **Unknown:** exact compiler version. A linker-version field
and rabin2's generic `cdecl` label do not establish every function's ABI.
Existing focused routines use ECX/EDX as well as stack arguments; check call
sites before trusting r2ghidra parameter names. Missing Smacker DLL SDB warnings
are tool metadata failures, not evidence that the PE code cannot be inspected.

## Evidence labels

- **Confirmed:** instruction-level evidence, native data, or inspected engine
  code. Each claim says which of these supports it.
- **Inferred:** an interpretation supported by evidence but not a traced
  complete retail algorithm.
- **Disproven/superseded:** retain the correction so a plausible wrong approach
  is not repeated.
- **Unknown/unported:** no complete verified rule or implementation yet.

The dated findings report retains the detailed evidence chain. These topic
documents reorganize that work; they are not a new complete disassembly or
proof of full gameplay fidelity. Full decompiler output remains ignored under
`reverse/`, following [REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md).

## Controlling functions and data

Names below describe the observed role; they are not recovered retail symbols.

| Address | Observed role | Report |
|---|---|---|
| `0x00445c90` | Unit definition parser, movement and capability fields | [Units](DR_UNIT_BEHAVIORS.md) |
| `0x00483e70` | Weapon definition parser, human/nonhuman classifiers | [Units](DR_UNIT_BEHAVIORS.md) |
| `0x004a04e5` | `SetBay` comparison inside building parser | [Transport](DR_TRANSPORTER_ANIMATION.md) |
| `0x004a056c..0x004a059c` | Packed building-bay coordinate stores | [Transport](DR_TRANSPORTER_ANIMATION.md) |
| `0x0042ca50` | Overlay footprint effect/altitude parsing | [Transport](DR_TRANSPORTER_ANIMATION.md) |
| `0x0049bbb0` | Build resource route from authored bays | [Transport](DR_TRANSPORTER_ANIMATION.md) |
| `0x00422550` | Resolve building by exact bay cell | [Transport](DR_TRANSPORTER_ANIMATION.md) |
| `0x004baac0` | Order dispatcher, including resource order 6 | [Transport](DR_TRANSPORTER_ANIMATION.md) |
| `0x0049c010` | Face, animate and transfer resource batches | [Transport](DR_TRANSPORTER_ANIMATION.md) |
| `0x004a8920` | Start sprite animation section | [Transport](DR_TRANSPORTER_ANIMATION.md) |
| `0x004a96f0` | Advance section cursor and detect completion | [Transport](DR_TRANSPORTER_ANIMATION.md) |
| `0x004a8e00` | Turn a part along the shorter direction | [Transport](DR_TRANSPORTER_ANIMATION.md) |
| `0x004957b0` / `0x00495660` | HUD descriptor loading / dimensions | [HUD](DR_HUD_DISASSEMBLY.md) |
| `0x005be94c` | 29 HUD image descriptors, stride 12 | [HUD](DR_HUD_DISASSEMBLY.md) |
| `0x004947b0` / `0x00494d70` | HUD page / static-frame drawing | [HUD](DR_HUD_DISASSEMBLY.md) |
| `0x0048d900` / `0x0048dd30` | Unit / building production slot drawing | [HUD](DR_HUD_DISASSEMBLY.md) |
| `0x00469530` | Font delimiter-row decoding | [HUD](DR_HUD_DISASSEMBLY.md) |
| `0x0048b030` / `0x0048ad10` | Palette loading / unavailable-red translation | [HUD](DR_HUD_DISASSEMBLY.md) |
| `0x0048fac0` / `0x0047ad10` | Radar setup / terrain colors | [HUD](DR_HUD_DISASSEMBLY.md) |
| `0x0048f340` | Resource-gauge smoothing and drawing | [HUD](DR_HUD_DISASSEMBLY.md) |
| `0x00472480` | AIP parameter/debug references | [Findings](DR_EXE_FINDINGS.md#executable-evidence) |

## Reproduce focused disassembly

Run from the repository root. Known entry points can be defined directly;
full analysis is not necessary for every small instruction window.

```sh
shasum -a 256 data/REIGN/dkreign.exe
rabin2 -I data/REIGN/dkreign.exe
rabin2 -H data/REIGN/dkreign.exe
r2 -q -e bin.cache=true -e scr.color=0 \
  -c 'af @ 0x445c90' -c 'pdf @ 0x445c90' -c q data/REIGN/dkreign.exe
r2 -q -e bin.cache=true -e scr.color=0 \
  -c 'af @ 0x49c010' -c 'pdf @ 0x49c010' -c q data/REIGN/dkreign.exe
```

Broad discovery already exists at `reverse/dr-hud/dkreign.c`. If regenerating
it, keep new discovery output under the ignored directory:

```sh
mkdir -p reverse/dr-exe-r2ghidra
r2 -q -e bin.cache=true -A -c afl -c q data/REIGN/dkreign.exe \
  > reverse/dr-exe-r2ghidra/functions.txt
r2 -q -e bin.cache=true -A -c 'pdg @@F' -c q data/REIGN/dkreign.exe \
  > reverse/dr-exe-r2ghidra/dkreign.c
```

Promote useful results into the dated findings report and link the relevant
topic document. Never commit the full generated C/disassembly dump.
