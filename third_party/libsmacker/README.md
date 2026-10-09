# libsmacker

Decoder sources from https://github.com/JonnyH/libsmacker at
`ae8d4c9ec07b24d43ccff184d6e512bae793dfd1` (version 1.2.0).
Copyright Greg Kennedy; LGPL-2.1-or-later. See `COPYING` and source notices.

Local changes, 2026-10-09:

- Accept a complete Huffman tree smaller than the header's allocation capacity.
  StarCraft `glue/palcs/arrow.smk` has one node in a two-node allocation.
  Recursive construction still checks the capacity and the terminator bit.
- Allow freeing NULL during cleanup of a partially opened movie. Truncated
  files must return an error before the video buffer has been allocated.
- Rename the classic header guards to the repository convention.

`tests/starcraft/test_glue.c` decodes every installed glue movie, checks all
five arrow frames, and rejects a truncated header without asserting.
Detailed evidence and provenance are in `docs/SC_EXE_FINDINGS.md` and
`REFERENCES.md`. No FFmpeg code is incorporated.
