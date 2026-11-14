#define _POSIX_C_SOURCE 200809L
#include "t_local.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

#define CHECK(c) RTS_CHECK(c, "z APC scanner", #c)

static void put(mobj_t *unit, ivec2_t at) {
    fixed2_t centre;
    if (Z_UnitCentre(level.native_data, Z_UnitType(unit), at, &centre))
        unit->core.position = fixed3_from_fixed2(centre, 0);
}

static mobj_t *spawn(int type, int team, ivec2_t at) {
    mobj_t *unit = P_SpawnMobj(fixed3_zero(), type);
    if (unit) {
        Z_SetTeam(unit, team);
        put(unit, at);
    }
    return unit;
}

static const mobj_t *flame(int n) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *unit = (const mobj_t *)th;
        if (th->function == P_MobjThinker && unit->type_id == MT_PYRO_PROJECTILE && n-- == 0)
            return unit;
    }
    return NULL;
}

static bool aims_at(const mobj_t *shot, ivec2_t at) {
    return shot && ivec2_equal(at, fixed2_cell(fixed2_scale(shot->movement.goal, FIXED_FROM_INT(Z_TILE))));
}

int main(void) {
    static zmap_t map;
    static uint8_t blocked[Z_MAP_STRIDE * Z_MAP_STRIDE];
    static uint16_t tiles[Z_MAP_STRIDE * Z_MAP_STRIDE];
    static uint32_t sight[Z_MAP_STRIDE * Z_MAP_STRIDE];
    static uint8_t roots[1024], rsc[272] = {
        [0] = 67, [2 + 66 * 4] = 270 & 255, [2 + 66 * 4 + 1] = 270 >> 8,
        [270] = 16, [271] = 2,
    };
    /* Synthetic assets isolate the scan from retail file availability.
     * The only RSC header used is the robot's 16 x 16 sprite. */
    for (int i = 0, rank = 1; i < 1024; ++i) {
        if (i == (rank + 1) * (rank + 1)) ++rank;
        roots[i] = (uint8_t)rank;
    }
    map.flag_players = 2;
    map.rsc = (blob_t){.bytes = rsc, .size = sizeof(rsc)};
    map.square_roots = (blob_t){.bytes = roots, .size = sizeof(roots)};
    level = (level_t){.width = Z_MAP_STRIDE, .height = Z_MAP_STRIDE, .native_data = &map,
                      .blocked = blocked, .tile_ids = tiles};
    snprintf(level.map_path, sizeof(level.map_path), "%s", __FILE__);
    P_InitThinkers();

    /* Equal rank across bins. The north bin is later even though its unit
     * was created first. East has its own winner and west breaks a same-bin tie. */
    mobj_t *apc = spawn(MT_UNIT_APC, 0, (ivec2_t){320, 260});
    mobj_t *north = spawn(MT_UNIT_GRUNT, 1, (ivec2_t){320, 252});
    mobj_t *same_bin = spawn(MT_UNIT_GRUNT, 1, (ivec2_t){320, 256});
    mobj_t *east = spawn(MT_UNIT_GRUNT, 1, (ivec2_t){336, 260});
    mobj_t *west = spawn(MT_UNIT_GRUNT, 1, (ivec2_t){304, 260});
    mobj_t *west_later = spawn(MT_UNIT_GRUNT, 1, (ivec2_t){304, 261});
    CHECK(apc && north && same_bin && east && west && west_later);
    apc->core.angle = ANG90;
    apc->z.driver = MT_UNIT_PYRO - MT_UNITS + 1;
    apc->z.crew_count = 4;
    apc->z.crew[0].flame_phase = 9;
    apc->attack.target = east;
    A_Look(apc);
    CHECK(apc->core.state_id == S_APC_FIRE && apc->core.angle == ANG90);
    /* The save stores the thinker ring in scan order, including its ties. */
    level.sight.cells = sight;
    for (size_t i = 0; i < sizeof(sight) / sizeof(*sight); ++i) sight[i] = UINT32_MAX;
    level.sight.allies[0] = UINT32_MAX;
    app_t app = {0};
    hudtext_t hud = {0};
    static AiContext ai;
    void *save = NULL;
    size_t size = 0;
    uint32_t ids[] = {apc->id, north->id, same_bin->id, east->id, west->id, west_later->id};
    CHECK(G_SaveGameBlob(&save, &size, "APC scan order", &app, &ai, &hud));
    CHECK(P_TickMobjState(apc) && P_TickMobjState(apc));
    CHECK(apc->core.state_id == S_APC_SHOT);
    uint32_t hash = G_Consistency();
    CHECK(G_LoadGameBlob(save, size, &app, &ai, &hud));
    free(save);
    apc = P_MobjById(ids[0]); north = P_MobjById(ids[1]); same_bin = P_MobjById(ids[2]);
    east = P_MobjById(ids[3]); west = P_MobjById(ids[4]); west_later = P_MobjById(ids[5]);
    CHECK(apc && north && same_bin && east && west && west_later);
    CHECK(P_TickMobjState(apc) && P_TickMobjState(apc) && G_Consistency() == hash);
    level.sight.cells = NULL;
    CHECK(aims_at(flame(0), (ivec2_t){320, 252}));
    CHECK(aims_at(flame(1), (ivec2_t){336, 260}));
    CHECK(aims_at(flame(2), (ivec2_t){304, 261}));
    CHECK(!flame(3) && apc->z.crew[0].flame_phase == 10 && apc->attack.cooldown_left_ms > 0);
    CHECK(apc->attack.target == east && apc->core.angle == ANG90);
    CHECK(!P_Attack(apc));

    P_RemoveMobj(north);
    P_RemoveMobj(same_bin);
    P_RemoveMobj(west);
    P_RemoveMobj(west_later);
    put(east, (ivec2_t){449, 260});
    apc->attack.cooldown_left_ms = 0;
    CHECK(!P_Attack(apc) && apc->z.crew[0].flame_phase == 10 && !apc->attack.cooldown_left_ms);
    put(east, (ivec2_t){448, 260});
    CHECK(Z_APCScannerReady(apc));
    mobj_t *out_of_reach = spawn(MT_UNIT_GRUNT, 1, (ivec2_t){449, 260});
    CHECK(out_of_reach);
    CHECK(!Z_APCScannerReady(apc)); /* Later tied rank wins, then fails reach. */
    P_RemoveMobj(out_of_reach);
    /* The ray uses the terrain plane. Its class-1 source samples height at
     * +8 but traces from +16, so the chosen blocker is strictly between. */
    map.terrain[17 * Z_MAP_STRIDE + 26] = 6;
    CHECK(Z_APCScannerReady(apc)); /* Adjacent row to the actual ray. */
    map.terrain[16 * Z_MAP_STRIDE + 26] = 6;
    CHECK(!Z_APCScannerReady(apc));
    map.terrain[16 * Z_MAP_STRIDE + 26] = 0;

    /* The closest enemy is the 33rd indexed entry of its bin. Allied
     * units fill it before the search filters by team. */
    P_RemoveMobj(east);
    mobj_t *first = NULL;
    for (int i = 0; i < 32; ++i) {
        mobj_t *ally = spawn(MT_UNIT_GRUNT, 0, (ivec2_t){420, 260});
        CHECK(ally);
        if (!first) first = ally;
    }
    east = spawn(MT_UNIT_GRUNT, 1, (ivec2_t){400, 260});
    CHECK(east);
    CHECK(!Z_APCScannerReady(apc));
    P_RemoveMobj(first);
    CHECK(Z_APCScannerReady(apc));

    P_FreeThinkers();
    puts("PASS: Z APC bin and slot order, cap, reach, terrain, and phase-gated multi-target fire");
    return 0;
}
