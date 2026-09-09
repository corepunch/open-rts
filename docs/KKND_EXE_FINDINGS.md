# KKnD loader evidence

## Representation cleanup (2026-09-09)

**Confirmed by asset-loader comparison to `46f826a`.** All 47 installed LVL map
candidates and MOBD members 0–99 match, including rejected candidates. There
are 44 successful maps and 57 successful MOBD members. Every map comparison
includes the terrain-atlas pixels as well as map metadata and initial mobjs.
Default screenshots are byte-identical. This task did not inspect KKND.EXE and
makes no new executable-address or retail-behavior claim. See
[loader verification](LOADER_REFACTOR_VERIFICATION.md) for commands and scope.

Representative SHA-256 fingerprints:

- `data/KKND/LEVELS/640/SPRITES.LVL`:
  `3e7dbe10624c706afd963e18f54f780052e6ee0b415fc8008add6e37de669ae6`.
- `data/KKND/LEVELS/640/SURV_01.LVL`:
  `66243cfff0f49e22684be6b7074a4bdf71d370f54e22a96d025b0fdcb7af85f7`.

The existing MAPD decoder reads the layer count, layer-offset table, palette
count and palette, then LRCS dimensions and tile offsets. It still skips the
four-byte Gen1 tile prefix and treats index zero as transparent only on upper
layers. Atlas frame numbering, including the gap before the first upper layer,
is unchanged. Palette conversion now writes the final terrain atlas directly;
upper-layer visibility is accumulated from those same pixels while decoding.
There is no need to retain expanded per-layer pixel images or scan them again.

MOBD animation ordering still follows the existing timing/offset traversal.
Only frame offsets are retained until cell allocation; each image then decodes
directly into its own texture. TRPS bit 0 still mirrors the decoded image.
Displacement remains `(width/2,height/2) - authored offset`. Raw and RLE images
retain index-zero transparency. Oversized transparent RLE runs now reject like
oversized literal runs; a fixture covers this malformed-input correction.

`MUTE_07.LVL`, `SPRITES.LVL`, and `SUPSPR.LVL` remain rejected as map candidates.
The manifest deliberately includes non-map containers and unsupported MOBD
members; these failures are not interpreted as corrupt retail assets. Animation
selection limits, member-name aliases, and the retail meaning of unsupported
members remain **unverified here** and were not changed.
