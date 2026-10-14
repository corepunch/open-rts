#ifndef __MOBJ_DATA__
#define __MOBJ_DATA__

/* A robot sent to drive an empty vehicle or gun names it in `board`; a
 * manned one keeps its driver's weapon type + 1 in `driver`. Robots of a
 * starting group share a `squad` and the `grenades` its crates gave them.
 * A flag carries its territory + 1 and the tics before it can be taken
 * again. */
#define MOBJ_GAME_FIELDS struct { \
    uint32_t board; \
    uint8_t driver, squad, grenades, territory; \
    int hold; \
} z;
#define MOBJ_GAME_CHECKSUM(HASH, mo) do { \
    HASH((mo)->z.board); HASH((mo)->z.driver); HASH((mo)->z.squad); HASH((mo)->z.grenades); \
    HASH((mo)->z.territory); HASH((mo)->z.hold); \
} while (0)

#endif
