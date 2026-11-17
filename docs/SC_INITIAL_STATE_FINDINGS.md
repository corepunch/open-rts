# StarCraft initial cloak and burrow state

## Confirmed

- PyMS `CHKSectionUNIT.py` defines `CLOAK = 1 << 0` and `BURROW = 1 << 1`; its decoder reads `validAbilities` and `abilityStates` as separate 16-bit fields. `CHKSectionUPRP.py` serializes the same fields in the same 20-byte property record. The runtime applies an initial state only when the matching valid and state bits are both set. `In Transit` is bit 2 in both formats and is intentionally not interpreted here.
- OpenBW at the repository's pinned `bwgame.h` commit sets the Burrowed and passively-cloaked status together (`set_unit_burrowed`, lines 3330–34). Its energy update drains cloak/detector energy only when the unit is not passively cloaked (lines 1117–23). Therefore a burrowed spellcaster must not lose energy to cloak upkeep.
- `w_assets.c::animation()` returns the final compiled state. For Burrow, this is the authored hold state: the chain's last state points to itself and has one tic (lines 278–80). `sc_burrow_states[][0]` remains the animation head for actual Burrow orders; initial Burrow uses the returned final state instead. `R_InitSprites()` preserves that state after loading graphics.
- The focused test `tests/starcraft/test_initial_states.c` exercises UNIT cloak/burrow, a trigger-created UPRP-burrowed Defiler, the distinct head/final IDs for both units, passive cloak ticking, and save/restore of status flags and traits.

## Residual: manual toggle without research (superseded by "Retail trace" below)

OpenBW's `can_use_tech` check for `TechTypes::Burrowing` requires Burrowing researched for ordinary units, with exceptions for heroes and Lurkers (`bwgame.h`, lines 17879–82). It also has a separate `unburrow_unit()` path used by automatic reactions, which does not repeat that research check (lines 12906–11). These facts do not establish whether a player can manually unburrow or decloak a unit whose initial state came from UNIT/UPRP while the technology is unavailable. The implementation leaves the existing manual command gate unchanged pending a direct native trace or a narrower OpenBW command-path proof.

## Manual toggle gate (issue 99, item 4): earlier OpenBW-only notes (superseded below)

Pinned OpenBW checks that decide whether a unit may start Burrow or Cloak
(`unit_can_use_tech`): `u_completed`, not hallucinated, not disabled, tech
available to the player, then per tech: Burrowing needs the research unless the
unit is a hero or a Lurker, and the unit must be one of Lurker, Drone, Infested
Terran, Defiler, Zergling, Hydralisk or the Unclean One, Devouring One and Hunter
Killer heroes; Cloaking Field and Personnel Cloaking need the research unless the
unit is a hero, and the unit must be a Wraith or Ghost respectively. No check reads
the unit's current burrowed or cloaked state, its initial CHK state or its energy:
energy is paid by the cloak upkeep in `update_unit_energy`, which skips passively
cloaked and burrowed units. `unburrow_unit` (automatic reaction to damage) does
not repeat the research check.

Not in the pinned files: the player command handlers that turn a Burrow, Unburrow,
Cloak or Decloak click into an order. They (not `unit_can_use_tech`) decide whether
an Unburrow or Decloak command for a unit that has no research is accepted, and
whether a CHK-granted state counts as owned. `unit_can_receive_order` lets only
`Unburrowing` through for a burrowed unit, but `unit_order_allowed` has no entry for
it, so that function alone cannot be the gate. The local `sc_cast` requires the
research (or hero status) for both directions of both toggles and charges cloak
energy. Without a retail command trace or the OpenBW action source the local gate
is kept: changing it either way would be a guess. The trace to take: issue
Unburrow and Decloak on a trigger-created unit with no research and observe whether
the command card offers and executes them.

## Manual toggle gate, second search (batch two): OpenBW has no command layer (superseded below)

Searched `/tmp/openbw-bwgame.h`, `openbw-game_types.h` and `openbw-data_types.h`
for burrow, unburrow, cloak and decloak by name, by `Orders::` id and by command
wording (`action_`, `user_action`, `issue_order`, `unit_can_receive_order`,
`unit_order_allowed`, `set_queued_order`).

