# 7th Legion loader evidence

## Representation cleanup (2026-09-09)

**Confirmed by asset-loader comparison to `46f826a`.** The installed MAPT.000
and all 240 non-TILES BIM candidates produce identical results: 235 successful
sprite pixel/metadata fingerprints and five unchanged rejections. The default
screenshot is byte-identical. This task did not inspect legion.exe, so it makes
no new executable-address or retail-behavior claim. Reproduce with the commands
in [loader verification](LOADER_REFACTOR_VERIFICATION.md).

Representative `data/7LEGION/GFX/TROOP1W.BIM` SHA-256:
`65408d8835ce593c4443b4e0e38ec19e5c52fda7a9f475e807ceae29a3e270e4`.

The existing decoder reads a leading 32-bit offset table. Sparse frame headers
contain a 16-bit relative pixel-data offset and height; each row contains a
16-bit span count and `(x,length)` pairs. Those validated spans still determine
the shared canvas and final bounds. Missing spans remain transparent; every
palette value inside a span retains the supplied color, including index zero.
Trailing zero-height frames remain excluded, and the existing eight-facing
block interpretation remains unchanged. The full per-frame metadata table and
temporary RGBA atlas were allocation artifacts, not asset requirements.

The VCLZ ring decoder is unchanged; its output replaces the compressed file
buffer immediately. Expanded outputs under four bytes now reject before reading
the offset table. A focused fixture covers this malformed-input correction.

Unchanged rejected sprite candidates: `FOGGY.BIM`, `FOGWAR.BIM`, `FONT16.BIM`,
`INDFOG.BIM`, and `SNOWFOG.BIM`. Their format/usage remains **unknown in this
investigation**; no fallback, name exception, or guessed frame metadata was
added. Existing MAPT/MAPOVL/MAPL indexing and decoding formulas are preserved,
not newly verified against an executable by this comparison.
