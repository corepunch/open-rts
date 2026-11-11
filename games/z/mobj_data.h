#ifndef __MOBJ_DATA__
#define __MOBJ_DATA__

/* A robot sent to drive an empty vehicle or gun names it in `board`; a
 * manned one keeps its driver's weapon type + 1 in `driver`. Robots of a
 * starting group share a `squad` and the `grenades` its crates gave them.
 * A flag carries its territory + 1 and the tics before it can be taken
 * again. */
#define MOBJ_GAME_FIELDS struct { \
    uint32_t board; \
    uint32_t rating; /* Native building +0x0c production technology. */ \
    uint32_t service; /* Stable target ID for a crane or vehicle at a repair bay. */ \
    int service_work; /* Rebuild progress on a wreck, or negative repair stock on a vehicle. */ \
    uint16_t cliff_bit; \
    uint16_t robot; /* ROBOTS.DAT record + 1, retained for native AI valuation. */ \
    uint8_t cliff_block, home; \
    uint8_t object, building; /* Native OBJECT type / BUILD record index + 1. */ \
    uint8_t driver, squad, grenades, territory; \
    uint8_t crew_hurt; /* Damage the driver has taken from bullets. */ \
    uint8_t bridge; /* BRIDGEnn.DAT slot + 1. */ \
    uint8_t service_ticks; /* Facility work ticks since the last HP step. */ \
    bool manufactured; /* CPU unit flag 0x10, set by 0x5103e. */ \
    int hold; \
} z;
#define MOBJ_GAME_CHECKSUM(HASH, mo) do { \
    HASH((mo)->z.board); HASH((mo)->z.rating); HASH((mo)->z.driver); HASH((mo)->z.squad); HASH((mo)->z.grenades); \
    HASH((mo)->z.territory); HASH((mo)->z.hold); \
    HASH((mo)->z.cliff_block); HASH((mo)->z.cliff_bit); HASH((mo)->z.home); \
    HASH((mo)->z.robot); HASH((mo)->z.object); HASH((mo)->z.building); HASH((mo)->z.crew_hurt); \
    HASH((mo)->z.service); HASH((mo)->z.service_work); HASH((mo)->z.bridge); HASH((mo)->z.service_ticks); \
    HASH((mo)->z.manufactured); \
} while (0)

#endif
