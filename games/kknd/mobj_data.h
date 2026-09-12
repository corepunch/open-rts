#ifndef __MOBJ_DATA__
#define __MOBJ_DATA__

/* OpenKrush research belongs to its producer, and is lost with that building.
 * References use stable mobj ids so removal cannot leave dangling pointers. */
#define MOBJ_GAME_FIELDS \
    struct { \
        int level; \
        uint32_t target; \
        int next_level; \
        int total_time, remaining_time; \
        int total_cost, remaining_cost; \
        int clock; \
    } research;

#define MOBJ_GAME_CHECKSUM(HASH, u) \
    HASH((u)->research.level); HASH((u)->research.target); \
    HASH((u)->research.next_level); HASH((u)->research.total_time); \
    HASH((u)->research.remaining_time); HASH((u)->research.total_cost); \
    HASH((u)->research.remaining_cost); HASH((u)->research.clock)

#endif
