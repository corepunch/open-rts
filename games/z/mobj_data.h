#ifndef __MOBJ_DATA__
#define __MOBJ_DATA__

enum { Z_CREW_MAX = 21 }; /* ROBOTS.DAT group has 21 member slots. */
enum { Z_GUN_STOCK = 4 }; /* Native building +0x4b..+0x4e. */
typedef struct { int hp; uint16_t robot; uint8_t type, grenades, home, flame_phase; bool manufactured; } zcrew_t;
typedef struct { fixed3_t position; uint32_t target; int order; } zroute_step_t;
typedef struct {
    zroute_step_t steps[MAXWAYPOINTS];
    uint32_t target;
    int active;
    uint8_t count, owner;
} zroute_t;
enum { Z_DETOUR_TRAVEL = 1, Z_DETOUR_CHEER = 2 };
typedef struct {
    fixed2_t resume;
    uint32_t target, claimant;
    uint8_t slot, phase, cooldown;
} zdetour_t;

/* A robot sent to drive an empty vehicle or gun names it in `board`; a
 * manned one keeps its driver's weapon type + 1 in `driver`. Robots of a
 * starting group share a `squad` and the `grenades` its crates gave them.
 * A flag carries its territory + 1 and the tics before it can be taken
 * again. */
#define MOBJ_GAME_FIELDS struct { \
    zroute_t route; /* Player order queue, or a factory's rally template. */ \
    zdetour_t detour; /* Native object slot, interaction reservation and interrupted goal. */ \
    uint32_t board; \
    uint32_t rating; /* Native building +0x0c production technology. */ \
    int32_t rebuild_stock; uint16_t rebuild_setting; /* BUILD +0x45 / +0x49. */ \
    struct { ivec2_t origin, position, velocity; int height, rise, ticks; uint8_t kind, samples; } debris; \
    uint32_t service; /* Stable target ID for a crane or vehicle at a repair bay. */ \
    int service_work; /* Work on a wreck, crane animation phase, or negative repair stock. */ \
    uint16_t cliff_bit; \
    uint16_t robot; /* ROBOTS.DAT record + 1, retained for native AI valuation. */ \
    uint8_t cliff_block, home; \
    uint8_t object, building, object_slot, object_hp; /* Native OBJECT type, slot and effect health. */ \
    uint8_t driver, squad, squad_slot, formation, formation_direction, grenades, territory, flame_phase; \
    bool formation_active, formation_rejoin; \
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
    HASH((mo)->z.route.count); HASH((mo)->z.route.active); HASH((mo)->z.route.owner); HASH((mo)->z.route.target); \
    HASH((mo)->z.detour.resume.x); HASH((mo)->z.detour.resume.y); \
    HASH((mo)->z.detour.target); HASH((mo)->z.detour.claimant); \
    HASH((mo)->z.detour.slot); HASH((mo)->z.detour.phase); HASH((mo)->z.detour.cooldown); \
    for (int zi = 0; zi < (mo)->z.route.count && zi < MAXWAYPOINTS; ++zi) { \
        HASH((mo)->z.route.steps[zi].order); HASH((mo)->z.route.steps[zi].target); \
        HASH((mo)->z.route.steps[zi].position.x); HASH((mo)->z.route.steps[zi].position.y); HASH((mo)->z.route.steps[zi].position.z); \
    } \
    HASH((mo)->z.board); HASH((mo)->z.rating); HASH((mo)->z.driver); HASH((mo)->z.squad); HASH((mo)->z.squad_slot); HASH((mo)->z.formation); HASH((mo)->z.formation_direction); HASH((mo)->z.formation_active); HASH((mo)->z.formation_rejoin); HASH((mo)->z.grenades); HASH((mo)->z.flame_phase); \
    HASH((mo)->z.rebuild_stock); HASH((mo)->z.rebuild_setting); \
    HASH((mo)->z.debris.origin.x); HASH((mo)->z.debris.origin.y); \
    HASH((mo)->z.debris.position.x); HASH((mo)->z.debris.position.y); \
    HASH((mo)->z.debris.velocity.x); HASH((mo)->z.debris.velocity.y); \
    HASH((mo)->z.debris.height); HASH((mo)->z.debris.rise); HASH((mo)->z.debris.ticks); \
    HASH((mo)->z.debris.kind); HASH((mo)->z.debris.samples); \
    HASH((mo)->z.territory); HASH((mo)->z.hold); \
    HASH((mo)->z.gun_count); for (int z_g = 0; z_g < (mo)->z.gun_count && z_g < Z_GUN_STOCK; ++z_g) HASH((mo)->z.guns[z_g]); \
    HASH((mo)->z.cliff_block); HASH((mo)->z.cliff_bit); HASH((mo)->z.home); \
    HASH((mo)->z.robot); HASH((mo)->z.object); HASH((mo)->z.building); HASH((mo)->z.object_slot); HASH((mo)->z.object_hp); HASH((mo)->z.crew_hurt); \
    HASH((mo)->z.service); HASH((mo)->z.service_work); HASH((mo)->z.bridge); HASH((mo)->z.service_ticks); \
    HASH((mo)->z.body_speed); HASH((mo)->z.body_fire); HASH((mo)->z.body_accuracy); HASH((mo)->z.body_tracked); \
    HASH((mo)->z.manufactured); \
    HASH((mo)->z.crew_count); \
    for (int z_i = 0; z_i < (mo)->z.crew_count && z_i < Z_CREW_MAX; ++z_i) { \
        HASH((mo)->z.crew[z_i].hp); HASH((mo)->z.crew[z_i].robot); HASH((mo)->z.crew[z_i].type); \
        HASH((mo)->z.crew[z_i].grenades); HASH((mo)->z.crew[z_i].home); HASH((mo)->z.crew[z_i].flame_phase); HASH((mo)->z.crew[z_i].manufactured); \
    } \
} while (0)

#endif
