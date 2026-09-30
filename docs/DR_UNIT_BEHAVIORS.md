# Dark Reign unit behavior comparison

Audit date: September 30, 2026. Engine baseline: `047477a`.
The [disassembly index](DR_DISASSEMBLY.md) records the executable fingerprint;
[DR_EXE_FINDINGS.md](DR_EXE_FINDINGS.md#runtime-capability-and-hud-audit-2026-09-30)
retains the detailed investigation. No recorded retail gameplay session is
claimed by this comparison.

## Capability parsing

**Confirmed from instructions:** `0x00445c90` parses unit definitions. Movement
is stored at unit type `+0xf0`, and capability bits at `+0x53c`.

| Native input | Native value/bit | Decisive instruction | Engine interpretation |
|---|---|---|---|
| `SetMoveMode(Fly)` | Movement 0 | `0x004462d3` | `MF_FLY` |
| `SetMoveMode(Hover)` | Movement 1 | `0x004462f6` | Ground-bound; hover terrain semantics incomplete |
| `SetMoveMode(Fixed)` | Movement 2 | `0x00446319` | Stationary actor/attachment |
| `SetMoveMode(Ground)` | Movement 3 | `0x00446339` | Ordinary ground movement |
| `SetMoveMode(Tunnel)` | Movement 4 | `0x00446359` | Tunnel semantics unported |
| `IsHuman()` | Capability `0x100` | `0x00446935`, store `0x0044693a` | `MF_HUMAN` |
| `NoAutoTarget()` | Capability `0x40000` | `0x00446f80`, store `0x00446f85` | `MF_NOAUTOTARGET` |
| `CanSpy(a b c)` | Capability `0x10`; args at `+0x560/+0x564/+0x568` | `0x00446b3c..0x00446b58` | Spying unported |

These native values are not the numeric values of the engine's trait enum.
The earlier report's human-bit address `0x00446b3c` was wrong; that instruction
belongs to `CanSpy`. The correct human branch compares the string at
`0x00446901` and ORs `0x100` at `0x00446935`.

Focused, freshly checked instruction excerpts:

```asm
; Unit type in EBX; movement and capability fields
004462d3  mov dword [ebx + 0xf0], 0
0044692f  mov eax, dword [ebx + 0x53c]
00446935  or eax, 0x100
0044693a  mov dword [ebx + 0x53c], eax
00446f7a  mov eax, dword [ebx + 0x53c]
00446f80  or eax, 0x40000
00446f85  mov dword [ebx + 0x53c], eax
```

## Comparison by unit family

**Confirmed native data** below means `deftxt/UNITS.TXT` and `WEAPON.TXT`;
**implemented** means inspected C and focused tests, not complete retail parity.

| Unit/family | Native contract | Current implementation | Remaining work |
|---|---|---|---|
| FG/IMP Construction Rig, native 11/1005 | Ground; no `AddWeapon` | Move and produce structures; no offensive trait or action | Complete native construction lifecycle |
| Freighter / Hover Freighter, 13/14/1006/1007 | Two resource records; authored source/destination pairs; 135-degree section-1 docking | Exact bay, turning, one-shot batch transfer, cargo preservation for both factions | Native stock/economy and local traffic arbitration |
| Sky Bike / Outrider / Recon Drone / Cyclone / Sky Fortress | `SetMoveMode(Fly)` | Actual spawned actors retain `MF_FLY`; Recon Drone stays unarmed | Flight speed/height, full weapon and rearming rules |
| Sniper and other `NoAutoTarget` units | Disable automatic acquisition | Automatic fallback scans disabled; assigned valid target still works | Retail retaliation/order distinction |
| Field Medic / Karoch | MedicHeal; human-only ground target; range 1; strength -20; delay 10 cycles | Trait-driven allied healing, cap, minimum cooldown, ordinary state actions | Native projectile, defense factors, support-order search and boost cancellation |
| Mechanic | MechanicRepair; nonhuman ground unit; range 1; strength -5; delay 10 cycles | Trait-driven allied repair; excludes humans, aircraft and buildings | Exact projectile/defense factors and support-order dispatch |
| Raider / Mercenary / Phase Tank | `CanPhase(30 25 ft1 0)` | Ordinary movement and authored weapons | Phasing lifecycle, destination eligibility and orders |
| Sniper / Scout / Saboteur | Morph into overlay `electric_blue_explosion`, argument 10 | Ordinary body/state rendering | Concealment, overlay transformation and cancellation |
| FG/IMP Infiltrator | Morph into unit; `CanSpy(300 1200 500)` | Ordinary selectable human actor | Disguise, building entry, discovery and lockout |
| Saboteur | `CanSabotage(120 1 0)` | Ordinary selectable human actor | Sabotage dispatch, target eligibility and lifecycle |
| RAT / Invader / civilian convoy | `SetCarry(5 4)` | Ordinary transport body can move | Passenger storage, boarding, exit and destruction |
| Phase Runner | `SetCarry(5 50)`, `CanBoomerang()` | Ordinary movement/body | Tunnel transport and return lifecycle |
| Amper | `CanBoost()`, booster `(200 200 0.5 1)` | Selectable support body | Boost acquisition, expiration, fractional fade and deadly outcome |
| Hostage Taker | `SetCarry(1 2)`, `CanGrab(IMPSuicideZombie 300)` | Ordinary vehicle movement/body | Capture, conversion, passenger ownership and release |
| SCARAB / SCARAB decoy | Alternate to native shield type 1026; parameters 2/0 | Ordinary authored actor/state | Alternate type and shield lifecycle |
| Sky Fortress / Temporal Rift | `ChargeWeapon(700)` / `ChargeWeapon(5000)` | Fortress basic attack exists; internal Rift weapon unmapped | Charge state, discharge and target rules |
| Martyr / Suicide Zombie | Suicide weapon definitions | Basic authored attack/state | Native self-destruction and area damage |
| Remaining infantry, vehicles and towers | Authored weapons, target classes, armor and parts | Basic attack/state machinery for mapped armed actors | Complete projectile, targeting, armor and multipart audit |

## Support path and its limits

**Confirmed from instructions:** weapon parser `0x00483e70` writes human-only
at `+0xa1` and nonhuman-only at `+0xa2`:

```asm
0048433b  mov byte [ebx + 0xa1], 1
004843d7  mov byte [ebx + 0xa2], 1
```

**Confirmed engine behavior:** `weapon_target()` and `P_Attack()` in
[play/p_mobj.c](../play/p_mobj.c) restore allied health using negative authored
damage. `MF_HEAL` requires `MF_HUMAN`; `MF_REPAIR` requires its absence.
Both require a living, damaged, mobile ground recipient and cap at max HP.
Support does not become an offensive `MF_ATTACK` capability.

**Inferred integration:** instantaneous restoration and automatic nearby target
search reproduce the basic support capability, not the complete retail
projectile/armor pipeline. Ten cycles become a minimum 334 ms cooldown in the
existing 30 Hz engine timer contract. Generated animation lengths can space
pulses farther apart; exact retail cadence remains unknown/unported.

## Catalog coverage

The C audit matches **65 non-training unit definitions** to runtime actor
types. It verifies flight, human, harvesting and manual targeting in both the
authored actor table and generated `mobjinfo[]` flags. Actual aircraft spawning
is checked too. A passing audit does not certify every special ability.

Six native definitions remain unmapped:

| Native ID | Definition | Known role |
|---|---|---|
| 4100 | `FGAntiAirSite` | Attached anti-air weapon, `bfaarsp0.spr` |
| 4101 | `FGGuardTower` | Attached guard weapon, `bfgdtsp0.spr` |
| 4102 | `FGAdvancedGuardTower` | Attached advanced weapon, `bfagtsp0.spr` |
| 1026 | `IMPShieldedSPA` | SCARAB alternate, `uiiarac0.spr` |
| 11103 | `IMPriftCreator` | Charged Rift attachment, `bitrcsp0.spr` |
| 3006 | `CameraTower` | Common camera attachment, `bccamsp0.spr` |

**Disproven:** the earlier claim that all 71 non-T units were represented.
Similarly named building actors do not replace their internal attachment
contracts. T-prefix training definitions and expansion rosters are separate
coverage questions.

## Verification and reproduction

```sh
r2 -q -e bin.cache=true -c 'af @ 0x445c90' \
  -c 'pdf @ 0x445c90' -c q data/REIGN/dkreign.exe
r2 -q -e bin.cache=true -c 'pd 4 @ 0x44692f' \
  -c 'pd 8 @ 0x446b3c' -c q data/REIGN/dkreign.exe
r2 -q -e bin.cache=true -c 'pd 2 @ 0x48433b' \
  -c 'pd 2 @ 0x4843d7' -c q data/REIGN/dkreign.exe
make build/bin/tests/dark-reign/test_unit_traits
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-reign/test_unit_traits
env SDL_VIDEODRIVER=dummy make test-dark-reign
```

The focused tests establish recipient classes, pulse amounts, cooldown, cap,
enemy/full-health/aircraft rejection, Karoch thinker dispatch, unarmed rigs,
flight defaults and assigned versus automatic sniper targeting. The separate
[transport report](DR_TRANSPORTER_ANIMATION.md) records its stricter animation
and conservation checks.
