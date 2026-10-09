#ifndef __MOBJ_DATA__
#define __MOBJ_DATA__

typedef struct { uint32_t target; int tics; } w2_repair_t;

/* Lumber progress belongs to the worker, not the tree (Blizzard: 51 chops).
 * A worker's building job is its own: the type and top-left cell it was
 * sent to build, the site it works inside, and how often it re-approached.
 * A structure under construction carries the time left, the whole build
 * time and the builder inside it. A computer's transport carries its
 * ferry job: gathering, sailing, then landing troops at `to`. */
#define MOBJ_GAME_FIELDS struct { \
    int chops; \
    int build_phase; uint16_t build_type; ivec2_t build_cell; uint32_t site; int build_tries; \
    int build_left_tics, build_tics; uint32_t builder; \
    w2_repair_t repair; bool stand_ground; \
    uint32_t fire; \
    uint32_t carrier; bool boarded, unloading; \
    struct { int phase, wait; ivec2_t to; } ferry; \
    int mana, buffs[5], ttl; \
    struct { int spell; uint32_t target; fixed3_t position; } cast; \
    struct { int kind, age, clock, basic, piercing, mask, bounces; uint32_t subject; fixed3_t end; } fx; \
} w2;
#define MOBJ_GAME_CHECKSUM(HASH, mo) do { \
    HASH((mo)->w2.chops); HASH((mo)->w2.build_phase); HASH((mo)->w2.build_type); \
    HASH((mo)->w2.build_cell.x); HASH((mo)->w2.build_cell.y); HASH((mo)->w2.site); \
    HASH((mo)->w2.build_left_tics); HASH((mo)->w2.builder); \
    HASH((mo)->w2.build_tics); HASH((mo)->w2.build_tries); \
    HASH((mo)->w2.repair.target); HASH((mo)->w2.repair.tics); HASH((mo)->w2.stand_ground); \
    HASH((mo)->w2.carrier); HASH((mo)->w2.boarded); HASH((mo)->w2.unloading); \
    HASH((mo)->w2.ferry.phase); HASH((mo)->w2.ferry.wait); \
    HASH((mo)->w2.ferry.to.x); HASH((mo)->w2.ferry.to.y); \
    HASH((mo)->w2.fire); HASH((mo)->w2.fx.kind); HASH((mo)->w2.fx.age); HASH((mo)->w2.fx.clock); \
    HASH((mo)->w2.fx.basic); HASH((mo)->w2.fx.piercing); HASH((mo)->w2.fx.mask); HASH((mo)->w2.fx.bounces); \
    HASH((mo)->w2.fx.subject); HASH((mo)->w2.fx.end.x); HASH((mo)->w2.fx.end.y); \
    HASH((mo)->w2.fx.end.z); \
    HASH((mo)->w2.mana); HASH((mo)->w2.ttl); \
    for (int b = 0; b < 5; ++b) HASH((mo)->w2.buffs[b]); \
    HASH((mo)->w2.cast.spell); HASH((mo)->w2.cast.target); \
    HASH((mo)->w2.cast.position.x); HASH((mo)->w2.cast.position.y); \
    HASH((mo)->w2.cast.position.z); \
} while (0)

#endif
