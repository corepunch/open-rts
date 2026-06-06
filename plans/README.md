# Local Plans

This folder is the local issue tracker for work that is not yet mirrored to
GitHub issues. Keep each plan small enough to implement and verify. Mark
checkboxes as complete when the corresponding code and tests land.

## Dark Colony Roadmap

| Status | Plan | Summary |
|--------|------|---------|
| [ ] | [001-headless-model-library.md](001-headless-model-library.md) | Detach the game/model library from rendering with a command-in/render-snapshot-out API. |
| [ ] | [002-dark-colony-headless-tests.md](002-dark-colony-headless-tests.md) | Add deterministic Dark Colony tests without SDL windows. |
| [ ] | [003-dark-colony-buildings-as-entities.md](003-dark-colony-buildings-as-entities.md) | Load buildings as selectable, renderable, non-mobile model entities. |
| [ ] | [004-dark-colony-product-definitions.md](004-dark-colony-product-definitions.md) | Model construction/production products from original data. |
| [ ] | [005-dark-colony-building-placement.md](005-dark-colony-building-placement.md) | Implement BUILD mode, placement ghost, costs, and footprints. |
| [ ] | [006-dark-colony-unit-production.md](006-dark-colony-unit-production.md) | Train units from production buildings with queues and tests. |
| [ ] | [007-dark-reign-model-production.md](007-dark-reign-model-production.md) | Mirror model-side products and production into Dark Reign. |

## Maintenance

- Prefer updating plan checkboxes in the same commit as implementation.
- If scope grows, split the plan rather than making one giant checklist.
- Every gameplay plan should include a headless test path.
