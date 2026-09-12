# open-rts — agent context index

Build commands and data layout are in [CLAUDE.md](../CLAUDE.md).
This directory adds architectural constraints, design decisions, and known gotchas.

## Files

- [arch-doom-heritage.md](arch-doom-heritage.md) — State machine, mobj lifecycle, what must stay Doom-shaped
- [arch-assets.md](arch-assets.md) — Sprite/asset system: WAD-style lumps, no pre-baked atlases
- [arch-metadata-ownership.md](arch-metadata-ownership.md) — ACTOR_TYPES[] vs mobjinfo[] ownership rules and known mismatch in Dark Reign
- [project-game-status.md](project-game-status.md) — Per-game playability gaps and coverage accounting
- [project-allegiance-fix.md](project-allegiance-fix.md) — Dark Reign combat was silently broken; how and why it was fixed
