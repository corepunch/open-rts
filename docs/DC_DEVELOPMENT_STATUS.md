# Dark Colony development status

Updated September 9, 2026. This is a navigation and verification handoff;
[detailed native evidence](DC_EXE_FINDINGS.md) remains authoritative. Earlier
reports of a failing Human01 initial-Trooper assertion are superseded by the
final September 9 section of that report.

## Implemented and verified

- VENT and BEAC are ordinary mobjs with complete FIN frames. Vents have active,
  attached and exhausted states; their plume uses the crater attachment origin.
  See commit `00cc34c` and `test_vent_states`.
- Buildings have entries in `mobjinfo[]` and spawn through `P_SpawnMobj`.
  Native city slot positions preserve simulation/sort geometry; FIN rendering
  uses the common city origin. TOWR uses its own slot 5. See `cad8953`,
  `3a6e889` and `test_city_layout`.
- Human02's nearby extra bases were a loader error: an absent city anchor was
  replaced by an AI location. `6f375e7` removes that fallback. Human02 cities
  belong only to team 0; Human03 legitimately retains teams 0, 2 and 7.
- The Exploiter can mine enough Petra-7 to buy Troopers. Barracks production
  runs the complete native TRSCBUILD0 door/exit sequence, then creates the
  mobile Trooper at the FIN-derived handoff position. Queued production,
  sidebar activation and blocked-exit retries are checked by
  `test_barracks_production`; see `3a6e889`.
- Human01 starts with 30 enemy Greys and zero player Troopers. Its opening
  script supplies four native type-0 Troopers plus native type-69 commander
  (currently also represented as MT_TROOPER). The headless model test now
  checks this sequence and reaches the Human02/03 assertions as well.
- Snapshot gameplay sprite names are registry stems, such as GRAY/HUBU/DISH.
  Negative SCN health requests the type's default health, not invisibility.

## Verification commands

Current build outputs are separate game binaries. Some older instructions
still show the former `open-rts --game` invocation.

```sh
make
make tags
SDL_VIDEODRIVER=dummy make test-dark-colony test-layout
SDL_VIDEODRIVER=dummy build/bin/dark-colony --check
```

The complete Dark Colony suite and sprite-layout checks pass. The focused
city test writes `/private/tmp/city-human02.bmp` and `city-human03.bmp`.
The production test writes `/private/tmp/barracks-{closed,open,exit,released}.bmp`.
Tests requiring SDL must use the dummy video driver for headless runs.

## Remaining fidelity questions

These are investigation notes, not reasons to reintroduce workarounds:

- Native tower synthesis is confirmed, but the full mode/team policy and
  dynamic-object companion-tower path remain only partially traced.
- Barracks FIN presentation and handoff coordinates are verified. The current
  cost-based training duration and post-exit spacing policy are engine behavior;
  exact retail caller-side timing still needs tracing.
- The Exploiter's native harvesting light implementation remains unknown.
  Preserve its deployed-body work frames until evidence supports another effect.
- The original SHUF selection path is unknown. Preserve the documented
  eight animated travel directions and sixteen stationary Exploiter poses.
- Preserve Reaper RUN timing `{4,3,3,4,1,3,3,1}` after state-table regeneration.

Follow [the reverse-engineering workflow](../REVERSE_ENGINEERING.md), record
addresses and asset hashes in the findings document, and keep provenance in
[REFERENCES.md](../REFERENCES.md). Do not use visual offsets or guessed aliases
to compensate for unknown native behavior.
