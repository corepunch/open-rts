#ifndef __MOBJ_DATA__
#define __MOBJ_DATA__

enum { Z_CREW_MAX = 21 }; /* ROBOTS.DAT group has 21 member slots. */
enum { Z_GUN_STOCK = 4 }; /* Native building +0x4b..+0x4e. */
typedef struct { int hp; uint16_t robot; uint8_t type, grenades, home; bool manufactured; } zcrew_t;

/* A robot sent to drive an empty vehicle or gun names it in `board`; a
 * manned one keeps its driver's weapon type + 1 in `driver`. Robots of a
 * starting group share a `squad` and the `grenades` its crates gave them.
 * A flag carries its territory + 1 and the tics before it can be taken
 * again. */
#define MOBJ_GAME_FIELDS struct { \
    waypoints_t rally; /* Zod factory rally route for subsequently manufactured units. */ \
    uint32_t board; \
    uint32_t rating; /* Native building +0x0c production technology. */ \
    uint32_t service; /* Stable target ID for a crane or vehicle at a repair bay. */ \
    int service_work; /* Work on a wreck, crane animation phase, or negative repair stock. */ \
    uint16_t cliff_bit; \
    uint16_t robot; /* ROBOTS.DAT record + 1, retained for native AI valuation. */ \
    uint8_t cliff_block, home; \
    uint8_t object, building; /* Native OBJECT type / BUILD record index + 1. */ \
    uint8_t driver, squad, grenades, territory; \
    uint8_t gun_count, guns[Z_GUN_STOCK]; /* Stored weapon types + 1, newest first. */ \
    uint8_t crew_count; zcrew_t crew[Z_CREW_MAX]; \
    uint8_t crew_hurt; /* Damage the driver has taken from bullets. */ \
    uint8_t body_speed, body_fire, body_accuracy; bool body_tracked; /* Mutable native stats. */ \
    uint8_t bridge; /* BRIDGEnn.DAT slot + 1. */ \
    uint8_t service_ticks; /* Service animation, pending bridge effects, or repair bay HP clock. */ \
    bool manufactured; /* CPU unit flag 0x10, set by 0x5103e. */ \
    int hold; \
} z;
#define MOBJ_GAME_CHECKSUM(HASH, mo) do { \
    HASH((mo)->z.rally.count); HASH((mo)->z.rally.mode); \
    for (int zi = 0; zi < (mo)->z.rally.count; ++zi) { \
        HASH((mo)->z.rally.points[zi].x); HASH((mo)->z.rally.points[zi].y); \
    } \
    HASH((mo)->z.board); HASH((mo)->z.rating); HASH((mo)->z.driver); HASH((mo)->z.squad); HASH((mo)->z.grenades); \
    HASH((mo)->z.territory); HASH((mo)->z.hold); \
    HASH((mo)->z.gun_count); for (int z_g = 0; z_g < (mo)->z.gun_count && z_g < Z_GUN_STOCK; ++z_g) HASH((mo)->z.guns[z_g]); \
    HASH((mo)->z.cliff_block); HASH((mo)->z.cliff_bit); HASH((mo)->z.home); \
    HASH((mo)->z.robot); HASH((mo)->z.object); HASH((mo)->z.building); HASH((mo)->z.crew_hurt); \
    HASH((mo)->z.service); HASH((mo)->z.service_work); HASH((mo)->z.bridge); HASH((mo)->z.service_ticks); \
    HASH((mo)->z.body_speed); HASH((mo)->z.body_fire); HASH((mo)->z.body_accuracy); HASH((mo)->z.body_tracked); \
    HASH((mo)->z.manufactured); \
    HASH((mo)->z.crew_count); \
    for (int z_i = 0; z_i < (mo)->z.crew_count; ++z_i) { \
        HASH((mo)->z.crew[z_i].hp); HASH((mo)->z.crew[z_i].robot); HASH((mo)->z.crew[z_i].type); \
        HASH((mo)->z.crew[z_i].grenades); HASH((mo)->z.crew[z_i].home); HASH((mo)->z.crew[z_i].manufactured); \
    } \
} while (0)

#endif
