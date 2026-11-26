#ifndef __MOBJ_DATA__
#define __MOBJ_DATA__

/* A projectile remembers whom it was fired at (a mobj id), to hit it on arrival. */
/* A construction site remembers how far it has built (per mille), to grow its hit points. */
/* g_base.c: kills toward veterancy, a structure being repaired, a producer's rally point. */
#define MOBJ_GAME_FIELDS uint32_t rf_victim; int rf_built; uint16_t rf_kills; uint8_t rf_repairing; \
    bool rf_rally_set; fixed2_t rf_rally;
#define MOBJ_GAME_CHECKSUM(HASH, mo) do { \
    HASH((mo)->rf_victim); HASH((mo)->rf_built); HASH((mo)->rf_kills); HASH((mo)->rf_repairing); \
    HASH((mo)->rf_rally_set); HASH((mo)->rf_rally.x); HASH((mo)->rf_rally.y); \
} while (0)

#endif