What exists in the pinned source:

- Order functions `order_Burrowing`, `order_Burrowed`, `order_Unburrowing`;
  secondary orders `secondary_order_Cloak` (cloak) and `secondary_order_Decloak`
  (just sets the secondary order to Nothing). None checks research or CHK state.
- `unburrow_unit` (hit reaction), the energy-out decloak in `update_unit_energy`,
  and `give_unit_to`, which on an ownership change grants the new owner the
  techs of each unit type (Marine/Firebat Stim, Ghost Lockdown and Personnel
  Cloaking, Wraith Cloaking Field, Zergling/Hydralisk/Drone/Defiler/Infested
  Terran Burrowing, and so on), so a given or rescued unit can use its tech.
- `unit_can_receive_order`: a burrowed non-Lurker accepts only `Unburrowing`.
  `unit_order_allowed` has no case for Burrowing, Unburrowing, Cloak or Decloak.
  `set_queued_order` raises `error("cloak fixme")` for a Cloak order, so Cloak
  never comes through that path.
- `unit_can_use_tech` has only three callers in the pinned files (Healing,
  `order_SpiderMine`); the research test for Burrowing and the two cloaks is
  reached by nothing that turns a click into an order.

What is absent: every player-command handler that creates a `Burrowing`,
`Unburrowing`, `Cloak` or `Decloak` order (`grep Orders::Decloak` finds only the
secondary-order dispatch). The pinned files contain no action/command layer at
all. Whether a unit that starts cloaked or burrowed from CHK state without the
research may decloak/unburrow and re-toggle therefore cannot be decided from this
source, so the gate is unchanged and no gate test was added (a test could only
pin the current local behavior, not retail).

Lurker: the exemption in `unit_can_use_tech` cannot be aligned because the
Lurker is a Brood War unit this port does not have (no `MT_LURKER`); hero
exemption already exists in `sc_unit_has_tech`.

Not done, and what would settle it: the OpenBW action source (not pinned) for
the Burrow/Unburrow/Cloak/Decloak commands, or a retail trace of Unburrow and
Decloak on a trigger-created unit with no research. A separate, evidence-backed
gap is that this port does not grant a unit type's techs on an ownership change
(`give_unit_to`); that belongs with the trigger Give Units action.

## Retail trace of the four toggle commands (batch three): settled

Source: `data/STARCRAFT/install/files/starcraft.exe` (sha256 `16cd8f0d...b2f5`, base
0x400000). Method: `r2 -q -e scr.color=0 -e asm.bytes=false -e bin.relocs.apply=true
-c "pD 0xd5000 @ 0x401000"` to a text file, then grep. Order ids come from PyMS
`Orders.txt` (line-1): Cloak 0x6d, Decloak 0x6e, Burrow 0x74, Burrowed 0x75, Unburrow 0x76.

**Command dispatcher 0x46d148** (ecx = stream, edx = length; called per player slot
from the loop at 0x46ce03, which sets the commanding player in `[0x4e49b0]` and the
selection owner in `[0x4e49b4]`). Switch on `byte - 5`, byte index table 0x46d7bc,
jump table 0x46d6e4, each case checks the remaining length, then `call`s the handler:
0x21 Cloak -> 0x46a7f0, 0x22 Decloak -> 0x46a8d0, 0x2c Burrow -> 0x46a600,
0x2d Unburrow -> 0x46a710 (each 2 bytes: id, queued flag). Senders: 0x46a5e0 (0x2c),
0x46a6f0 (0x2d), 0x46a7d0 (0x21), 0x46a8b0 (0x22). 0x14/0x15 are at 0x46d33d/0x46d325.

