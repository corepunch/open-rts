# Zod / Stargus implementation handover

Checkpoint: 2026-11-12, committed code through `bb88cd9`. Work continued after
this checkpoint; inspect `git status`, recent commits and current tests before
relying on any pending-item description below.

## Request and working agreement

The user wants missing usable gameplay and AI from Zod and Stargus implemented,
not merely a comparison report. They explicitly authorized orchestration with
GPT-6.1 Terra agents. They briefly requested a cheaper-agent handover, then
asked to continue using Terra 6.1 agents as much as possible. Keep the parent
focused on integration and independent review. Do not declare complete while
known applicable reference mechanics remain unresolved.

Read `AGENTS.md`, `REVERSE_ENGINEERING.md`, `REFERENCES.md`, and
`docs/REFERENCE_IMPLEMENTATION_COVERAGE.md`. Native executable/data evidence
takes priority over conflicting remake policy. No copied GPL implementation,
Python tooling, PNG runtime assets, guessed constants or separate effect pools.
Commit verified changes with what/how/why/verification bodies; do not push.
Use `SDL_VIDEODRIVER=dummy` for tests. Loopback tests need sandbox escalation.
Keep new local disk use below 500 MB; no new dependencies are needed.

## Committed work

| Commit | Result |
|---|---|
| `e64c18f`, `0d46595` | Reference provenance and pinned Zod comparison |
| `1d706bd` | Z crew/APC ejection and factory rally routes |
| `f675466`, `043c4be` | StarCraft options, mission save restoration, briefing voices, control groups |
| `efa8ee2` | Z native shell impact bounds and crew ownership correction |
| `41cf364`, `17010cf` | Z damage-dependent speed, fire rate and driver accuracy |
| `b87f1e1` | Z timed shell flight and saved impact state |
| `ffa5c45` | StarCraft regeneration, building fire, diplomacy, music and Spore AI chain |
| `7ed27c4` | Z four-gun stock, territory capacity and mixed production queue |
| `6674559` | StarCraft native-data moving projectiles and headless terrain metadata |
| `cbaf803` | Z timed building reconstruction and crane reconstruction effects |
| `e08c2f1` | StarCraft mixed queued orders, network protocol 7, immediate-weapon miss/splash rules |
| `c0ed12d` | Cloaked Ghost/Kerrigan acquisition suppression, preserving Hold and explicit/provoked attacks |
| `bb88cd9` | Offline model move/harvest commands use the same tic-command path as network commands |

Detailed source evidence and implementation limits are in `SC_EXE_FINDINGS.md`
and `Z_EXE_FINDINGS.md`; these commits do not establish full retail parity.

## Pending implementation at this checkpoint

The working tree intentionally contains unfinished changes. Do not reset them
or commit them as finished solely because focused checks pass.

1. **Z mixed action routes**: new `games/z/p_route.c` replaces positional-only
   factory rally storage with bounded action/position/stable-target steps.
   Immediate group movement retains shared formation dispatch; Shift appends.
   Boarding, repair, wreck-footprint ground clicks and hostile-fort entrance
   commands use existing game procedures. Produced units inherit templates.
   Capture/death/Stop cancel routes; state is saved and hashed. Remaining review:
   target-death continuation, rejected commands, stale targets, mixed selections,
   queued path capacity/modes and malformed saved steps. Queued `WP_ONCE` paths
   may expand to steps only if the entire path fits; never silently truncate or
   reinterpret loop/backtrack routes. Nonqueued paths retain existing semantics.
2. **Native squad promotion**: new `p_squad.c` uses existing squad identity plus
   one slot byte (255 leader, 1–20 follower, 0 none). Lowest surviving slot is
   promoted on departure/death/owner change; route intent is transferred and
   replanned from the survivor. Ejected crew receive free slots. Native order
   words +0x50/+0x52/+0x54 have state-dependent meanings; no universal mapping
   is established. Verify actual travel, save/load, boarding/ejection and
   removal. Persistent follower formation/rejoin remains to trace/implement.
3. **Terrain speed**: `p_capture.c` applies native signed terrain terms using
   top-left +8,+8, combines mutable damaged body speed, clamps to levels 0–12.
   `test_terrain_speed.c` covers both clamps and an independent Fire Truck
   adjacent-cell sampling case. Full integration still pending.
4. **Moving-unit detours**: confirmed native counterpart to Zod opportunistic
   grabbing, but not implemented yet. Continue the exact trace below.
5. Finish the coverage ledger/provenance, independent review, integrated tests
   and commits. Do not turn the ledger's category coverage into a parity claim.

Related dirty integration files include `g_hooks.c`, `mobj_data.h`,
`p_capture.c`, `p_prod.c`, `w_lvl.c`, `z_local.h`, and Z capture/commander/fort/
production tests. Z save layout is 7 at this checkpoint. `game/g_cmd.c` now
uses `RTS_QUEUED_ORDERS`, defined in Z and StarCraft `game_config.h`; Z has no
`RTS_GAME_Z` or `RTS_MODULE_Z` macro. New untracked files include route/squad/
terrain tests and `REFERENCE_IMPLEMENTATION_COVERAGE.md`; preserve them.

## Agent ownership

- `/root/z_implementation`: route implementation, Z shared declarations/hooks,
  production/fort/repair integration and tests.
