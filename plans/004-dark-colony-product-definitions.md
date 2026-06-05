# Implement Dark Colony Construction Product Definitions

## Context

Original data maps construction and production through
`data/DCOLONY/GAMESTAT/DEPEND.TXT` and UI metadata in
`data/DCOLONY/INTRFACE/MAINE`. We need a model-side product definition table
before UI clicks can construct buildings or train units.

## Source Mapping

Human building buttons:

| UI id | Label | Icon frame | Product type |
|-------|-------|------------|--------------|
| 206 | Exo-Ctr 2000 | 129 | building 16 `EXCOPOD` |
| 80 | Barracks 1000 | 20 | building 17 `BRRKPOD` |
| 81 | Sci-Pod 2000 | 21 | building 20 `SCNCPOD` |
| 82 | Robo-Ftr 2000 | 22 | building 18 `ROBOPOD` |
| 83 | Rsch-Bay 3000 | 23 | building 22 `RSCHPOD` |
| 85 | Sci-Pod + 2000 | 26 | building 21 `SCNCPOD2` |
| 86 | Robo-Ftr+ 2000 | 30 | building 19 `ROBOPOD2` |

Human unit buttons:

| UI id | Label | Icon frame | Product type |
|-------|-------|------------|--------------|
| 87 | Exploiter 1500 | 8 | unit 6 `EXPL` |
| 89 | Trooper 350 | 6 | unit 0/69-72 `TRSC` |
| 90 | Sentinel 450 | 5 | unit 1 tower builder / mine-deploy path |
| 92 | Osprey IV 600 | 9 | unit 5 `SCGM` |
| 91 | Reaper 600 | 11 | unit 2 `REAP` |
| 88 | Firestorm 900 | 10 | unit 1 tower builder path |
| 93 | Barrager 1000 | 7 | unit 3 `BARR` |
| 94 | S.A.R.G.E 1500 | 12 | unit 4 `SARG` |
| 135 | Medi-craft 900 | 29 | unit 49 `BEON` |

## Scope

- [x] Decide whether to parse `DEPEND.TXT` at runtime or generate/static-code a
      product table.
- [x] Store UI id, label, cost, icon frame, product class, product type,
      faction, and prerequisites.
- [x] Expose product availability from current model state.
- [x] Expose product lists for sidebar modes.

## Acceptance Criteria

- [x] Human product table includes the original building and unit buttons.
- [ ] Prerequisites are resolved against model-owned buildings/upgrades.
- [x] UI can ask for available/disabled products without duplicating rules.
- [x] Costs and labels match original data.

## Tests

- [x] Exo Center is available at initial no-prerequisite state.
- [x] Barracks requires Exo Center.
- [x] Trooper requires Barracks.
- [x] Reaper/Barrager availability follows Robot Factory prerequisites.
- [x] Product metadata test verifies icon frames and costs for key products.