**Common gate `CanUseTech` 0x4019f0** (ecx = tech, edx = player, stack = unit; returns
1, else stores a reason in `[0x67fed0]` via 0x4a9c70). In order: tech < 0x18; unit owner
== player (`[unit+0x4c]`); status Completed; not Disabled (`[unit+0xdc]` 0x400) and
timers `[unit+0x10f]`, `[unit+0x111]` zero (lockdown, stasis by the usual CUnit layout);
not Hallucination (0x40000000); tech "available" byte `0x4fdb3c[player*24+tech]` (or
cheat flag `[0x50a100]` 0x1000); then the requirement script (opcode interpreter 0x401280,
scripts at 0x4db4f8). Burrowing (tech 11) script: group 1 = unit type is Hero
(type flag 0x40) OR researched (`0x4fdc5c[player*24+tech]`, or the cheat flag); group 2 =
unit type in {Zergling, Hydralisk, Drone, Defiler, Infested Terran, Unclean One,
Hunter Killer, Devouring One}. Cloak techs: same group 1, whitelist Ghost-type or
Wraith-type heroes and units. No check reads energy, the current burrowed/cloaked state or
CHK origin. Failure only stores the reason (the message routine 0x482780 is a bare `ret`).

**Handlers** (all iterate the commanding player's selection 0x491d30 / 0x491cc0):
- Burrow 0x46a600: type flag Burrowable (0x100000), `CanUseTech(11)`; skipped if status
  Burrowed (0x10) or order already 0x74; else `0x425370` issues order 0x74 (queued by flag).
- Unburrow 0x46a710: Burrowable, `CanUseTech(11)` again (research or hero required), then
  0x438c70, which issues 0x76 only if the order is 0x74 or 0x75.
- Cloak 0x46a7f0: tech from 0x4305f0 (Ghost 1/Kerrigan 0x10/0x33 -> 10; Wraith 8/0x15 -> 9;
  else 0x18, which fails), `CanUseTech`, then 0x42fe70: no-op if already cloaked (status
  0x100); energy < cost aborts (equal passes), else deducts and sets secondary 0x6d.
- Decloak 0x46a8d0: same `CanUseTech`, then secondary order 0x6e via 0x4254e0.

**Answer.** A player without the research can neither unburrow nor decloak a CHK-cloaked
or CHK-burrowed unit by command, and cannot re-toggle it; heroes can without research.
Disabled (lockdown/stasis) units can do none of the four. Cloak costs energy (strict
`<`), decloak, burrow and unburrow cost none. Computer players: AI code calls the same
0x4019f0 with tech 11 before its own burrow order (0x4845ec..0x484600), so the research
or hero rule applies to them too; there is no separate player-type check.

**Order handlers (item 2).** Primary order dispatch is 0x434e50 (3 nested switches;
final byte table 0x435768, table 0x4355a8): 0x74 -> 0x438ab0, 0x75 -> 0x438c40,
0x76 -> 0x438c90. None of them calls 0x4019f0: Burrowing finishes by setting status
0x810 (burrowed + passive cloak), secondary 0x6d and order 0x75; Unburrowing clears it.
Orders 0x6d/0x6e are secondary orders, not primary-dispatched. So research is enforced
only when the command (or AI) is accepted, never at order execution.

**Command-card conditions** (button records 20 bytes: position, icon, condition fn,
action fn, condition var, action var, strings; Burrow at 0x4e5d24, Unburrow 0x4e5d38,
Cloak 0x4e729c / 0x4e74fc, Decloak 0x4e72b0 / 0x4e7510). Burrow 0x495fa0: any selected
Burrowable unit that passes `CanUseTech(11)` and is not burrowed. Unburrow 0x496010:
every selected Burrowable unit is burrowed and passes `CanUseTech(11)`. Cloak 0x495c30:
a selected unit passes `CanUseTech`, is not passively cloaked (0x800) and bit 0x100 is
clear. Decloak 0x495ca0: all pass `CanUseTech`, none passive, bit 0x100 set. (Corrected
below: an unresearched button is greyed, not hidden.)

**Engine.** `sc_cast` already required research or hero for both directions of both
toggles and charged cloak energy with the same boundary. The one divergence fixed: a
disabled (lockdown/stasis) unit could still toggle cloak (Burrow already refused). Tests:
`tests/starcraft/test_initial_states.c` (unresearched Ghost and Zergling stuck, researched
toggles, lockdown, energy 24/25, free decloak, hero without research).

**Command card, settled (issue 99).** Read again in starcraft.exe:

- Values. The card builder at 0x497640 calls each record's condition (`call [edi+4]`)
  for its slot: 0 skips the record (hidden; the next record may take the slot), a
  positive value enables the button, a negative one greys it (0x4cbbc0) and its
  second string becomes the tooltip (Burrow: "Burrow: Evolve at Hatchery", Wraith
  Cloak: "Cloaking Field: Research at Control Tower").
- `CanUseTech` 0x4019f0 returns 0 for another player's unit (error 1), an unfinished
  one (0x14), a disabled one (0xa: status 0x400, timers +0x10f/+0x111), a
  hallucination (0x16) or a tech the map withholds (0x4fdb3c, the per-player
  availability table, error 2), all before research is looked at. Research is the
  requirement list at 0x4db4f8, run by 0x401280: Burrowing, Personnel Cloaking and
  Cloaking Field all begin `0xff1c OR 0xff0f` (units.dat flag 0x40 Hero, or the
  researched table 0x4fdc5c that 0x4360f2 sets when research ends), then the caster
  types. A failed group with nothing yet matched returns -1 (error 8). So an
  unresearched Burrow or Cloak is GREYED, as `m_menu.c` already did; the earlier
  "hides" reading took error 2 (availability) for research.
- Multiple units. Burrow 0x495fa0 walks the selection, skips types that cannot
  burrow, and the first burrowable unit decides: `CanUseTech` other than 1 is
  returned as is (greyed or hidden), a unit up returns 1, a unit already down passes
  to the next; none left is 0. Unburrow 0x496010: every burrowable unit is down and
  passes, else 0 (never greyed). Both sit at position 9 and Burrow, listed first,
  wins when not hidden; so a burrowed unit without the research shows Burrow greyed.
  Cloak 0x495c30 and Decloak 0x495ca0 (position 7) follow the same pattern over every
  selected unit.
- Mixed selections (0x4977a0). The active unit's set is kept when every selected
  unit's set is the same, shares its record array (Zergling and Hydralisk: 0x4e5e58)
  or is the other's fallback; otherwise set 0xf2 (Move, Stop, Attack, Patrol, Hold),
  replaced by 0xf3 when all are workers (flag 0x8), 0xf4 when all can cloak (flag
  0x200: Cloak 0x495d00, Decloak 0x495d60, each unit's own tech via 0x4305f0; a unit
  failing `CanUseTech` is skipped rather than deciding), or 0xf5 when all are Zerg
  (group flag 1) and not airborne (status 0x4) and one can burrow (the Zergling
  record array).
- The actions (OpenBW `action_burrow`, `action_cloak` and reverses) act on each
  selected unit they apply to.

Implemented in `games/starcraft/m_menu.c` (`can_use_tech`, `burrow_button`,
`cloak_button`, `toggle`): Burrow/Unburrow and Cloak/Decloak take their state and
lock from the whole selection by the rules above, retail icons (259/260, 252/253) and
hotkeys (U, C from stat_txt), and a press goes only to the units on the button's
side; sets 0xf4 and 0xf5 for mixed selections. `p_units.c` maps a cloak order to
each unit's own cloak tech. Tests: `toggle_cards` in `tests/starcraft/test_gameplay.c`.

**Residual unknowns.** (1) Passive cloaking (status 0x800, an Arbiter's field) is not
modelled locally, so it never blocks Cloak or Decloak. (2) Set 0xf3 (mixed workers)
and the shared-record rule for other types (Marine and Firebat Stim, for one) are not
modelled: a mixed selection still shows only movement buttons plus 0xf4/0xf5. (3)
Button positions: the local card lays abilities out in order, not at the retail
positions (Burrow 9, Cloak 7) except on the mixed cards. (4) The exact role of the flag
that `0xff02` "current unit is" clears (it reads the unit-count table 0x4f5afc and
seems to turn a later failed group from -1 into a skip) was not traced; it does not
change the result for these casters, whose research group runs first. (5) Cheat flag 0x50a100 bit 0x1000/0x800 meaning not identified.
(4) Timers +0x10f/+0x111 named by community CUnit layout, not verified here.
(5) Lurker is absent from this executable's whitelist (it predates Brood War).
Commands: `make build/bin/starcraft`; `TEST_JOBS=4 TEST_BUDGET=60 make -o starcraft-maps
test-starcraft` (33 tests; test_multiplayer failed once with "Address already in use" from a
concurrent worktree and passed on rerun).
