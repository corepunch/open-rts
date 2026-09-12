# Asset system architecture

## Core rule: WAD-style indexed lumps

Assets are stored as **indexed lumps with a 256-entry palette**, not pre-baked
RGBA atlases. This mirrors Doom's WAD model and Dark Colony's native SPR/FIN
formats exactly.

- `spritelump_t` contains an `SDL_Texture` created from palette-mapped pixels.
- `spritecell_t` tracks the cell rect and displacement (draw offset from anchor).
- `spritesheet_t` holds lumps + cells + a spritedef for frame mapping.

## Init must be trivial

`init_lumps` (or equivalent per-game asset init) does **no rendering work**.
Assets are loaded and decoded on first use, not at game startup. Pre-baking
textures or atlas packing at init time is wrong.

## Per-game asset loaders

| Game | Loader | Format |
|------|--------|--------|
| Dark Colony | `w_spr.c`, `w_fin.c` | SPR (indexed), FIN (animation) |
| Dark Reign | `games/dark-reign/w_spr.c` | RSPR (indexed RLE) |
| 7th Legion | `games/7legion/w_bim.c` | BIM (run-length encoded) |
| KKnD | `games/kknd/w_spr.c` | MOBD (frame→TRPS→image chain) |

## Flip flag (KKnD MOBD)

`decode_mobd_image` returns a `bool *flip_out`. The caller is responsible for
horizontal flipping; the decoder stores pixels in original (unflipped) order.
The `flip_out` value is stored in `flips[]` alongside the lump and applied at
render time.
