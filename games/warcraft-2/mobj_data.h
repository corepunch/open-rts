#ifndef __MOBJ_DATA__
#define __MOBJ_DATA__

/* Lumber progress belongs to the worker, not the tree (Blizzard: 51 chops).
 * A worker's building job is its own: the type and top-left cell it was
 * sent to build, the site it works inside, and how often it re-approached.
 * A structure under construction carries the time left, the whole build
 * time and the builder inside it. */
#define MOBJ_GAME_FIELDS struct { \
    int chops; \
    int build_phase; uint16_t build_type; ivec2_t build_cell; uint32_t site; int build_tries; \
    int build_left_ms, build_time_ms; uint32_t builder; \
} w2;
#define MOBJ_GAME_CHECKSUM(HASH, mo) do { \
    HASH((mo)->w2.chops); HASH((mo)->w2.build_phase); HASH((mo)->w2.build_type); \
    HASH((mo)->w2.build_cell.x); HASH((mo)->w2.build_cell.y); HASH((mo)->w2.site); \
    HASH((mo)->w2.build_left_ms); HASH((mo)->w2.builder); \
} while (0)

#endif