- `/root/coverage_review`: `p_squad.c`, `test_squad.c`, formation tracing,
  independent route review and coverage ledger.
- `/root/sc_implementation`: completed terrain speed; now native detour tracing
  and implementation. The name reflects its earlier StarCraft assignment.

They share the working tree. Coordinate overlapping edits and builds. Do not
start parallel `make` processes targeting the same object directories.

## Native trace continuation

`data/Z/ZED.EXE` SHA-256:
`3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676`.
Use r2 with `-e bin.cache=true -e bin.relocs.apply=true`. Existing broad
disassembly is `reverse/z-ui/service-code.txt`; `reverse/z-ui/examine.c`
and its binary can inspect relocated data. Verify narrow instructions with
r2; broad linear disassembly contains inline-string decoding errors.

- Action 2: `0x7a9e4`, search branch `0x7ab0c..0x7ab36` every other native
  step (`0x10e91b`, toggled `0x19e72`). `0x78fed` uses direction-indexed
  regions/sectors, object visibility, SQROOT ranking and terrain ray `0x6e491`.
  Require twice distance <= unit +4, then guard `0x78bfb` and dispatch
  `0x61022`. Selection occurs before type eligibility; do not replace with
  unrestricted nearest eligible object.
- Flag marker `0x61293` preserves path pointer +0x14. Capture
  `0x79e91..0x79ec7` clears follower/ungrouped paths, retaining leader path.
  Cheer ends through `0x79049`, resumes only with negative order byte +0x5a.
  `0x7d053` copies group +0x3a to that byte. Resolve signed-order lifecycle,
  reservations, ray/visibility and crate/vehicle continuation before porting.
- Squad attach `0x77fd6`, remove `0x77821`, promotion `0x77b97`.
  Group record is 72 bytes at `0x1943f8`; unit +0x5c slot, +0x5d group.
  First survivor gets leader slot and old path/order state. Native grenades
  are distributed among members, unlike Zod's leader-held transfer.
- Native group +0x3d is a countdown coupled to target state, not rechargeable
  stamina: player command bit set `0x47af6`, route setup `0x7c6ba` sets
  120/30, group pass `0x780b3` decrements and clears when group +0x10 expires.
  Target response sets +0x10 to 75. Nonzero +0x3d adds three speed levels and
  suppresses moving enemy scans. Do not give every move an arbitrary sprint.
  Exact evidence is in Z findings and `/tmp/z-speed-modifiers.txt`.
- Native missile reaction action 0x2b (`0x62cec`) advances animation and
  returns to idle; no proven displacement or damage immunity. Zod random
  dodge and stamina rules remain conflicting/unverified fork policies.

## Verification checkpoint

- Six current Z checks passed together: production, commander, capture, fort,
  squad, terrain. `/tmp/open-rts-handover-tests.log`; build log beside it.
  This is a focused checkpoint, not final integrated verification.
- All 20 StarCraft checks passed after Ghost/model changes:
  `/tmp/open-rts-sc-audit-final.log`.
- Dark Colony/Dark Reign model command suites passed after `bb88cd9`:
  `/tmp/open-rts-model-dispatch.log`.
- Before the pending Z changes: all 24 Z checks passed, with menu/network
  checks rerun outside the socket sandbox. All 18 network cases passed:
  `/tmp/open-rts-shift-network.log`.
- StarCraft AI soak passed 30 simulated minutes per race and a 15-minute
  duel: `/tmp/open-rts-sc-final-ai.log`. It covered advanced units, level-three
  research, expansions, siege/bunkers/storm and the Spore prerequisite chain.
- All seven binaries and SC/DC/War2/Z headless smokes passed before this
  pending Z tranche. Final builds/smokes are still required after it.

Useful final commands (loopback suites may need escalation):

```sh
env SDL_VIDEODRIVER=dummy make -j4 test-z TEST_JOBS=3 TEST_BUDGET=60
env SDL_VIDEODRIVER=dummy make -o starcraft-maps -j4 test-starcraft TEST_JOBS=3 TEST_BUDGET=60
env SDL_VIDEODRIVER=dummy make -j4 test-model-commands TEST_JOBS=2 TEST_BUDGET=60
make -j4
env SDL_VIDEODRIVER=dummy build/bin/z --check
git diff --check
```

Old `/tmp` test logs include failures already fixed; use the latest named
logs above. Do not rerun expensive soaks without a relevant new change.

## Reference pins

- `reference/zod`: `5ccb43ab76ee08c9f624906fb465dc3ba76a7a35`
- `reference/stargus`: `2a4d54604949e4772f6638a8412c8ebd62444044`
- `reference/stratagus`: `3d87c93f7fd8c0b62ee1be5df0a6d9efc72ca6cc`
- OpenBW: `4b046d5f65302b10cb0a745f0fecd37ec85b20a8`, downloaded source
  `/tmp/openbw-bwgame.h`, `/tmp/openbw-game_types.h`, `/tmp/openbw-data_types.h`.
  Use function names for citations; web-normalized line numbers differ.

Stargus operational unit fields have been categorized in the ledger. Its
Coward workers, broad flyer detection, six-slot transports, balance values
and cheat resources conflict with native rules. Its commented/inert spells,
Still-only critters and CHK trigger comments are not missing working gameplay
to copy. Full native campaign/AI-bytecode fidelity remains broader unfinished
work, not a capability supplied by Stargus.
