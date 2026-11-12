#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

#include <stdio.h>
#include <stdlib.h>

static int fail(const char *msg) { return rts_fail("z combat", msg); }

static RtsGameModel *load(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/Z" };
    if (model && rts_game_model_load(model, &config)) return model;
    rts_game_model_destroy(model);
    return NULL;
}

static bool alive(const mobj_t *target) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if ((const mobj_t *)th == target && th->function == P_MobjThinker) return true;
    return false;
}

static mobj_t *nth(int type, int team, int n) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->type_id == type && mo->hp > 0 &&
            (team < 0 || mo->team == team) && n-- == 0) return mo;
    }
    return NULL;
}

/* The `n`th live member of `squad`. */
static mobj_t *member(int squad, int n) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->hp > 0 && mo->z.squad == squad && n-- == 0) return mo;
    }
    return NULL;
}

/* A computer Grunt of another squad than `squad`. */
static mobj_t *other_squad(int squad) {
    for (int i = 0; nth(MT_UNIT_GRUNT, 1, i); ++i)
        if (nth(MT_UNIT_GRUNT, 1, i)->z.squad != squad) return nth(MT_UNIT_GRUNT, 1, i);
    return NULL;
}

static bool tick(RtsGameModel *model, int tics) {
    RtsRenderSnapshot snap;
    for (int t = 0; t < tics; ++t)
        if (!rts_tick(model, &snap)) return false;
    return true;
}

static void place(mobj_t *mo, ivec2_t cell) {
    mo->core.position = fixed3_from_fixed2(fixed2_cell_center(cell), mo->core.position.z);
    P_ClearMove(mo);
    mo->movement.goal = fixed3_xy(mo->core.position);
    mo->movement.order_arrived = true;
}

static void disarm_except(int squad, const mobj_t *keep);

static mobj_t *blast_unit(int type, int team, fixed2_t at, int hp) {
    mobj_t *mo = P_SpawnMobj(fixed3_from_fixed2(at, 0), (uint16_t)type);
    if (!mo) return NULL;
    Z_SetTeam(mo, team);
    mo->hp = mo->max_hp = hp;
    return mo;
}

static fixed2_t pixel_at(fixed2_t at, int x, int y) {
    return fixed2_add(at, (fixed2_t){x * (FIXED_ONE / Z_TILE), y * (FIXED_ONE / Z_TILE)});
}

static void jeep_terrain(const mobj_t *jeep, uint8_t terrain) {
    zmap_t *map = level.native_data;
    ivec2_t origin = ivec2_sub(Z_BlastAnchor(jeep), (ivec2_t){20, 23});
    ivec2_t tile = {(origin.x + 7) / Z_TILE, (origin.y + 7) / Z_TILE};
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x)
        map->terrain[(tile.y + y) * Z_MAP_STRIDE + tile.x + x] = terrain;
}

static int test_grunt_bullet(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load bullet flight");
    fixed2_t at = fixed2_cell_center((ivec2_t){40, 40});
    mobj_t *shooter = blast_unit(MT_UNIT_GRUNT, 0, at, 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 112, 0), 500);
    mobj_t *blocker = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 80, 0), 500);
    if (!shooter || !target || !blocker) return fail("spawn bullet flight fixture");
    zmap_t *map = level.native_data;
    for (int y = 39; y <= 41; ++y) for (int x = 39; x <= 48; ++x)
        map->ground_attr[y * Z_MAP_STRIDE + x] = map->terrain[y * Z_MAP_STRIDE + x] = 0;
    disarm_except(0, shooter);
    shooter->z.body_tracked = true;
    shooter->z.body_accuracy = 127;
    shooter->attack.target = target;
    P_UpdateSight();
    if (!P_Attack(shooter) || target->hp != 500 || blocker->hp != 500 || !nth(MT_BULLET, 0, 0))
        return fail("rifle launches a bullet before applying damage");
    shooter->traits &= ~MF_ATTACK;
    if (!tick(model, 1) || !nth(MT_BULLET, 0, 0) || blocker->hp != 500)
        return fail("bullet remains in flight after its first ticker");
    mobj_t *bullet = nth(MT_BULLET, 0, 0);
    uint32_t bullet_id = bullet->id, blocker_id = blocker->id, target_id = target->id;
    fixed3_t position = bullet->core.position;
    int remaining = bullet->z.water.remaining;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t bytes = 0;
    if (!G_SaveGameBlob(&save, &bytes, "Bullet flight", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, bytes, &app, rts_game_model_ai(model), &hud)) return fail("save bullet flight");
    free(save);
    bullet = P_MobjById(bullet_id); blocker = P_MobjById(blocker_id); target = P_MobjById(target_id);
    if (!bullet || !blocker || !target || bullet->z.water.remaining != remaining ||
        bullet->core.position.x != position.x || bullet->core.position.y != position.y)
        return fail("bullet flight state survives reload");
    for (int n = 0; n < 8 && blocker->hp == 500; ++n)
        if (!tick(model, 1)) return fail("tick bullet interception");
    if (blocker->hp >= 500 || target->hp != 500 || P_MobjById(bullet_id))
        return fail("intervening enemy receives bullet instead of aimed target");
    rts_game_model_destroy(model);

    model = load();
    if (!model) return fail("load bullet terrain");
    map = level.native_data;
    for (int y = 39; y <= 41; ++y) for (int x = 39; x <= 48; ++x)
        map->ground_attr[y * Z_MAP_STRIDE + x] = 0;
    for (int y = 39; y <= 41; ++y) map->ground_attr[y * Z_MAP_STRIDE + 43] = 1;
    Z_RebuildCliffs(&level);
    shooter = blast_unit(MT_UNIT_GRUNT, 0, at, 500);
    target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 112, 0), 500);
    if (!shooter || !target) return fail("spawn bullet terrain fixture");
    disarm_except(0, NULL);
    shooter->z.body_tracked = true; shooter->z.body_accuracy = 127;
    if (!Z_Attack(shooter, &shooter->info->attack, target) || !(bullet = nth(MT_BULLET, 0, 0)))
        return fail("launch bullet toward raised terrain");
    bullet_id = bullet->id;
    P_RemoveMobj(shooter);
    for (int n = 0; n < 8 && P_MobjById(bullet_id); ++n)
        if (!tick(model, 1)) return fail("tick bullet into raised terrain");
    if (P_MobjById(bullet_id) || target->hp != 500)
        return fail("raised terrain intercepts bullet after shooter removal");
    for (int y = 39; y <= 41; ++y) map->ground_attr[y * Z_MAP_STRIDE + 43] = 0;
    Z_RebuildCliffs(&level);
    shooter = blast_unit(MT_UNIT_GRUNT, 0, at, 500);
    if (!shooter) return fail("spawn bullet orphan fixture");
    shooter->z.body_tracked = true; shooter->z.body_accuracy = 127;
    if (!Z_Attack(shooter, &shooter->info->attack, target) || !(bullet = nth(MT_BULLET, 0, 0)))
        return fail("launch bullet from removed shooter");
    bullet_id = bullet->id;
    P_RemoveMobj(shooter);
    for (int n = 0; n < 8 && P_MobjById(bullet_id); ++n)
        if (!tick(model, 1)) return fail("tick orphaned bullet");
    if (P_MobjById(bullet_id) || target->hp >= 500)
        return fail("bullet can hit after its shooter is removed");
    shooter = blast_unit(MT_UNIT_GRUNT, 0, at, 500);
    if (!shooter) return fail("spawn moving bullet target fixture");
    shooter->z.body_tracked = true; shooter->z.body_accuracy = 127;
    int hp = target->hp;
    if (!Z_Attack(shooter, &shooter->info->attack, target) || !(bullet = nth(MT_BULLET, 0, 0)))
        return fail("launch bullet at moving target");
    bullet_id = bullet->id;
    target->core.position = fixed3_from_fixed2(pixel_at(at, 112, 32), 0);
    P_RemoveMobj(shooter);
    for (int n = 0; n < 8 && P_MobjById(bullet_id); ++n)
        if (!tick(model, 1)) return fail("tick bullet past moved target");
    if (P_MobjById(bullet_id) || target->hp != hp)
        return fail("bullet keeps its original aim when target moves");
    rts_game_model_destroy(model);
    return 0;
}

static int test_bullet_terrain_gate(void) {
    static const struct { int y, here, below; bool blocked; } cases[] = {
        {7, 1, 0, true}, {8, 1, 0, false}, {8, 1, 1, true},
        {8, 0, 1, false}, {7, 0x81, 0, true},
    };
    RtsGameModel *model = load();
    if (!model) return fail("load bullet terrain gate");
    zmap_t *map = level.native_data;
    fixed2_t at = fixed2_cell_center((ivec2_t){40, 40});
    mobj_t *shooter = blast_unit(MT_UNIT_GRUNT, 0, pixel_at(at, -48, 0), 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, at, 500);
    if (!shooter || !target) return fail("spawn bullet terrain gate fixture");
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        ivec2_t start = {40 * Z_TILE + 4, 40 * Z_TILE + cases[i].y};
        ivec2_t impact = ivec2_add(start, (ivec2_t){1, 0});
        target->core.position = fixed3_from_fixed2(fixed2_scale(fixed2_from_cell(impact), FIXED_ONE / Z_TILE), 0);
        target->hp = 500;
        if (!Z_Attack(shooter, &shooter->info->attack, target)) return fail("launch terrain gate bullet");
        mobj_t *bullet = nth(MT_BULLET, 0, 0);
        if (!bullet) return fail("spawn terrain gate bullet");
        bullet->core.position = fixed3_from_fixed2(fixed2_scale(fixed2_from_cell(start), FIXED_ONE / Z_TILE), 0);
        bullet->z.water.start = start;
        bullet->z.water.direction = (ivec2_t){1, 0};
        bullet->z.water.vertical = 0;
        bullet->z.water.major = bullet->z.water.remaining = 90;
        bullet->z.water.minor = 0; bullet->z.water.error = 45;
        bullet->z.water.speed = 1; bullet->z.water.height = 255;
        map->terrain[40 * Z_MAP_STRIDE + 40] = cases[i].here;
        map->terrain[41 * Z_MAP_STRIDE + 40] = cases[i].below;
        A_ZBullet(bullet);
        if ((target->hp == 500) != cases[i].blocked)
            return fail("bullet terrain gate precedes unit collision and checks lower half");
        P_RemoveMobj(bullet);
    }
    rts_game_model_destroy(model);
    return 0;
}

static int test_specialist_bullets(void) {
    static const struct { int type, range, damage; } cases[] = {
        {MT_UNIT_SNIPER, 144, 10}, {MT_UNIT_PSYCHO, 120, 2}, {MT_UNIT_LASER, 136, 15},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        RtsGameModel *model = load();
        if (!model) return fail("load specialist bullet");
        zmap_t *map = level.native_data;
        for (int y = 39; y <= 41; ++y) for (int x = 39; x <= 47; ++x)
            map->ground_attr[y * Z_MAP_STRIDE + x] = 0;
        Z_RebuildCliffs(&level);
        fixed2_t at = fixed2_cell_center((ivec2_t){40, 40});
        mobj_t *shooter = blast_unit(cases[i].type, 0, at, 500);
        mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 80, 0), 500);
        if (!shooter || !target) return fail("spawn specialist bullet fixture");
        disarm_except(0, shooter);
        shooter->z.body_tracked = true; shooter->z.body_accuracy = 127;
        shooter->attack.target = target;
        P_UpdateSight();
        ivec2_t start = ivec2_add(Z_AITopLeft(shooter), (ivec2_t){2,2});
        if (!P_Attack(shooter)) return fail("specialist fires at infantry");
        mobj_t *bullet = nth(MT_BULLET, 0, 0);
        if (!bullet || bullet->z.water.speed != 15 || bullet->z.water.range != cases[i].range ||
            bullet->missile.damage != cases[i].damage || !ivec2_equal(bullet->z.water.start, start) || target->hp != 500)
            return fail("specialist bullet keeps native muzzle, speed, range and damage");
        shooter->traits &= ~MF_ATTACK;
        if (!tick(model, 1) || !nth(MT_BULLET, 0, 0) || target->hp != 500)
            return fail("specialist bullet travels before impact");
        uint32_t bullet_id = bullet->id, target_id = target->id, shooter_id = shooter->id;
        app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t bytes = 0;
        if (!G_SaveGameBlob(&save, &bytes, "Specialist bullet", &app, rts_game_model_ai(model), &hud) ||
            !G_LoadGameBlob(save, bytes, &app, rts_game_model_ai(model), &hud)) return fail("save specialist bullet");
        free(save);
        bullet = P_MobjById(bullet_id); target = P_MobjById(target_id); shooter = P_MobjById(shooter_id);
        if (!bullet || !target || !shooter || bullet->z.water.range != cases[i].range || bullet->missile.damage != cases[i].damage)
            return fail("restore specialist bullet range and damage");
        for (int n = 0; n < 8 && P_MobjById(bullet_id); ++n)
            if (!tick(model, 1)) return fail("tick specialist bullet");
        if (P_MobjById(bullet_id) || target->hp != 500 - cases[i].damage)
            return fail("specialist bullet hits after flight");
        mobj_t *jeep = blast_unit(MT_UNIT_JEEP, 1, pixel_at(at, 80, 16), 500);
        if (!jeep) return fail("spawn specialist grenade target");
        shooter->z.grenades = 1;
        if (!Z_Attack(shooter, &shooter->info->attack, jeep)) return fail("specialist attacks vehicle with grenade");
        if (cases[i].type == MT_UNIT_LASER) {
            if (shooter->z.grenades != 1 || !nth(MT_BULLET, 0, 0) || nth(MT_GRENADE, 0, 0))
                return fail("Laser retains grenade and fires bullet at vehicle");
        } else if (shooter->z.grenades || !nth(MT_GRENADE, 0, 0) || nth(MT_BULLET, 0, 0))
            return fail("Sniper and Psycho throw grenades at vehicles");
        rts_game_model_destroy(model);
    }
    return 0;
}

static int test_gatling_bullet(void) {
    static const ivec2_t muzzle[] = {{13,-2},{24,1},{29,9},{22,17},{13,20},{4,18},{-4,9},{2,0}};
    RtsGameModel *model = load();
    if (!model) return fail("load Gatling bullet");
    zmap_t *map = level.native_data;
    for (int y = 38; y <= 43; ++y) for (int x = 38; x <= 47; ++x)
        map->ground_attr[y * Z_MAP_STRIDE + x] = 0;
    Z_RebuildCliffs(&level);
    fixed2_t at = fixed2_cell_center((ivec2_t){40, 40});
    mobj_t *gun = blast_unit(MT_UNIT_GATLING, 0, at, 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 80, 0), 500);
    if (!gun || !target) return fail("spawn Gatling bullet fixture");
    gun->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    gun->z.body_tracked = true; gun->z.body_accuracy = 127;
    ivec2_t origin = Z_AITopLeft(gun);
    for (int facing = 0; facing < 8; ++facing) {
        gun->core.angle = direction_to_angle(facing, 8, ANG90, true);
        if (!Z_Attack(gun, &gun->info->attack, target)) return fail("launch Gatling facing");
        mobj_t *bullet = nth(MT_BULLET, 0, 0);
        if (!bullet || !ivec2_equal(bullet->z.water.start, ivec2_add(origin, muzzle[facing])) ||
            bullet->z.water.speed != 15 || bullet->z.water.range != 120 || bullet->missile.damage != 6)
            return fail("Gatling uses native facing muzzle and bullet stats");
        P_RemoveMobj(bullet);
    }
    disarm_except(0, gun);
    gun->core.angle = direction_to_angle(2, 8, ANG90, true);
    gun->attack.target = target;
    P_UpdateSight();
    if (!P_Attack(gun)) return fail("Gatling fires through attack dispatch");
    mobj_t *bullet = nth(MT_BULLET, 0, 0);
    if (!bullet || target->hp != 500) return fail("Gatling does not hit instantly");
    gun->traits &= ~MF_ATTACK;
    if (!tick(model, 1) || !nth(MT_BULLET, 0, 0)) return fail("Gatling bullet remains in flight");
    uint32_t bullet_id = bullet->id, target_id = target->id, hash = G_Consistency();
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t bytes = 0;
    if (!G_SaveGameBlob(&save, &bytes, "Gatling bullet", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, bytes, &app, rts_game_model_ai(model), &hud)) return fail("save Gatling bullet");
    free(save);
    bullet = P_MobjById(bullet_id); target = P_MobjById(target_id);
    if (!bullet || !target || G_Consistency() != hash) return fail("restore Gatling bullet flight");
    for (int n = 0; n < 8 && P_MobjById(bullet_id); ++n)
        if (!tick(model, 1)) return fail("tick Gatling bullet");
    if (P_MobjById(bullet_id) || target->hp != 488)
        return fail("Gatling endpoint checks can both hit the target");
    rts_game_model_destroy(model);
    return 0;
}

static int test_jeep_bullet(void) {
    static const ivec2_t body[] = {{2,8},{-5,11},{-10,6},{-6,0},{0,0},{12,0},{14,5},{10,11}};
    static const ivec2_t gun[] = {{8,-2},{9,-1},{13,1},{10,4},{6,4},{3,2},{0,0},{4,-4}};
    RtsGameModel *model = load();
    if (!model) return fail("load Jeep bullet");
    zmap_t *map = level.native_data;
    for (int y = 38; y <= 43; ++y) for (int x = 38; x <= 48; ++x)
        map->ground_attr[y * Z_MAP_STRIDE + x] = 0;
    Z_RebuildCliffs(&level);
    fixed2_t at = fixed2_cell_center((ivec2_t){40, 40});
    mobj_t *jeep = blast_unit(MT_UNIT_JEEP, 0, at, 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 96, 0), 500);
    if (!jeep || !target) return fail("spawn Jeep bullet fixture");
    jeep->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    jeep->z.body_tracked = true; jeep->z.body_accuracy = 127;
    ivec2_t origin = Z_AITopLeft(jeep);
    for (int hull = 0; hull < 8; ++hull) for (int facing = 0; facing < 8; ++facing) {
        jeep->core.angle = direction_to_angle(hull, 8, ANG90, true);
        jeep->attack.aim = direction_to_angle(facing, 8, ANG90, true) - jeep->core.angle;
        if (!Z_Attack(jeep, &jeep->info->attack, target)) return fail("launch Jeep facing");
        mobj_t *bullet = nth(MT_BULLET, 0, 0);
        if (!bullet || !ivec2_equal(bullet->z.water.start, ivec2_add(origin, ivec2_add(body[hull], gun[facing]))) ||
            bullet->z.water.speed != 15 || bullet->z.water.range != 128 || bullet->missile.damage != 4)
            return fail("Jeep uses native hull and gun offsets and bullet stats");
        P_RemoveMobj(bullet);
    }
    disarm_except(0, jeep);
    jeep->core.angle = direction_to_angle(2, 8, ANG90, true);
    jeep->attack.aim = 0;
    jeep->attack.target = target;
    P_UpdateSight();
    if (!P_Attack(jeep)) return fail("Jeep fires through attack dispatch");
    mobj_t *bullet = nth(MT_BULLET, 0, 0);
    if (!bullet || target->hp != 500) return fail("Jeep does not hit instantly");
    jeep->traits &= ~MF_ATTACK;
    if (!tick(model, 1) || !nth(MT_BULLET, 0, 0)) return fail("Jeep bullet remains in flight");
    uint32_t bullet_id = bullet->id, jeep_id = jeep->id, target_id = target->id, hash = G_Consistency();
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t bytes = 0;
    if (!G_SaveGameBlob(&save, &bytes, "Jeep bullet", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, bytes, &app, rts_game_model_ai(model), &hud)) return fail("save Jeep bullet");
    free(save);
    bullet = P_MobjById(bullet_id); target = P_MobjById(target_id); jeep = P_MobjById(jeep_id);
    if (!bullet || !target || !jeep || bullet->z.water.range != 128 || G_Consistency() != hash)
        return fail("restore Jeep bullet flight");
    P_RemoveMobj(jeep);
    for (int n = 0; n < 8 && P_MobjById(bullet_id); ++n)
        if (!tick(model, 1)) return fail("tick Jeep bullet");
    if (P_MobjById(bullet_id) || target->hp >= 500) return fail("Jeep bullet hits after shooter removal");
    rts_game_model_destroy(model);
    return 0;
}

static int test_bad_crew_combat(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for malformed crew combat");
    fixed2_t at = fixed2_cell_center((ivec2_t){5, 5});
    mobj_t *grunt = blast_unit(MT_UNIT_GRUNT, 0, at, 500);
    mobj_t *jeep = blast_unit(MT_UNIT_JEEP, 1, at, 500);
    mobj_t *apc = blast_unit(MT_UNIT_APC, 0, at, 500);
    if (!grunt || !jeep || !apc) return fail("spawn malformed crew combat units");
    grunt->z.body_tracked = true;
    grunt->z.body_accuracy = 127;
    jeep->z.driver = 255;
    for (int i = 0; i < 128; ++i, ++leveltime)
        Z_HitDamage(grunt, &grunt->info->attack, jeep, 10, 1);
    if (jeep->z.crew_hurt || jeep->z.driver != 255) return fail("invalid driver cannot be hit");
    apc->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    apc->z.crew_count = 255;
    if (Z_HitDamage(apc, &apc->info->attack, grunt, 10, 1) != 0)
        return fail("oversized APC crew cannot amplify hitscan damage");
    apc->z.driver = MT_UNIT_TOUGH - MT_UNITS + 1;
    if (!Z_Attack(apc, &apc->info->attack, grunt) || nth(MT_SHELL, 0, 0))
        return fail("oversized APC crew cannot fire a shell or fall back to hitscan");
    apc->z.crew_count = 1;
    if (!Z_Attack(apc, &apc->info->attack, grunt) || !nth(MT_SHELL, 0, 0))
        return fail("valid APC crew can fire a shell");
    rts_game_model_destroy(model);
    puts("PASS: z malformed crews cannot affect combat");
    return 0;
}

static int test_apc_driver_cooldown(void) {
    static const struct { int type, steps; } drivers[] = {
        {MT_UNIT_GRUNT, 30}, {MT_UNIT_TOUGH, 30}, {MT_UNIT_SNIPER, 30},
        {MT_UNIT_PSYCHO, 3}, {MT_UNIT_PYRO, 3}, {MT_UNIT_LASER, 30},
    };
    RtsGameModel *model = load();
    if (!model) return fail("load for APC driver cooldown");
    fixed2_t at = fixed2_cell_center((ivec2_t){5, 5});
    mobj_t *apc = blast_unit(MT_UNIT_APC, 0, at, 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 16, 0), 5000);
    if (!apc || !target) return fail("spawn APC cooldown fixture");
    apc->attack.target = target;
    apc->z.body_tracked = true; apc->z.body_fire = 1;
    P_UpdateSight();
    for (size_t i = 0; i < sizeof(drivers) / sizeof(*drivers); ++i) {
        apc->z.driver = drivers[i].type - MT_UNITS + 1;
        apc->z.crew_count = 1;
        apc->z.crew[0] = (zcrew_t){.hp = 30, .type = drivers[i].type - MT_UNITS};
        if (!P_Attack(apc) || apc->attack.cooldown_left_ms != drivers[i].steps * 1000 / Z_NATIVE_HZ)
            return fail("APC reload uses driver rate plus half");
    }
    mobj_t *driver = blast_unit(MT_UNIT_PSYCHO, 0, at, 50);
    mobj_t *empty = blast_unit(MT_UNIT_APC, Z_NEUTRAL, at, 500);
    if (!driver || !empty) return fail("spawn APC boarding cooldown fixture");
    empty->traits &= ~(MF_MOBILE | MF_ATTACK);
    P_SetMobjState(empty, S_A_P_C);
    if (!Z_DetourBoard(driver, empty) || empty->z.driver != MT_UNIT_PSYCHO - MT_UNITS + 1)
        return fail("Psycho boards empty APC");
    uint32_t id = empty->id;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!G_SaveGameBlob(&save, &size, "APC driver", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud))
        return fail("save APC driver cooldown");
    free(save);
    empty = P_MobjById(id);
    if (!empty || G_WeaponCooldown(empty, &empty->info->attack, empty->info->attack.cooldown_ms) != 3 * 1000 / Z_NATIVE_HZ)
        return fail("APC retains its boarded driver's reload after load");
    rts_game_model_destroy(model);
    return 0;
}

static int apc_firing_interval(int driver_type) {
    RtsGameModel *model = load();
    if (!model) return -1;
    fixed2_t at = fixed2_cell_center((ivec2_t){5, 5});
    mobj_t *apc = blast_unit(MT_UNIT_APC, 0, at, 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 16, 0), 5000);
    if (!apc || !target) { rts_game_model_destroy(model); return -1; }
    apc->z.driver = driver_type - MT_UNITS + 1;
    apc->z.crew_count = 1;
    apc->z.crew[0] = (zcrew_t){.hp = 30, .type = driver_type - MT_UNITS};
    apc->attack.target = target;
    P_UpdateSight();
    int first = -1, interval = -1, hp = target->hp;
    for (int t = 0; t < 10 * RTS_TICRATE && interval < 0; ++t) {
        if (!tick(model, 1)) break;
        if (target->hp < hp) {
            if (first >= 0) interval = t - first;
            else first = t;
            hp = target->hp;
        }
    }
    rts_game_model_destroy(model);
    return interval;
}

static int test_apc_driver_range(void) {
    static const struct { int type, range; } drivers[] = {
        {MT_UNIT_GRUNT, 120}, {MT_UNIT_TOUGH, 120}, {MT_UNIT_SNIPER, 144},
        {MT_UNIT_PSYCHO, 120}, {MT_UNIT_PYRO, 120}, {MT_UNIT_LASER, 136},
    };
    RtsGameModel *model = load();
    if (!model) return fail("load for APC driver range");
    fixed2_t at = fixed2_cell_center((ivec2_t){10, 10});
    mobj_t *apc = blast_unit(MT_UNIT_APC, 0, at, 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 128, 0), 500);
    if (!apc || !target) return fail("spawn APC driver range fixture");
    for (size_t i = 0; i < sizeof(drivers) / sizeof(*drivers); ++i) {
        apc->z.driver = drivers[i].type - MT_UNITS + 1;
        target->core.position = fixed3_from_fixed2(pixel_at(at, drivers[i].range, 0), 0);
        if (!P_InAttackRange(apc, target)) return fail("APC reaches driver's exact range");
        target->core.position = fixed3_from_fixed2(pixel_at(at, drivers[i].range + 1, 0), 0);
        if (P_InAttackRange(apc, target)) return fail("APC cannot reach past driver's range");
    }
    target->core.position = fixed3_from_fixed2(pixel_at(at, 128, 0), 0);
    apc->attack.target = target;
    P_UpdateSight();
    apc->z.driver = MT_UNIT_SNIPER - MT_UNITS + 1;
    if (P_AttackTarget(apc) != target) return fail("Sniper APC acquires target within inherited range");
    apc->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    if (P_AttackTarget(apc) == target) return fail("Grunt APC does not acquire target outside inherited range");
    mobj_t *driver = blast_unit(MT_UNIT_SNIPER, 0, at, 50);
    mobj_t *empty = blast_unit(MT_UNIT_APC, Z_NEUTRAL, at, 500);
    if (!driver || !empty) return fail("spawn APC range boarding fixture");
    empty->traits &= ~(MF_MOBILE | MF_ATTACK);
    P_SetMobjState(empty, S_A_P_C);
    if (!Z_DetourBoard(driver, empty)) return fail("Sniper boards APC");
    target->core.position = fixed3_from_fixed2(pixel_at(fixed3_xy(empty->core.position), 128, 0), 0);
    if (!P_InAttackRange(empty, target)) return fail("boarded Sniper gives APC longer range");
    uint32_t apc_id = empty->id, target_id = target->id;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!G_SaveGameBlob(&save, &size, "APC range", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud))
        return fail("save APC driver range");
    free(save);
    empty = P_MobjById(apc_id); target = P_MobjById(target_id);
    if (!empty || !target || !P_InAttackRange(empty, target))
        return fail("APC retains boarded driver range after load");
    rts_game_model_destroy(model);
    return 0;
}

static int test_apc_firing_intervals(void) {
    int grunt = apc_firing_interval(MT_UNIT_GRUNT);
    int psycho = apc_firing_interval(MT_UNIT_PSYCHO);
    if (psycho < 1 || grunt <= psycho) return fail("driver cooldown changes real APC firing intervals");
    return 0;
}

static int test_pyro_cadence(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for Pyro cadence");
    fixed2_t at = fixed2_cell_center((ivec2_t){5, 5});
    mobj_t *pyro = blast_unit(MT_UNIT_PYRO, 0, at, 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 16, 0), 1000);
    if (!pyro || !target) return fail("spawn Pyro cadence fixture");
    pyro->attack.target = target;
    P_UpdateSight();
    int times[5], count = 0, hp = target->hp;
    for (int tic = 0; tic < 10 * RTS_TICRATE && count < 5; ++tic) {
        if (!tick(model, 1)) return fail("tick Pyro cadence");
        if (target->hp < hp) { times[count++] = tic; hp = target->hp; }
    }
    if (count != 5) return fail("Pyro fires five bursts");
    if (times[1] - times[0] != 2 || times[2] - times[1] != 2 ||
        times[3] - times[2] != 2 || times[4] - times[3] != 9)
        return fail("four-burst pause changes real firing timestamps");
    pyro->attack.shots = 3;
    P_SetMobjState(pyro, S_PYRO_RECOVER);
    int recovery = pyro->core.tics;
    uint32_t pyro_id = pyro->id, target_id = target->id;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!G_SaveGameBlob(&save, &size, "Pyro recovery", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud))
        return fail("save Pyro recovery");
    free(save);
    pyro = P_MobjById(pyro_id);
    target = P_MobjById(target_id);
    if (!pyro || !target || pyro->core.state_id != S_PYRO_RECOVER || pyro->core.tics != recovery || pyro->attack.shots)
        return fail("load preserves Pyro recovery and cycle");
    mobj_t *vehicle = blast_unit(MT_UNIT_JEEP, 1, pixel_at(at, 32, 0), 500);
    if (!vehicle) return fail("spawn Pyro grenade target");
    pyro->attack.shots = 2;
    pyro->z.grenades = 2;
    pyro->attack.target = vehicle;
    pyro->attack.cooldown_left_ms = 0;
    P_SetMobjState(pyro, S_PYRO_STND);
    if (pyro->core.state_id != S_PYRO_THROW)
        return fail("normal Pyro target selection enters grenade throw state");
    for (int tic = 0; tic < RTS_TICRATE && pyro->z.grenades == 2; ++tic)
        if (!tick(model, 1)) return fail("tick Pyro grenade");
    if (pyro->z.grenades != 1 || pyro->attack.shots != 2 ||
        pyro->attack.cooldown_left_ms != 4 * 1000 / Z_NATIVE_HZ - RTS_TICK_MS)
        return fail("grenade preserves flame cycle and uses its own native cooldown");
    mobj_t *stale = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 16, 0), 500);
    if (!stale) return fail("spawn stale Pyro target");
    pyro->attack.shots = 1;
    pyro->attack.cooldown_left_ms = 0;
    pyro->z.grenades = 2;
    pyro->attack.target = stale;
    P_SetMobjState(pyro, S_PYRO_RECOVER);
    stale->traits |= MF_NOBLOCKMAP;
    target->traits |= MF_NOBLOCKMAP;
    if (!tick(model, 1) || pyro->core.state_id != S_PYRO_THROW ||
        pyro->attack.target != vehicle || pyro->attack.shots != 2 || pyro->z.grenades != 2)
        return fail("Pyro selects grenade throw when its flame target becomes invalid");
    for (int tic = 0; tic < RTS_TICRATE && pyro->z.grenades == 2; ++tic)
        if (!tick(model, 1)) return fail("tick retargeted Pyro grenade");
    if (pyro->z.grenades != 1 || pyro->attack.shots != 2 ||
        pyro->attack.cooldown_left_ms != 4 * 1000 / Z_NATIVE_HZ - RTS_TICK_MS)
        return fail("retargeted grenade preserves flame cycle and throw cooldown");
    pyro->z.grenades = 2;
    pyro->attack.target = stale;
    pyro->attack.cooldown_left_ms = 0;
    P_SetMobjState(pyro, S_PYRO_FLAME);
    if (pyro->core.state_id != S_PYRO_THROW || pyro->attack.target != vehicle ||
        pyro->z.grenades != 2 || pyro->attack.shots != 2)
        return fail("flame action resolves an invalid target before choosing the throw");
    rts_game_model_destroy(model);
    return 0;
}

static int test_grenade_cooldown(void) {
    static const struct { int type, target; } cases[] = {
        {MT_UNIT_GRUNT, MT_UNIT_JEEP}, {MT_UNIT_LASER, MT_ROBOT_FACTORY},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        RtsGameModel *model = load();
        if (!model) return fail("load for grenade cooldown");
        fixed2_t at = fixed2_cell_center((ivec2_t){5, 5});
        mobj_t *robot = blast_unit(cases[i].type, 0, at, 500);
        mobj_t *target = blast_unit(cases[i].target, 1, pixel_at(at, 32, 0), 500);
        if (!robot || !target) return fail("spawn grenade cooldown fixture");
        robot->z.grenades = 1;
        robot->attack.target = target;
        P_UpdateSight();
        for (int t = 0; t < RTS_TICRATE && robot->z.grenades; ++t)
            if (!tick(model, 1)) return fail("tick grenade throw");
        if (robot->z.grenades || robot->attack.cooldown_left_ms != 40 * 1000 / Z_NATIVE_HZ - RTS_TICK_MS)
            return fail("Grunt and Laser grenade throws use doubled native fire delay");
        rts_game_model_destroy(model);
    }
    RtsGameModel *model = load();
    if (!model) return fail("load for changed grenade target");
    fixed2_t at = fixed2_cell_center((ivec2_t){5, 5});
    mobj_t *robot = blast_unit(MT_UNIT_GRUNT, 0, at, 500);
    mobj_t *vehicle = blast_unit(MT_UNIT_JEEP, 1, pixel_at(at, 32, 0), 500);
    mobj_t *infantry = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 16, 0), 500);
    if (!robot || !vehicle || !infantry) return fail("spawn changed grenade target fixture");
    robot->z.grenades = 1;
    robot->attack.target = vehicle;
    vehicle->traits |= MF_NOBLOCKMAP; /* Target became invalid during the throw. */
    P_UpdateSight();
    A_ZToss(robot);
    if (robot->z.grenades != 1 || robot->attack.target != infantry || infantry->hp != 500 || !nth(MT_BULLET, 0, 0) ||
        robot->attack.cooldown_left_ms != 20 * 1000 / Z_NATIVE_HZ)
        return fail("retargeted rifle shot keeps ordinary cooldown and grenade");
    rts_game_model_destroy(model);
    return 0;
}

static int test_gun_drivers(void) {
    static const int types[] = {MT_UNIT_LIGHT_GUN, MT_UNIT_GATLING, MT_UNIT_HOWITZER, MT_UNIT_MISSILE_GUN};
    static const ivec2_t offsets[] = {{-4, 0}, {-4, -1}, {-1, -6}, {0, 0}};
    RtsGameModel *model = load();
    if (!model) return fail("load for gun drivers");
    fixed2_t at = fixed2_cell_center((ivec2_t){5, 5});
    mobj_t *shooter = blast_unit(MT_UNIT_SNIPER, 0, at, 500);
    if (!shooter) return fail("spawn gun driver attacker");
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); ++i) {
        mobj_t *gun = blast_unit(types[i], 1, pixel_at(at, 16, 0), 500);
        if (!gun) return fail("spawn gun driver");
        ivec2_t expected_origin = ivec2_add(ivec2_sub(Z_BlastAnchor(gun), (ivec2_t){16, 16}), offsets[i]);
        gun->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
        for (int n = 0; n < 256 && !gun->z.crew_hurt; ++n, ++leveltime)
            Z_HitDamage(shooter, &shooter->info->attack, gun, 1, 1);
        if (!gun->z.crew_hurt) return fail("all class-2 guns can take driver hits");
        for (int n = 0; n < 256 && gun->z.driver; ++n, ++leveltime)
            Z_HitDamage(shooter, &shooter->info->attack, gun, 255, 1);
        if (gun->z.driver || gun->team != Z_NEUTRAL || !gun->z.object_slot ||
            gun->z.object_hp != 20 || gun->core.state_id != mobjinfo[gun->z.object].spawnstate)
            return fail("fatal driver hit leaves each gun neutral");
        isize2_t size;
        if (!Z_SpriteSize(level.native_data, states[gun->core.state_id].frame, &size) ||
            !ivec2_equal(fixed2_cell(fixed2_scale(fixed3_xy(gun->core.position), FIXED_FROM_INT(Z_TILE))),
                         ivec2_add(expected_origin, (ivec2_t){size.w / 2, size.h / 2})))
            return fail("driverless gun retains native effect origin");
        P_RemoveMobj(gun);
    }
    mobj_t *tank = blast_unit(MT_UNIT_MED_TANK, 1, pixel_at(at, 16, 0), 500);
    if (!tank) return fail("spawn protected tank driver");
    tank->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    for (int n = 0; n < 256; ++n, ++leveltime)
        Z_HitDamage(shooter, &shooter->info->attack, tank, 1, 1);
    if (tank->z.crew_hurt) return fail("ordinary tank does not use the open gun driver rule");
    mobj_t *boundary = blast_unit(MT_UNIT_JEEP, 1, pixel_at(at, 16, 0), 500);
    if (!boundary) return fail("spawn driver health boundary fixture");
    jeep_terrain(boundary, 7);
    ivec2_t jeep_origin = ivec2_sub(Z_BlastAnchor(boundary), (ivec2_t){20, 23});
    ivec2_t rounded = {(jeep_origin.x + 7) / Z_TILE * Z_TILE, (jeep_origin.y + 7) / Z_TILE * Z_TILE};
    boundary->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    boundary->hp = 499;
    boundary->z.crew_count = 1;
    boundary->z.crew[0] = (zcrew_t){.hp = 15, .type = MT_UNIT_GRUNT - MT_UNITS};
    for (int n = 0; n < 256 && !boundary->z.crew_hurt; ++n, ++leveltime)
        Z_HitDamage(shooter, &shooter->info->attack, boundary, 15, 1);
    if (boundary->z.crew_hurt != 15 || !boundary->z.driver || boundary->team != 1)
        return fail("driver at zero health remains aboard at native signed boundary");
    for (int n = 0; n < 256 && boundary->z.driver; ++n, ++leveltime)
        Z_HitDamage(shooter, &shooter->info->attack, boundary, 1, 1);
    if (boundary->remove || boundary->z.driver || boundary->team != Z_NEUTRAL || boundary->hp != 475 || boundary->max_hp != 500 ||
        boundary->z.object != MT_JEEP || boundary->z.object_hp != 19 || !boundary->z.object_slot)
        return fail("driver death empties the Jeep and quantizes body health");
    isize2_t jeep_size;
    if (!Z_SpriteSize(level.native_data, states[S_JEEP].frame, &jeep_size) ||
        !ivec2_equal(fixed2_cell(fixed2_scale(fixed3_xy(boundary->core.position), FIXED_FROM_INT(Z_TILE))),
                     ivec2_add(rounded, (ivec2_t){jeep_size.w / 2, jeep_size.h / 2})))
        return fail("driverless Jeep origin is rounded to the native terrain grid");
    uint32_t boundary_id = boundary->id, initial_shooter_id = shooter->id, tank_id = tank->id;
    app_t jeep_app = {0}; hudtext_t jeep_hud = {0}; void *jeep_save = NULL; size_t jeep_bytes = 0;
    if (!G_SaveGameBlob(&jeep_save, &jeep_bytes, "Empty Jeep", &jeep_app, rts_game_model_ai(model), &jeep_hud) ||
        !G_LoadGameBlob(jeep_save, jeep_bytes, &jeep_app, rts_game_model_ai(model), &jeep_hud))
        return fail("save driverless Jeep");
    free(jeep_save);
    boundary = P_MobjById(boundary_id); shooter = P_MobjById(initial_shooter_id);
    if (!boundary || !shooter || boundary->z.object != MT_JEEP || boundary->z.object_hp != 19 ||
        !boundary->z.object_slot ||
        !ivec2_equal(fixed2_cell(fixed2_scale(fixed3_xy(boundary->core.position), FIXED_FROM_INT(Z_TILE))),
                     ivec2_add(rounded, (ivec2_t){jeep_size.w / 2, jeep_size.h / 2})))
        return fail("restore driverless Jeep position and effect health");
    mobj_t *replacement = blast_unit(MT_UNIT_GRUNT, 0, fixed3_xy(boundary->core.position), 30);
    if (!replacement || !Z_DetourBoard(replacement, boundary) || boundary->hp != 475 || boundary->max_hp != 500)
        return fail("boarding retains the quantized empty-body health");
    mobj_t *fragile = blast_unit(MT_UNIT_JEEP, 1, pixel_at(at, 16, 0), 500);
    if (!fragile) return fail("spawn low-health driver fixture");
    jeep_terrain(fragile, 23);
    fragile->hp = 1; fragile->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    fragile->z.crew_count = 1;
    fragile->z.crew[0] = (zcrew_t){.hp = 1, .type = MT_UNIT_GRUNT - MT_UNITS};
    for (int n = 0; n < 256 && fragile->z.driver; ++n, ++leveltime)
        Z_HitDamage(shooter, &shooter->info->attack, fragile, 2, 1);
    if (fragile->z.driver || fragile->hp != 25)
        return fail("driver loss retains native minimum empty-body health");
    mobj_t *blocked = blast_unit(MT_UNIT_JEEP, 1, pixel_at(at, 64, 0), 500);
    if (!blocked) return fail("spawn blocked driver fixture");
    jeep_terrain(blocked, 0);
    ivec2_t blocked_origin = ivec2_sub(Z_BlastAnchor(blocked), (ivec2_t){20, 23});
    ivec2_t blocked_tile = {(blocked_origin.x + 7) / Z_TILE, (blocked_origin.y + 7) / Z_TILE};
    ((zmap_t *)level.native_data)->terrain[(blocked_tile.y + 1) * Z_MAP_STRIDE + blocked_tile.x + 1] = 1;
    blocked->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    for (int n = 0; n < 256 && !blocked->remove; ++n, ++leveltime)
        Z_HitDamage(shooter, &shooter->info->attack, blocked, 255, 1);
    if (!blocked->remove) return fail("blocked terrain prevents driverless Jeep placement");
    tank = P_MobjById(tank_id);
    if (tank) P_RemoveMobj(tank);
    mobj_t *gun = blast_unit(MT_UNIT_GATLING, 1, pixel_at(at, 16, 0), 500);
    if (!gun) return fail("spawn gun for real attack");
    gun->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    gun->z.crew_count = 1;
    gun->z.crew[0] = (zcrew_t){.hp = 1, .type = MT_UNIT_GRUNT - MT_UNITS};
    shooter->attack.target = gun;
    P_UpdateSight();
    for (int n = 0; n < 256 && gun->z.driver; ++n, ++leveltime) {
        int before = gun->hp;
        if (!P_Attack(shooter)) return fail("fire at gun driver");
        mobj_t *bullet = nth(MT_BULLET, 0, 0);
        if (!bullet) return fail("sniper launches gun driver shot");
        A_ZBullet(bullet);
        if (!gun->z.driver && gun->hp != (before / 25 ? before / 25 : 1) * 25)
            return fail("fatal driver hit quantizes remaining gun body health");
    }
    if (gun->z.driver || gun->team != Z_NEUTRAL) return fail("real attack empties the gun");
    uint32_t gun_id = gun->id, shooter_id = shooter->id;
    int raw_hp = gun->z.object_hp;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t bytes = 0;
    if (!G_SaveGameBlob(&save, &bytes, "Empty gun", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, bytes, &app, rts_game_model_ai(model), &hud)) return fail("save driverless gun");
    free(save);
    gun = P_MobjById(gun_id); shooter = P_MobjById(shooter_id);
    if (!gun || !shooter || !gun->z.object_slot || gun->z.object_hp != raw_hp)
        return fail("restore driverless gun effect");
    isize2_t size;
    if (!Z_SpriteSize(level.native_data, states[S_GATTLING].frame, &size)) return fail("driverless gun sprite");
    ivec2_t centre = fixed2_cell(fixed2_scale(fixed3_xy(gun->core.position), FIXED_FROM_INT(Z_TILE)));
    ivec2_t impact = ivec2_add(ivec2_sub(centre, (ivec2_t){size.w / 2, size.h / 2}), (ivec2_t){8, 8});
    mobj_t *shell = P_SpawnMobj(gun->core.position, MT_SHELL);
    if (!shell) return fail("spawn shell for driverless gun");
    shell->movement.goal = fixed2_scale(fixed2_from_cell(impact), FIXED_ONE / Z_TILE);
    shell->missile.damage = 1;
    A_ZBlastImpact(shell);
    if (gun->z.object_hp != raw_hp - 1 || gun->hp != 25 * (raw_hp - 1)) return fail("shell damages saved driverless gun effect");
    mobj_t *robot = blast_unit(MT_UNIT_GRUNT, 0, fixed3_xy(gun->core.position), 30);
    if (!robot || !Z_DetourBoard(robot, gun) || gun->team != 0 ||
        gun->z.driver != MT_UNIT_GRUNT - MT_UNITS + 1)
        return fail("a robot can board a gun after its driver is killed");
    for (int i = 0; i < 200; ++i) {
        mobj_t *box = blast_unit(MT_GRENADES, Z_NEUTRAL, at, 100);
        if (!box) return fail("spawn full effect pool");
        box->z.object_slot = i + 1;
    }
    mobj_t *full = blast_unit(MT_UNIT_LIGHT_GUN, 1, at, 500);
    if (!full) return fail("spawn driver in full effect pool");
    full->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    for (int n = 0; n < 256 && !full->remove; ++n, ++leveltime)
        Z_HitDamage(shooter, &shooter->info->attack, full, 255, 1);
    if (!full->remove) return fail("driver loss removes gun when represented effect slots are full");
    rts_game_model_destroy(model);
    return 0;
}

static int test_apc_bullet_salvo(void) {
    static const ivec2_t port[] = {{5,-4},{14,-4},{21,3},{17,11},{11,14},{3,15},{-4,8},{-1,1}};
    static const int drivers[] = {MT_UNIT_GRUNT, MT_UNIT_SNIPER, MT_UNIT_PSYCHO, MT_UNIT_LASER};
    static const int ranges[] = {128, 152, 128, 144}, damages[] = {5, 10, 2, 15};
    RtsGameModel *model = load();
    if (!model) return fail("load for APC bullet salvo");
    zmap_t *map = level.native_data;
    for (int y = 38; y <= 43; ++y) for (int x = 38; x <= 49; ++x)
        map->ground_attr[y * Z_MAP_STRIDE + x] = 0;
    Z_RebuildCliffs(&level);
    fixed2_t at = fixed2_cell_center((ivec2_t){40, 40});
    mobj_t *apc = blast_unit(MT_UNIT_APC, 0, at, 500);
    mobj_t *robot = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 112, 0), 500);
    mobj_t *blocker = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 80, 0), 500);
    if (!apc || !robot || !blocker) return fail("spawn APC bullet salvo fixture");
    apc->z.body_tracked = true; apc->z.body_accuracy = 127;
    apc->z.crew_count = 3;
    ivec2_t origin = Z_AITopLeft(apc);
    for (int d = 0; d < 4; ++d) for (int facing = 0; facing < 8; ++facing) {
        apc->z.driver = drivers[d] - MT_UNITS + 1;
        apc->core.angle = direction_to_angle(facing, 8, ANG90, true);
        if (!Z_Attack(apc, &apc->info->attack, robot)) return fail("launch APC salvo");
        ivec2_t start = ivec2_add(origin, port[facing]);
        if (drivers[d] == MT_UNIT_LASER) start = ivec2_sub(start, (ivec2_t){7,7});
        for (int i = 0; i < 3; ++i) {
            mobj_t *bullet = nth(MT_BULLET, 0, i);
            if (!bullet || !ivec2_equal(bullet->z.water.start, start) || bullet->z.water.speed != 15 ||
                bullet->z.water.range != ranges[d] || bullet->missile.damage != damages[d])
                return fail("APC salvo uses driver stats and native port");
        }
        if (nth(MT_BULLET, 0, 3) || robot->hp != 500) return fail("APC launches three separate bullets");
        for (int i = 0; i < 3; ++i) P_RemoveMobj(nth(MT_BULLET, 0, 0));
    }
    disarm_except(0, apc);
    apc->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    apc->core.angle = direction_to_angle(2, 8, ANG90, true);
    apc->attack.target = robot;
    P_UpdateSight();
    if (!P_Attack(apc) || robot->hp != 500 || !nth(MT_BULLET, 0, 2)) return fail("APC attack launches salvo");
    apc->traits &= ~MF_ATTACK;
    if (!tick(model, 1) || !nth(MT_BULLET, 0, 2) || blocker->hp != 500) return fail("APC salvo remains in flight");
    uint32_t apc_id = apc->id, robot_id = robot->id, blocker_id = blocker->id, hash = G_Consistency();
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t bytes = 0;
    if (!G_SaveGameBlob(&save, &bytes, "APC salvo", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, bytes, &app, rts_game_model_ai(model), &hud)) return fail("save APC salvo");
    free(save);
    apc = P_MobjById(apc_id); robot = P_MobjById(robot_id); blocker = P_MobjById(blocker_id);
    if (!apc || !robot || !blocker || !nth(MT_BULLET, 0, 2) || G_Consistency() != hash)
        return fail("restore APC salvo");
    apc->z.driver = MT_UNIT_SNIPER - MT_UNITS + 1;
    for (int n = 0; n < 8 && nth(MT_BULLET, 0, 0); ++n)
        if (!tick(model, 1)) return fail("tick APC salvo");
    if (nth(MT_BULLET, 0, 0) || blocker->hp != 485 || robot->hp != 500)
        return fail("APC bullets retain launch damage and hit intervening unit");
    P_RemoveMobj(robot); P_RemoveMobj(blocker);
    mobj_t *jeep = blast_unit(MT_UNIT_JEEP, 1, pixel_at(at, 80, 0), 500);
    if (!jeep) return fail("spawn APC driver-hit fixture");
    jeep->traits &= ~(MF_ATTACK | MF_MOBILE);
    bool mixed = false, neutral = false;
    for (int n = 0; n < 128 && !mixed; ++n) {
        jeep->hp = 500; jeep->z.crew_hurt = 0;
        jeep->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
        jeep->z.crew_count = 1;
        jeep->z.crew[0] = (zcrew_t){.hp = 200, .type = MT_UNIT_GRUNT - MT_UNITS};
        if (!Z_Attack(apc, &apc->info->attack, jeep)) return fail("launch APC driver-hit salvo");
        for (int t = 0; t < 8 && nth(MT_BULLET, 0, 0); ++t)
            if (!tick(model, 1)) return fail("tick APC driver-hit salvo");
        mixed = jeep->z.crew_hurt && jeep->hp < 500;
    }
    if (!mixed) return fail("independent APC bullets hit driver and armored body");
    for (int n = 0; n < 128 && !neutral; ++n) {
        jeep->hp = 500; jeep->z.crew_hurt = 0;
        jeep->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
        jeep->z.crew_count = 1;
        jeep->z.crew[0] = (zcrew_t){.hp = 1, .type = MT_UNIT_GRUNT - MT_UNITS};
        if (!Z_Attack(apc, &apc->info->attack, jeep)) return fail("launch fatal APC salvo");
        for (int t = 0; t < 24 && nth(MT_BULLET, 0, 0) && !neutral; ++t) {
            A_ZBullet(nth(MT_BULLET, 0, 0));
            neutral = jeep->team == Z_NEUTRAL;
        }
        if (neutral) {
            mobj_t *remaining = nth(MT_BULLET, 0, 0);
            if (!remaining) return fail("fatal hit leaves another bullet in flight");
            uint32_t remaining_id = remaining->id;
            int hp = jeep->hp;
            ivec2_t jeep_origin = Z_AITopLeft(jeep);
            ivec2_t start = ivec2_add(jeep_origin, (ivec2_t){-30, 8});
            fixed2_t control_pos = fixed2_scale(fixed2_from_cell(ivec2_add(jeep_origin, (ivec2_t){8, 8})), FIXED_ONE / Z_TILE);
            mobj_t *control = blast_unit(MT_UNIT_GRUNT, 1, control_pos, 500);
            if (!control) return fail("spawn target behind neutralized Jeep");
            remaining->core.position = fixed3_from_fixed2(fixed2_scale(fixed2_from_cell(start), FIXED_ONE / Z_TILE), 0);
            remaining->z.water.start = start;
            remaining->z.water.direction = (ivec2_t){1, 0};
            remaining->z.water.vertical = 0;
            remaining->z.water.major = remaining->z.water.remaining = 90;
            remaining->z.water.minor = 0;
            remaining->z.water.error = 45;
            for (int t = 0; t < 8 && P_MobjById(remaining_id); ++t)
                A_ZBullet(P_MobjById(remaining_id));
            if (P_MobjById(remaining_id) || jeep->hp != hp || control->hp >= 500)
                return fail("remaining bullet crosses neutralized Jeep and hits control target");
        }
    }
    if (!neutral) return fail("APC bullet neutralizes Jeep after fatal driver hit");
    rts_game_model_destroy(model);
    return 0;
}

static int test_apc_pyro_phase(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for APC Pyro phase");
    fixed2_t at = fixed2_cell_center((ivec2_t){5, 5});
    mobj_t *apc = blast_unit(MT_UNIT_APC, 0, at, 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 16, 0), 5000);
    if (!apc || !target) return fail("spawn APC Pyro fixture");
    apc->z.driver = MT_UNIT_PYRO - MT_UNITS + 1;
    apc->z.crew_count = 3;
    uint32_t apc_id = apc->id, target_id = target->id;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    for (int i = 1; i <= 16; ++i) {
        int hp = target->hp;
        if (!Z_Attack(apc, &apc->info->attack, target) ||
            target->hp != hp - ((i <= 3 || i == 16) ? 60 : 0))
            return fail("APC Pyro fires only during four phases of sixteen");
        for (int j = 0; j < 3; ++j)
            if (apc->z.crew[j].flame_phase != (i & 15)) return fail("each Pyro phase advances");
        if (i == 4 && !G_SaveGameBlob(&save, &size, "APC flame phase", &app, rts_game_model_ai(model), &hud))
            return fail("save APC flame phase");
    }
    if (!G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("restore APC flame phase");
    free(save);
    apc = P_MobjById(apc_id); target = P_MobjById(target_id);
    if (!apc || !target || apc->z.crew[0].flame_phase != 4) return fail("restore crew flame phase");
    uint32_t hash = G_Consistency();
    apc->z.crew[1].flame_phase = 2;
    if (G_Consistency() == hash) return fail("crew phase contributes to consistency");
    apc->z.crew[2].flame_phase = 15;
    int hp = target->hp;
    if (!Z_Attack(apc, &apc->info->attack, target) || target->hp != hp - 40 ||
        apc->z.crew[0].flame_phase != 5 || apc->z.crew[1].flame_phase != 3 ||
        apc->z.crew[2].flame_phase != 0) return fail("independent crew phases survive restoration");
    apc->z.crew_count = 0;
    apc->z.flame_phase = 3;
    hash = G_Consistency();
    if (!Z_Attack(apc, &apc->info->attack, target) || target->hp != hp - 40 ||
        apc->z.flame_phase != 4 || G_Consistency() == hash) return fail("implicit driver phase advances and is hashed");
    rts_game_model_destroy(model);
    return 0;
}

static int test_apc_shell_salvo(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for APC shell salvo");
    fixed2_t at = fixed2_cell_center((ivec2_t){5, 5});
    mobj_t *apc = blast_unit(MT_UNIT_APC, 0, at, 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 32, 0), 500);
    if (!apc || !target) return fail("spawn APC shell salvo fixture");
    apc->z.driver = MT_UNIT_TOUGH - MT_UNITS + 1;
    apc->z.crew_count = 3;
    uint32_t apc_id = apc->id, target_id = target->id;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!G_SaveGameBlob(&save, &size, "APC before salvo", &app, rts_game_model_ai(model), &hud))
        return fail("save before APC salvo");
    if (!Z_Attack(apc, &apc->info->attack, target)) return fail("fire APC shell salvo");
    ivec2_t anchor = Z_BlastAnchor(target);
    fixed2_t aims[3];
    bool varied = false;
    for (int i = 0; i < 3; ++i) {
        mobj_t *shell = nth(MT_SHELL, 0, i);
        if (!shell || shell->missile.damage != 40) return fail("each APC crew shell has Tough damage");
        aims[i] = shell->movement.goal;
        ivec2_t point = fixed2_cell(fixed2_scale(aims[i], FIXED_FROM_INT(Z_TILE)));
        ivec2_t delta = ivec2_sub(point, anchor);
        if (delta.x < -8 || delta.x > 7 || delta.y < -8 || delta.y > 7)
            return fail("APC scatter stays within native pixel bounds");
        if (i && !fixed2_equal(aims[0], aims[i])) varied = true;
    }
    if (!varied) return fail("APC shells use independent scatter");
    if (nth(MT_SHELL, 0, 3)) return fail("three crew produce three shells");
    uint32_t hash = G_Consistency();
    if (!G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud))
        return fail("restore before APC salvo");
    free(save); save = NULL; size = 0;
    apc = P_MobjById(apc_id); target = P_MobjById(target_id);
    if (!apc || !target || !Z_Attack(apc, &apc->info->attack, target) || G_Consistency() != hash)
        return fail("APC scatter is deterministic after restoring the same state");
    for (int i = 0; i < 3; ++i)
        if (!nth(MT_SHELL, 0, i) || !fixed2_equal(nth(MT_SHELL, 0, i)->movement.goal, aims[i]))
            return fail("APC scatter reproduces each shell aim");
    if (!G_SaveGameBlob(&save, &size, "APC salvo", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud))
        return fail("save APC shells in flight");
    free(save);
    target = P_MobjById(target_id);
    if (!target) return fail("restore APC shell target");
    for (int i = 0; i < 3; ++i) {
        mobj_t *shell = nth(MT_SHELL, 0, i);
        if (!shell || shell->missile.damage != 40) return fail("restore independent APC shells");
        A_ZBlastHit(shell);
    }
    if (target->hp != 380) return fail("each APC shell applies its own blast");
    rts_game_model_destroy(model);
    model = load();
    if (!model) return fail("load for full APC salvo");
    apc = blast_unit(MT_UNIT_APC, 0, at, 500);
    target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 32, 0), 500);
    if (!apc || !target) return fail("spawn full APC salvo fixture");
    apc->z.driver = MT_UNIT_TOUGH - MT_UNITS + 1;
    apc->z.crew_count = Z_CREW_MAX;
    if (!Z_Attack(apc, &apc->info->attack, target) || !nth(MT_SHELL, 0, Z_CREW_MAX - 1) ||
        nth(MT_SHELL, 0, Z_CREW_MAX)) return fail("targeted APC salvo includes all 21 crew");
    rts_game_model_destroy(model);
    return 0;
}

static int test_shell_blast(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for shell blast");
    fixed2_t at = fixed2_cell_center((ivec2_t){5, 5});
    mobj_t *gun = blast_unit(MT_UNIT_LIGHT_TANK, 0, pixel_at(at, -80, 0), 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, at, 500);
    mobj_t *left = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, -18, -14), 500);
    mobj_t *right = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 26, 18), 500);
    mobj_t *outx = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 27, 0), 500);
    mobj_t *outy = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 0, -15), 500);
    mobj_t *ally = blast_unit(MT_UNIT_GRUNT, 0, at, 500);
    mobj_t *killed = blast_unit(MT_UNIT_GRUNT, 1, at, 1);
    mobj_t *after = blast_unit(MT_UNIT_GRUNT, 1, at, 500);
    mobj_t *vehicle = blast_unit(MT_UNIT_MED_TANK, 1, at, 500);
    if (!gun || !target || !left || !right || !outx || !outy || !ally || !killed || !after || !vehicle)
        return fail("spawn shell targets");
    mobj_t *victims[] = {target, left, right, outx, outy, ally, killed, after, vehicle};
    for (size_t i = 0; i < sizeof(victims) / sizeof(*victims); ++i)
        victims[i]->traits &= ~(MF_ATTACK | MF_MOBILE);
    gun->traits &= ~MF_ATTACK;
    isize2_t robot_size;
    if (!Z_SpriteSize(level.native_data, 66, &robot_size)) return fail("robot spawn sprite size");
    ivec2_t pixel = fixed2_cell(fixed2_scale(at, FIXED_FROM_INT(Z_TILE)));
    ivec2_t expected_robot = ivec2_add(pixel, (ivec2_t){8 - robot_size.w / 2, 8 - robot_size.h / 2});
    ivec2_t expected_vehicle = ivec2_add(pixel, (ivec2_t){8, 8});
    ivec2_t robot_anchor = Z_BlastAnchor(target), vehicle_anchor = Z_BlastAnchor(vehicle);
    if (robot_anchor.x != expected_robot.x || robot_anchor.y != expected_robot.y ||
        vehicle_anchor.x != expected_vehicle.x || vehicle_anchor.y != expected_vehicle.y)
        return fail("robot and composed vehicle use their native selection anchors");
    ivec2_t shift = ivec2_sub(expected_vehicle, expected_robot);
    vehicle->core.position = fixed3_from_fixed2(pixel_at(at, 26 - shift.x, 18 - shift.y), 0);
    const weapondef_t *weapon = &gun->info->attack;
    if (!Z_Attack(gun, weapon, target)) return fail("shell uses area attack");
    mobj_t *shell = nth(MT_SHELL, 0, 0);
    if (!shell || target->hp != 500) return fail("shell remains in flight before impact");
    shell->movement.goal = fixed2_scale(fixed2_from_cell(Z_BlastAnchor(target)), FIXED_ONE / Z_TILE);
    P_ExplodeMissile(shell);
    if (target->hp != 500 || !tick(model, 2) || target->hp != 500 || !tick(model, 1))
        return fail("shell delays unit blast until explosion frame three");
    if (target->hp != 450 || left->hp != 450 || right->hp != 450 || after->hp != 450 ||
        vehicle->hp != 450 || killed->hp > 0)
        return fail("shell hits primary and boundary units once, including after a kill");
    if (outx->hp != 500 || outy->hp != 500 || ally->hp != 500 || gun->hp != 500)
        return fail("shell excludes outside and friendly units");
    if (!Z_Attack(ally, &ally->info->attack, target) || !nth(MT_BULLET, 0, 0) || left->hp != 450)
        return fail("bullet launch does not use shell area damage");
    puts("PASS: z shell blast hits enemies inside native bounds once");
    rts_game_model_destroy(model);
    return 0;
}

static int test_shell_building_contact(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for shell building contact");
    fixed2_t at = fixed2_cell_center((ivec2_t){25, 25});
    mobj_t *gun = blast_unit(MT_UNIT_LIGHT_TANK, 0, pixel_at(at, -80, 0), 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, -32, 0), 500);
    mobj_t *building = blast_unit(MT_ROBOT_FACTORY, 0, at, 500);
    if (!gun || !target || !building) return fail("spawn shell building contact fixture");
    building->z.building = 1;
    if (!Z_Attack(gun, &gun->info->attack, target)) return fail("launch building contact shell");
    mobj_t *shell = nth(MT_SHELL, 0, 0);
    if (!shell) return fail("find building contact shell");
    ivec2_t origin = Z_BuildingOrigin(building);
    ivec2_t point = ivec2_add(origin, (ivec2_t){-4, 8});
    shell->movement.goal = fixed2_scale(fixed2_from_cell(point), FIXED_ONE / Z_TILE);
    A_ZBlastImpact(shell);
    if (building->hp != 450) return fail("shell hits friendly non-target building at inclusive origin");
    building->hp = 500;
    point.x--;
    shell->movement.goal = fixed2_scale(fixed2_from_cell(point), FIXED_ONE / Z_TILE);
    A_ZBlastImpact(shell);
    if (building->hp != 500) return fail("shell misses just outside building footprint");
    point = ivec2_add(origin, (ivec2_t){building->info->footprint.w * Z_TILE - 4,
                                         building->info->footprint.h * Z_TILE + 8});
    shell->movement.goal = fixed2_scale(fixed2_from_cell(point), FIXED_ONE / Z_TILE);
    A_ZBlastImpact(shell);
    if (building->hp != 450) return fail("shell hits inclusive far building edge");
    rts_game_model_destroy(model);
    return 0;
}

static int test_shell_grenade_box(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for shell grenade box");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    disarm_except(0, NULL);
    fixed2_t at = fixed2_cell_center((ivec2_t){25, 25});
    mobj_t *gun = blast_unit(MT_UNIT_LIGHT_TANK, 0, pixel_at(at, -80, 0), 500);
    mobj_t *box = blast_unit(MT_GRENADES, Z_NEUTRAL, at, 100);
    mobj_t *second = blast_unit(MT_GRENADES_2, Z_NEUTRAL, pixel_at(at, 16, 0), 100);
    if (!gun || !box || !second) return fail("spawn grenade box fixture");
    isize2_t size;
    if (!Z_SpriteSize(level.native_data, states[mobjinfo[MT_GRENADES].spawnstate].frame, &size)) return fail("box sprite size");
    ivec2_t centre = fixed2_cell(fixed2_scale(at, FIXED_FROM_INT(Z_TILE)));
    ivec2_t origin = ivec2_sub(centre, (ivec2_t){size.w / 2, size.h / 2});
    fixed2_t blast_origin = fixed2_scale(fixed2_from_cell(origin), FIXED_ONE / Z_TILE);
    mobj_t *ally = blast_unit(MT_UNIT_GRUNT, 0, blast_origin, 500);
    mobj_t *enemy = blast_unit(MT_UNIT_GRUNT, 1, blast_origin, 500);
    mobj_t *outside = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(blast_origin, 80, 0), 500);
    if (!ally || !enemy || !outside || !Z_Attack(gun, &gun->info->attack, enemy)) return fail("launch box test shell");
    ally->traits &= ~MF_ATTACK; enemy->traits &= ~MF_ATTACK; outside->traits &= ~MF_ATTACK;
    mobj_t *shell = nth(MT_SHELL, 0, 0);
    if (!shell) return fail("find box shell");
    shell->movement.goal = fixed2_scale(fixed2_from_cell(ivec2_add(origin, (ivec2_t){8,8})), FIXED_ONE / Z_TILE);
    shell->missile.damage = 1;
    box->z.object_hp = 2; box->z.object_slot = 1; second->z.object_slot = 2;
    A_ZBlastImpact(shell);
    if (box->remove || box->z.object_hp != 1) return fail("box survives first damage point");
    P_ExplodeMissile(shell);
    if (!box->remove || second->remove || !nth(MT_BOX_BLAST, -1, 0)) return fail("box schedules one non-chaining blast");
    P_RemoveMobj(shell);
    uint32_t ally_id = ally->id, enemy_id = enemy->id, outside_id = outside->id, second_id = second->id;
    ivec2_t cell = {origin.x / Z_TILE, origin.y / Z_TILE};
    int terrain_index = cell.y * Z_MAP_STRIDE + cell.x;
    if ((((zmap_t *)level.native_data)->ground_attr[terrain_index] & 0x7f) != 0x16)
        return fail("box blast changes saved ground attribute");
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t bytes = 0;
    bool saved = G_SaveGameBlob(&save, &bytes, "Box explosion", &app, rts_game_model_ai(model), &hud);
    bool loaded = saved && G_LoadGameBlob(save, bytes, &app, rts_game_model_ai(model), &hud);
    if (!loaded) return fail("save delayed box blast");
    free(save);
    ally = P_MobjById(ally_id); enemy = P_MobjById(enemy_id); outside = P_MobjById(outside_id);
    second = P_MobjById(second_id);
    if (!ally || !enemy || !outside || !second ||
        (((zmap_t *)level.native_data)->terrain[terrain_index] & 0x7f) != 0x16)
        return fail("restore delayed box targets and terrain");
    int ally_hp = ally->hp, enemy_hp = enemy->hp, outside_hp = outside->hp;
    bool advanced = tick(model, 4);
    if (!advanced || ally->hp != ally_hp - 127 || enemy->hp != enemy_hp - 127 || outside->hp != outside_hp || second->remove)
        return fail("delayed box blast hits both teams and excludes outside units");
    rts_game_model_destroy(model);
    return 0;
}

static int test_shell_empty_object(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for empty object impact");
    fixed2_t at = fixed2_cell_center((ivec2_t){25, 25});
    mobj_t *gun = blast_unit(MT_UNIT_LIGHT_TANK, 0, pixel_at(at, -80, 0), 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, -32, 0), 500);
    mobj_t *box = blast_unit(MT_GRENADES, Z_NEUTRAL, at, 100);
    mobj_t *jeep = blast_unit(MT_UNIT_JEEP, Z_NEUTRAL, at, 50);
    if (!gun || !target || !box || !jeep) return fail("spawn empty-object impact fixture");
    isize2_t box_size, jeep_size;
    if (!Z_SpriteSize(level.native_data, states[S_GRENADES].frame, &box_size) ||
        !Z_SpriteSize(level.native_data, states[S_JEEP].frame, &jeep_size)) return fail("empty-object sprite dimensions");
    ivec2_t centre = fixed2_cell(fixed2_scale(at, FIXED_FROM_INT(Z_TILE)));
    ivec2_t origin = ivec2_sub(centre, (ivec2_t){box_size.w / 2, box_size.h / 2});
    ivec2_t jeep_centre = ivec2_add(origin, (ivec2_t){jeep_size.w / 2, jeep_size.h / 2});
    jeep->core.position = fixed3_from_fixed2(fixed2_scale(fixed2_from_cell(jeep_centre), FIXED_ONE / Z_TILE), 0);
    jeep->traits &= ~(MF_MOBILE | MF_ATTACK);
    P_SetMobjState(jeep, S_JEEP);
    jeep->z.object = MT_JEEP; jeep->z.object_slot = 1; jeep->z.object_hp = 2;
    box->z.object_slot = 2;
    if (!Z_Attack(gun, &gun->info->attack, target)) return fail("launch empty-object shell");
    mobj_t *shell = nth(MT_SHELL, 0, 0);
    if (!shell) return fail("find empty-object shell");
    shell->movement.goal = fixed2_scale(fixed2_from_cell(ivec2_add(origin, (ivec2_t){8,8})), FIXED_ONE / Z_TILE);
    shell->missile.damage = 1;
    A_ZBlastImpact(shell);
    if (jeep->z.object_hp != 1 || jeep->hp != 25 || box->remove)
        return fail("earlier empty Jeep takes damage before overlapping box");
    uint32_t jeep_id = jeep->id, box_id = box->id, shell_id = shell->id;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t bytes = 0;
    if (!G_SaveGameBlob(&save, &bytes, "Empty object", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, bytes, &app, rts_game_model_ai(model), &hud)) return fail("save damaged empty object");
    jeep = P_MobjById(jeep_id); box = P_MobjById(box_id); shell = P_MobjById(shell_id);
    if (!jeep || !box || !shell || jeep->z.object_hp != 1 || jeep->hp != 25)
        return fail("restore damaged empty object health");
    mobj_t *robot = blast_unit(MT_UNIT_GRUNT, 0, fixed3_xy(jeep->core.position), 30);
    if (!robot || !Z_DetourBoard(robot, jeep) || jeep->hp != 25 || jeep->z.object_slot)
        return fail("boarding retains damage and releases the empty-object slot");
    if (!G_LoadGameBlob(save, bytes, &app, rts_game_model_ai(model), &hud)) return fail("restore empty-object collision");
    free(save);
    jeep = P_MobjById(jeep_id); box = P_MobjById(box_id); shell = P_MobjById(shell_id);
    if (!jeep || !box || !shell) return fail("restore empty-object collision fixture");
    A_ZBlastImpact(shell);
    if (!jeep->remove || box->remove) return fail("fatal empty-object hit does not reach box");
    mobj_t *truck = blast_unit(MT_UNIT_TRUCK, Z_NEUTRAL, at, 75);
    isize2_t truck_size;
    if (!truck || !Z_SpriteSize(level.native_data, states[S_TRUCK].frame, &truck_size)) return fail("spawn empty truck");
    ivec2_t truck_centre = ivec2_add(origin, (ivec2_t){truck_size.w / 2, truck_size.h / 2});
    truck->core.position = fixed3_from_fixed2(fixed2_scale(fixed2_from_cell(truck_centre), FIXED_ONE / Z_TILE), 0);
    truck->traits &= ~(MF_MOBILE | MF_ATTACK);
    P_SetMobjState(truck, S_TRUCK);
    truck->z.object = MT_TRUCK; truck->z.object_slot = 1; truck->z.object_hp = 3;
    if (Z_Enterable(truck)) return fail("empty truck is not boardable");
    A_ZBlastImpact(shell);
    if (truck->z.object_hp != 2 || truck->hp != 50 || box->remove)
        return fail("unboardable empty truck preempts the box");
    truck->z.object_hp = 1;
    A_ZBlastImpact(shell);
    if (!truck->remove || box->remove) return fail("empty truck destruction stops the same hit");
    A_ZBlastImpact(shell);
    if (!box->remove || !nth(MT_BOX_BLAST, -1, 0)) return fail("subsequent shot reaches the box");
    rts_game_model_destroy(model);
    return 0;
}

static int test_grenade_impact(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load grenade impact fixture");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    fixed2_t at = fixed2_cell_center((ivec2_t){25, 25});
    mobj_t *thrower = blast_unit(MT_UNIT_GRUNT, 0, pixel_at(at, -48, 0), 100);
    mobj_t *target = blast_unit(MT_UNIT_MED_TANK, 1, at, 500);
    if (!thrower || !target) return fail("spawn grenade impact fixture");
    thrower->z.grenades = 1;
    thrower->traits &= ~MF_ATTACK; target->traits &= ~(MF_ATTACK | MF_MOBILE);
    if (!Z_Attack(thrower, &thrower->info->attack, target) || thrower->z.grenades)
        return fail("launch grenade");
    mobj_t *grenade = nth(MT_GRENADE, 0, 0);
    if (!grenade) return fail("find flying grenade");
    if (grenade->z.grenade.speed != 3) return fail("Grunt grenade uses native strength-derived speed");
    for (int i = 0; i < 80 && grenade->core.state_id == S_GRENADE; ++i)
        if (!tick(model, 1)) return fail("tick grenade flight");
    if (grenade->core.state_id != S_GRENADE_BLAST || target->hp != 500)
        return fail("grenade arrival defers unit damage");
    fixed2_t impact = fixed3_xy(grenade->core.position);
    mobj_t *inside = blast_unit(MT_UNIT_GRUNT, 1, impact, 100);
    mobj_t *outside = blast_unit(MT_UNIT_GRUNT, 1, impact, 100);
    mobj_t *ally = blast_unit(MT_UNIT_GRUNT, 0, impact, 100);
    if (!inside || !outside || !ally) return fail("spawn grenade blast units");
    inside->traits &= ~(MF_ATTACK | MF_MOBILE); outside->traits &= ~(MF_ATTACK | MF_MOBILE);
    ally->traits &= ~(MF_ATTACK | MF_MOBILE);
    ivec2_t pixel = fixed2_cell(fixed2_scale(impact, FIXED_FROM_INT(Z_TILE)));
    ivec2_t adjustment = ivec2_sub(pixel, Z_BlastAnchor(inside));
    inside->core.position = fixed3_from_fixed2(pixel_at(impact, adjustment.x - 18, adjustment.y - 14), 0);
    outside->core.position = fixed3_from_fixed2(pixel_at(impact, adjustment.x - 19, adjustment.y - 14), 0);
    ally->core.position = fixed3_from_fixed2(pixel_at(impact, adjustment.x, adjustment.y), 0);
    uint32_t inside_id = inside->id, outside_id = outside->id, ally_id = ally->id;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!G_SaveGameBlob(&save, &size, "Grenade impact", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save grenade impact");
    free(save);
    inside = P_MobjById(inside_id); outside = P_MobjById(outside_id); ally = P_MobjById(ally_id);
    if (!inside || !outside || !ally || !tick(model, 2) || inside->hp != 100 || !tick(model, 1) ||
        inside->hp != 50 || outside->hp != 100 || ally->hp != 100 || !tick(model, 1) || inside->hp != 50)
        return fail("saved grenade uses delayed native bounds exactly once");
    rts_game_model_destroy(model);
    return 0;
}

static int test_grenade_speed(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load grenade speed fixture");
    fixed2_t at = fixed2_cell_center((ivec2_t){25, 25});
    mobj_t *target = blast_unit(MT_BRIDGE, 1, pixel_at(at, 96, 96), 500);
    const int types[] = {MT_UNIT_GRUNT, MT_UNIT_SNIPER, MT_UNIT_PSYCHO, MT_UNIT_PYRO, MT_UNIT_LASER};
    const int speeds[] = {3, 5, 3, 6, 6};
    if (!target) return fail("spawn grenade speed fixture");
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); ++i) {
        mobj_t *robot = blast_unit(types[i], 0, at, 100);
        if (!robot) return fail("spawn grenade speed robot");
        robot->z.grenades = 1;
        if (!Z_Attack(robot, &robot->info->attack, target)) return fail("launch grenade speed fixture");
        mobj_t *grenade = nth(MT_GRENADE, 0, (int)i);
        if (!grenade) return fail("find grenade speed projectile");
        if (grenade->z.grenade.speed != speeds[i] || grenade->z.grenade.remaining <= 0)
            return fail("robot grenades use native strength-derived major-axis speed");
    }
    rts_game_model_destroy(model);
    return 0;
}

static int test_grenade_terrain(void) {
    for (int raised = 0; raised < 2; ++raised) {
        RtsGameModel *model = load();
        if (!model) return fail("load grenade terrain fixture");
        P_AiSetFeatures(rts_game_model_ai(model), 0);
        zmap_t *map = level.native_data;
        fixed2_t at = fixed2_cell_center((ivec2_t){25,25});
        mobj_t *robot = blast_unit(MT_UNIT_GRUNT, 0, at, 100);
        mobj_t *target = blast_unit(MT_UNIT_MED_TANK, 1, pixel_at(at, 80, 0), 500);
        if (!map || !robot || !target) return fail("spawn grenade terrain fixture");
        for (int y = 23; y <= 27; ++y) for (int x = 23; x <= 34; ++x)
            map->ground_attr[y * Z_MAP_STRIDE + x] = 0;
        Z_RebuildCliffs(&level);
        robot->traits &= ~MF_ATTACK; target->traits &= ~(MF_ATTACK | MF_MOBILE);
        robot->z.grenades = 1;
        if (!Z_Attack(robot, &robot->info->attack, target)) return fail("launch grenade over terrain");
        mobj_t *grenade = nth(MT_GRENADE, 0, 0);
        if (!grenade) return fail("find terrain grenade");
        ivec2_t start = grenade->z.grenade.start;
        int barrier = start.x / Z_TILE + 2;
        for (int y = 23; y <= 27; ++y)
            map->ground_attr[y * Z_MAP_STRIDE + barrier] = raised ? 1 : 11; /* Heights 3 and 1. */
        Z_RebuildCliffs(&level);
        uint32_t id = grenade->id;
        if (!tick(model, 1) || grenade->core.state_id != S_GRENADE) return fail("grenade begins flight");
        int remaining = grenade->z.grenade.remaining, error = grenade->z.grenade.error;
        uint32_t hash = G_Consistency();
        app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
        if (!G_SaveGameBlob(&save, &size, "Grenade flight", &app, rts_game_model_ai(model), &hud) ||
            !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save grenade flight");
        free(save);
        grenade = P_MobjById(id);
        if (!grenade || grenade->z.grenade.remaining != remaining || grenade->z.grenade.error != error || G_Consistency() != hash)
            return fail("restore grenade flight state");
        for (int i = 0; i < 80 && grenade->core.state_id == S_GRENADE; ++i)
            if (!tick(model, 1)) return fail("tick grenade to terrain or arrival");
        ivec2_t hit = fixed2_cell(fixed2_scale(fixed3_xy(grenade->core.position), FIXED_FROM_INT(Z_TILE)));
        if (grenade->core.state_id != S_GRENADE_BLAST || grenade->core.position.z != 0 ||
            (raised ? hit.x / Z_TILE != barrier : hit.x / Z_TILE <= barrier))
            return fail("raised terrain intercepts and equal height passes");
        rts_game_model_destroy(model);
    }
    RtsGameModel *model = load();
    if (!model) return fail("load grenade arc fixture");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    zmap_t *map = level.native_data;
    fixed2_t at = fixed2_cell_center((ivec2_t){25,25});
    mobj_t *robot = blast_unit(MT_UNIT_GRUNT, 0, at, 100);
    mobj_t *target = blast_unit(MT_UNIT_MED_TANK, 1, pixel_at(at, 200, 0), 500);
    if (!map || !robot || !target) return fail("spawn grenade arc fixture");
    for (int y = 23; y <= 27; ++y) for (int x = 23; x <= 40; ++x)
        map->ground_attr[y * Z_MAP_STRIDE + x] = 0;
    Z_RebuildCliffs(&level);
    robot->traits &= ~MF_ATTACK; target->traits &= ~(MF_ATTACK | MF_MOBILE); robot->z.grenades = 1;
    if (!Z_Attack(robot, &robot->info->attack, target)) return fail("launch long grenade");
    mobj_t *grenade = nth(MT_GRENADE, 0, 0);
    if (!grenade || grenade->z.grenade.arc < 1) return fail("long grenade selects raised arc");
    ivec2_t start = grenade->z.grenade.start;
    bool rose = false;
    for (int i = 0; i < 80 && grenade->core.state_id == S_GRENADE; ++i) {
        if (!tick(model, 1)) return fail("tick long grenade");
        rose |= grenade->core.position.z > 0;
    }
    ivec2_t hit = fixed2_cell(fixed2_scale(fixed3_xy(grenade->core.position), FIXED_FROM_INT(Z_TILE)));
    if (!rose || grenade->core.state_id != S_GRENADE_BLAST ||
        ivec2_distance_squared64(start, hit) < 120 * 120 || hit.x >= start.x + 200)
        return fail("grenade follows raised arc and stops at weapon range");
    mobj_t *bad = P_SpawnMobj(fixed3_from_fixed2(at, 0), MT_GRENADE);
    if (!bad) return fail("spawn invalid grenade fixture");
    bad->z.grenade.arc = 5;
    A_ZGrenade(bad);
    if (!bad->remove) return fail("invalid saved grenade arc is rejected");
    bad = P_SpawnMobj(fixed3_from_fixed2(at, 0), MT_GRENADE);
    if (!bad) return fail("spawn invalid grenade speed fixture");
    bad->z.grenade = (zgrenade_t){.start = start, .direction = {1,0}, .remaining = 20,
        .major = 20, .minor = 0, .error = 10, .range = 120, .speed = 1000000000};
    A_ZGrenade(bad);
    if (!bad->remove) return fail("invalid saved grenade speed is rejected");
    rts_game_model_destroy(model);
    return 0;
}

static int test_grenade_launch(void) {
    static const ivec2_t muzzle[] = {{5,-4},{7,-3},{12,3},{6,5},{2,6},{-2,6},{-6,2},{0,-2}};
    const int types[] = {MT_UNIT_MED_TANK, MT_UNIT_LIGHT_GUN};
    RtsGameModel *model = load();
    if (!model) return fail("load grenade launch fixture");
    fixed2_t at = fixed2_cell_center((ivec2_t){25,25});
    mobj_t *robot = blast_unit(MT_UNIT_GRUNT, 0, at, 100);
    if (!robot) return fail("spawn grenade thrower");
    for (size_t t = 0; t < sizeof(types) / sizeof(*types); ++t) {
        mobj_t *target = blast_unit(types[t], 1, pixel_at(at, 24, 8), 500);
        if (!target) return fail("spawn grenade launch target");
        for (int d = 0; d < 8; ++d) {
            robot->core.angle = direction_to_angle(d, 8, ANG90, true);
            robot->z.grenades = 1;
            if (!Z_Attack(robot, &robot->info->attack, target)) return fail("launch grenade by facing");
            mobj_t *grenade = NULL;
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
                mobj_t *mo = (mobj_t *)th;
                if (th->function == P_MobjThinker && mo->type_id == MT_GRENADE && !mo->remove) grenade = mo;
            }
            ivec2_t start = ivec2_add(Z_AITopLeft(robot), muzzle[d]);
            ivec2_t aim = ivec2_add(Z_AITopLeft(target), (ivec2_t){t ? 16 : 8, 8});
            if (!grenade || !ivec2_equal(start, fixed2_cell(fixed2_scale(fixed3_xy(grenade->core.position), FIXED_FROM_INT(Z_TILE)))))
                return fail("grenade uses native facing-dependent launch point");
            zgrenade_t *flight = &grenade->z.grenade;
            ivec2_t offset = flight->vertical ? (ivec2_t){flight->minor, flight->major} :
                                                (ivec2_t){flight->major, flight->minor};
            ivec2_t end = {start.x + flight->direction.x * offset.x, start.y + flight->direction.y * offset.y};
            if (!ivec2_equal(end, aim))
                return fail("grenade aims at native tank or gun anchor");
            P_RemoveMobj(grenade);
        }
        P_RemoveMobj(target);
    }
    robot->core.angle = direction_to_angle(0, 8, ANG90, true);
    ivec2_t start = ivec2_add(Z_AITopLeft(robot), muzzle[0]);
    fixed2_t centre;
    if (!Z_UnitCentre(level.native_data, MT_UNIT_LIGHT_GUN - MT_UNITS,
                      ivec2_sub(start, (ivec2_t){16,8}), &centre))
        return fail("place zero-length grenade target");
    mobj_t *target = blast_unit(MT_UNIT_LIGHT_GUN, 1, centre, 500);
    if (!target) return fail("spawn zero-length grenade target");
    robot->z.grenades = 1;
    if (!Z_Attack(robot, &robot->info->attack, target) || robot->z.grenades != 1)
        return fail("zero-length grenade retains ammunition");
    rts_game_model_destroy(model);
    return 0;
}

static int test_grenade_scatter(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load grenade scatter fixture");
    fixed2_t at = fixed2_cell_center((ivec2_t){25,25});
    mobj_t *robot = blast_unit(MT_UNIT_GRUNT, 0, at, 100);
    mobj_t *target = blast_unit(MT_UNIT_MED_TANK, 1, pixel_at(at, 24, 0), 500);
    zmap_t *map = level.native_data;
    if (!robot || !target || !map) return fail("spawn grenade scatter fixture");
    robot->z.grenades = 4;
    uint16_t initial = map->combat_seed;
    ivec2_t origin = Z_AITopLeft(robot);
    fixed2_t boundary;
    if (!Z_UnitCentre(map, Z_UnitType(target), ivec2_add(origin, (ivec2_t){48,0}), &boundary))
        return fail("place grenade scatter boundary");
    target->core.position = fixed3_from_fixed2(boundary, 0);
    if (map->square_roots.bytes[36] != 6 || map->square_roots.bytes[49] != 7)
        return fail("native scatter distance boundary data");
    if (!Z_Attack(robot, &robot->info->attack, target) || map->combat_seed != initial)
        return fail("distance six consumes no scatter randomness");
    if (!Z_UnitCentre(map, Z_UnitType(target), ivec2_add(origin, (ivec2_t){56,0}), &boundary))
        return fail("place outside grenade scatter boundary");
    target->core.position = fixed3_from_fixed2(boundary, 0);
    if (!Z_Attack(robot, &robot->info->attack, target) || map->combat_seed == initial)
        return fail("distance seven consumes scatter randomness");
    map->combat_seed = initial;
    target->core.position = fixed3_from_fixed2(pixel_at(at, 96, 0), 0);
    uint32_t robot_id = robot->id, target_id = target->id;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!G_SaveGameBlob(&save, &size, "Grenade scatter", &app, rts_game_model_ai(model), &hud))
        return fail("save grenade scatter seed");
    uint16_t expected_seed = initial;
    int radius = (128 - (60 + 60 / 4)) / 4;
    int distance = Z_NextRandom(&expected_seed) / (65535 / radius);
    int angle = Z_NextRandom(&expected_seed) & 0x3fe;
    int sx = (int16_t)((map->trig.bytes[angle] << 8) | map->trig.bytes[angle + 1]);
    int sy = (int16_t)((map->trig.bytes[angle + 0x100] << 8) | map->trig.bytes[angle + 0x101]);
    ivec2_t expected = ivec2_add(Z_AITopLeft(target), (ivec2_t){8 + (((sx >> 5) * distance) >> 10),
                                                                 8 + (((sy >> 5) * distance) >> 10)});
    if (!Z_Attack(robot, &robot->info->attack, target) || map->combat_seed != expected_seed)
        return fail("far grenade consumes two native RNG draws");
    mobj_t *shot = nth(MT_GRENADE, 0, 2);
    if (!shot) return fail("find scattered grenade");
    zgrenade_t first = shot->z.grenade;
    ivec2_t dimensions = first.vertical ? (ivec2_t){first.minor, first.major} : (ivec2_t){first.major, first.minor};
    ivec2_t aim = {first.start.x + first.direction.x * dimensions.x, first.start.y + first.direction.y * dimensions.y};
    if (!ivec2_equal(aim, expected)) return fail("grenade uses native trig and scatter radius");
    if (!G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("restore grenade scatter seed");
    free(save);
    robot = P_MobjById(robot_id); target = P_MobjById(target_id); map = level.native_data;
    if (!robot || !target || !Z_Attack(robot, &robot->info->attack, target)) return fail("repeat saved grenade scatter");
    shot = nth(MT_GRENADE, 0, 2);
    if (!shot || map->combat_seed != expected_seed || shot->z.grenade.major != first.major ||
        shot->z.grenade.minor != first.minor || !ivec2_equal(shot->z.grenade.direction, first.direction))
        return fail("saved combat RNG reproduces grenade aim");
    mobj_t *bridge = blast_unit(MT_BRIDGE, Z_NEUTRAL, pixel_at(at, 96, 32), 500);
    if (!bridge) return fail("spawn direct grenade target");
    robot->z.grenades = 1;
    expected_seed = map->combat_seed;
    radius = (128 - (60 + 60 / 4)) / 2;
    distance = Z_NextRandom(&expected_seed) / (65535 / radius);
    angle = Z_NextRandom(&expected_seed) & 0x3fe;
    sx = (int16_t)((map->trig.bytes[angle] << 8) | map->trig.bytes[angle + 1]);
    sy = (int16_t)((map->trig.bytes[angle + 0x100] << 8) | map->trig.bytes[angle + 0x101]);
    ivec2_t point = fixed2_cell(fixed2_scale(fixed3_xy(bridge->core.position), FIXED_FROM_INT(Z_TILE)));
    expected = ivec2_add(point, (ivec2_t){((sx >> 5) * distance) >> 10, ((sy >> 5) * distance) >> 10});
    if (!Z_Attack(robot, &robot->info->attack, bridge) || map->combat_seed != expected_seed)
        return fail("direct grenade consumes two native RNG draws");
    shot = nth(MT_GRENADE, 0, 3);
    if (!shot) return fail("find direct grenade");
    zgrenade_t direct = shot->z.grenade;
    dimensions = direct.vertical ? (ivec2_t){direct.minor, direct.major} : (ivec2_t){direct.major, direct.minor};
    aim = (ivec2_t){direct.start.x + direct.direction.x * dimensions.x, direct.start.y + direct.direction.y * dimensions.y};
    if (!ivec2_equal(aim, expected)) return fail("direct grenade uses native wider scatter without unit offset");
    rts_game_model_destroy(model);
    return 0;
}

static int test_fire_truck_water(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load Fire Truck projectile fixture");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    fixed2_t at = fixed2_cell_center((ivec2_t){25, 25});
    mobj_t *truck = blast_unit(MT_UNIT_FIRE_TRUCK, 0, pixel_at(at, -80, 0), 200);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, at, 100);
    mobj_t *inside = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, -2, 10), 100);
    mobj_t *outside = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, -3, 10), 100);
    mobj_t *ally = blast_unit(MT_UNIT_GRUNT, 0, pixel_at(at, -2, 10), 100);
    if (!truck || !target || !inside || !outside || !ally) return fail("spawn Fire Truck projectile fixture");
    target->traits &= ~(MF_ATTACK | MF_MOBILE);
    inside->traits &= ~(MF_ATTACK | MF_MOBILE);
    outside->traits &= ~(MF_ATTACK | MF_MOBILE);
    ally->traits &= ~(MF_ATTACK | MF_MOBILE);
    truck->attack.target = target;
    P_UpdateSight();
    if (!P_Attack(truck)) return fail("launch Fire Truck projectile");
    truck->traits &= ~MF_ATTACK;
    mobj_t *water = nth(MT_WATER, 0, 0);
    if (!water || water->z.water.remaining <= 8 || water->missile.damage != 10 || states[water->core.state_id].frame != 1622 || inside->hp != 100)
        return fail("water launches native sprite with half damage and delayed flight");
    mobj_t *building = blast_unit(MT_ROBOT_FACTORY, 0, at, 500);
    if (!building) return fail("spawn water impact building");
    building->z.building = 1;
    ivec2_t impact = fixed2_cell(fixed2_scale(water->movement.goal, FIXED_FROM_INT(Z_TILE)));
    ivec2_t shift = ivec2_sub(ivec2_add(impact, (ivec2_t){16,16}), Z_BuildingOrigin(building));
    building->core.position = fixed3_from_fixed2(fixed2_add(fixed3_xy(building->core.position),
        fixed2_scale(fixed2_from_cell(shift), FIXED_ONE / Z_TILE)), 0);
    uint32_t water_id = water->id, inside_id = inside->id, outside_id = outside->id, ally_id = ally->id, building_id = building->id;
    int initial_distance = water->z.water.remaining;
    if (!tick(model, 1) || inside->hp != 100) return fail("water cannot hit before arrival");
    int native_steps = water->missile.age;
    if (native_steps < 2 || native_steps > 3 || initial_distance - water->z.water.remaining != native_steps * (native_steps + 1) / 2)
        return fail("water accelerates one pixel per native update");
    int saved_remaining = water->z.water.remaining, saved_error = water->z.water.error;
    uint32_t hash = G_Consistency();
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!G_SaveGameBlob(&save, &size, "Water flight", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save water projectile in flight");
    free(save);
    water = P_MobjById(water_id); inside = P_MobjById(inside_id);
    outside = P_MobjById(outside_id); ally = P_MobjById(ally_id); building = P_MobjById(building_id);
    if (!water || !inside || !outside || !ally || !building || water->missile.damage != 10 || water->missile.age != native_steps ||
        water->z.water.remaining != saved_remaining || water->z.water.error != saved_error || G_Consistency() != hash)
        return fail("restore water projectile and deterministic flight state");
    for (int i = 0; i < 60 && water->core.state_id == S_WATER; ++i)
        if (!tick(model, 1)) return fail("tick water to arrival");
    if (water->core.state_id != S_WATER_BLAST || inside->hp != 100 || building->hp != 490)
        return fail("water arrival damages buildings immediately and leaves units pending");
    save = NULL; size = 0;
    if (!G_SaveGameBlob(&save, &size, "Water impact", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save delayed water impact");
    free(save);
    water = P_MobjById(water_id); inside = P_MobjById(inside_id);
    outside = P_MobjById(outside_id); ally = P_MobjById(ally_id); building = P_MobjById(building_id);
    if (!water || !inside || !outside || !ally || !building || building->hp != 490 || !tick(model, 2) || inside->hp != 100)
        return fail("restored water blast waits before unit damage");
    if (!tick(model, 1)) return fail("tick delayed water damage");
    if (inside->hp != 90 || outside->hp != 100 || ally->hp != 100)
        return fail("water impact uses native inclusive bounds and excludes allies");
    if (!tick(model, 1) || inside->hp != 90) return fail("water blast damages each unit once");
    rts_game_model_destroy(model);
    return 0;
}

static int test_water_terrain(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load water terrain fixture");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    zmap_t *map = level.native_data;
    fixed2_t at = fixed2_cell_center((ivec2_t){25, 25});
    mobj_t *truck = blast_unit(MT_UNIT_FIRE_TRUCK, 0, at, 200);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 80, 0), 100);
    if (!map || !truck || !target) return fail("spawn water terrain fixture");
    for (int y = 24; y <= 25; ++y) for (int x = 24; x <= 31; ++x)
        map->ground_attr[y * Z_MAP_STRIDE + x] = 0;
    for (int y = 24; y <= 25; ++y) map->ground_attr[y * Z_MAP_STRIDE + 27] = 1; /* Height 3 exceeds launch height 0 + 1. */
    Z_RebuildCliffs(&level);
    target->traits &= ~(MF_ATTACK | MF_MOBILE);
    if (!Z_Attack(truck, &truck->info->attack, target)) return fail("launch water at raised terrain");
    truck->traits &= ~MF_ATTACK;
    mobj_t *water = nth(MT_WATER, 0, 0);
    if (!water) return fail("find water for terrain test");
    for (int i = 0; i < 60 && water->core.state_id == S_WATER; ++i)
        if (!tick(model, 1)) return fail("tick water into raised terrain");
    ivec2_t hit = fixed2_cell(fixed2_scale(water->movement.goal, FIXED_FROM_INT(Z_TILE)));
    if (water->core.state_id == S_WATER || hit.x < 27 * Z_TILE || hit.x >= 28 * Z_TILE || target->hp != 100)
        return fail("raised terrain intercepts water before its target");
    if (!tick(model, 20)) return fail("finish intercepted water effect");
    for (int y = 24; y <= 25; ++y) {
        map->ground_attr[y * Z_MAP_STRIDE + 27] = 11; /* Height 1 equals the threshold. */
        map->ground_attr[y * Z_MAP_STRIDE + 30] = 1;
    }
    Z_RebuildCliffs(&level);
    target->core.position = fixed3_from_fixed2(pixel_at(at, 68, 0), 0); /* Aim is exactly x = 30 tiles. */
    if (!Z_Attack(truck, &truck->info->attack, target)) return fail("launch water through equal-height terrain");
    water = nth(MT_WATER, 0, 0);
    if (!water) return fail("find second water projectile");
    for (int i = 0; i < 60 && water->core.state_id == S_WATER; ++i)
        if (!tick(model, 1)) return fail("tick second water projectile");
    hit = fixed2_cell(fixed2_scale(water->movement.goal, FIXED_FROM_INT(Z_TILE)));
    if (water->core.state_id == S_WATER || hit.x != 30 * Z_TILE)
        return fail("equal-height terrain passes and arrival precedes terminal terrain test");
    rts_game_model_destroy(model);
    return 0;
}

static int test_water_directions(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load directional water fixture");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    zmap_t *map = level.native_data;
    fixed2_t start = fixed2_cell_center((ivec2_t){25, 25});
    mobj_t *truck = blast_unit(MT_UNIT_FIRE_TRUCK, 0, start, 200);
    mobj_t *north = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(start, 20, -80), 100);
    mobj_t *west = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(start, -80, 20), 100);
    if (!map || !truck || !north || !west) return fail("spawn directional water fixture");
    for (int y = 18; y <= 31; ++y) for (int x = 18; x <= 31; ++x)
        map->terrain[y * Z_MAP_STRIDE + x] = 0;
    north->traits &= ~(MF_ATTACK | MF_MOBILE); west->traits &= ~(MF_ATTACK | MF_MOBILE);
    if (!Z_Attack(truck, &truck->info->attack, north) || !Z_Attack(truck, &truck->info->attack, west))
        return fail("launch directional water projectiles");
    truck->traits &= ~MF_ATTACK;
    mobj_t *a = nth(MT_WATER, 0, 0), *b = nth(MT_WATER, 0, 1);
    if (!a || !b || !a->z.water.vertical || b->z.water.vertical) return fail("water selects the major axis");
    fixed2_t goal_a = a->movement.goal, goal_b = b->movement.goal;
    if (!tick(model, 1) || a->core.position.y >= start.y || b->core.position.x >= start.x)
        return fail("steep and westward water travel along their negative axes");
    bool arrived_a = false, arrived_b = false;
    for (int i = 0; i < 60 && (!arrived_a || !arrived_b); ++i) {
        if (!tick(model, 1)) return fail("tick directional water projectiles");
        if (!arrived_a && a->core.state_id != S_WATER) {
            if (!fixed2_equal(a->movement.goal, goal_a)) return fail("steep water reaches its exact aim");
            arrived_a = true;
        }
        if (!arrived_b && b->core.state_id != S_WATER) {
            if (!fixed2_equal(b->movement.goal, goal_b)) return fail("westward water reaches its exact aim");
            arrived_b = true;
        }
    }
    if (!arrived_a || !arrived_b) return fail("directional water reaches its destination");
    rts_game_model_destroy(model);
    return 0;
}

static int test_water_launch(void) {
    static const ivec2_t hull[] = {{0,4},{-1,3},{-3,4},{0,1},{1,0},{4,1},{6,4},{4,4}};
    static const ivec2_t nozzle[] = {{-3,-19},{7,-16},{12,-11},{7,-5},{1,-3},{-7,-5},{-12,-9},{-10,-15}};
    static const ivec2_t bearing[] = {{0,-80},{80,-80},{80,0},{80,80},{0,80},{-80,80},{-80,0},{-80,-80},
                                     {80,-40},{80,40},{-80,-40},{-80,40},{39,-80},{40,-80},{-39,80},{-40,80}};
    static const uint8_t facing[] = {0,1,2,3,4,5,6,7,2,2,7,5,0,1,4,5};
    RtsGameModel *model = load();
    if (!model) return fail("load water launch fixture");
    fixed2_t start = fixed2_cell_center((ivec2_t){25, 25});
    mobj_t *truck = blast_unit(MT_UNIT_FIRE_TRUCK, 0, start, 200);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, start, 100);
    if (!truck || !target) return fail("spawn water launch fixture");
    ivec2_t origin = Z_AITopLeft(truck);
    for (int body = 0; body < 8; ++body) for (size_t i = 0; i < sizeof(facing); ++i) {
        truck->core.angle = direction_to_angle(body, 8, ANG90, true);
        ivec2_t aim = ivec2_add(ivec2_add(origin, (ivec2_t){14,12}), bearing[i]);
        fixed2_t centre;
        if (!Z_UnitCentre(level.native_data, Z_UnitType(target), ivec2_sub(aim, (ivec2_t){12,8}), &centre))
            return fail("water launch target center");
        target->core.position = fixed3_from_fixed2(centre, 0);
        if (!Z_Attack(truck, &truck->info->attack, target)) return fail("launch water in each direction");
        mobj_t *water = NULL;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *mo = (mobj_t *)th;
            if (th->function == P_MobjThinker && mo->type_id == MT_WATER && !mo->remove) water = mo;
        }
        ivec2_t expected = ivec2_add(origin, ivec2_add(hull[body], nozzle[facing[i]]));
        if (!water || !ivec2_equal(fixed2_cell(fixed2_scale(fixed3_xy(water->core.position), FIXED_FROM_INT(Z_TILE))), expected))
            return fail("water launch uses native hull and nozzle offsets");
        P_RemoveMobj(water);
    }
    rts_game_model_destroy(model);
    return 0;
}

static int test_gun_muzzle(void) {
    const int types[] = {MT_UNIT_LIGHT_GUN, MT_UNIT_HOWITZER, MT_UNIT_TOUGH};
    static const ivec2_t gun[] = {{13,-2},{24,1},{29,9},{22,17},{13,20},{4,18},{-4,9},{2,0}};
    static const ivec2_t tough[] = {{3,-5},{-1,-6},{5,-5},{3,-1},{-2,3},{-4,0},{-7,-2},{-2,-5}};
    RtsGameModel *model = load();
    if (!model) return fail("load gun muzzle fixture");
    fixed2_t at = fixed2_cell_center((ivec2_t){25,25});
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 96, 0), 500);
    if (!target) return fail("spawn gun muzzle target");
    for (int t = 0; t < 3; ++t) {
        mobj_t *attacker = blast_unit(types[t], 0, at, 500);
        if (!attacker) return fail("spawn gun muzzle attacker");
        for (int facing = 0; facing < 8; ++facing) {
            attacker->core.angle = direction_to_angle(facing, 8, ANG90, true);
            if (!Z_Attack(attacker, &attacker->info->attack, target)) return fail("launch gun by facing");
            mobj_t *shell = nth(MT_SHELL, 0, 0);
            ivec2_t start = ivec2_add(Z_AITopLeft(attacker), t == 2 ? tough[facing] : gun[facing]);
            if (!shell || !ivec2_equal(t == 2 ? shell->z.water.start : shell->z.grenade.start, start))
                return fail("gun and Tough use native facing-dependent muzzle");
            P_RemoveMobj(shell);
        }
        P_RemoveMobj(attacker);
    }
    rts_game_model_destroy(model);
    return 0;
}

static int test_tank_aim(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load tank aim fixture");
    zmap_t *map = level.native_data;
    fixed2_t at = fixed2_cell_center((ivec2_t){25,25});
    mobj_t *tank = blast_unit(MT_UNIT_MED_TANK, 0, at, 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, at, 500);
    if (!map || !tank || !target) return fail("spawn tank aim fixture");
    tank->core.angle = direction_to_angle(0, 8, ANG90, true);
    ivec2_t start = ivec2_add(Z_AITopLeft(tank), (ivec2_t){8,-13});
    fixed2_t centre;
    if (!Z_UnitCentre(map, Z_UnitType(target), ivec2_add(start, (ivec2_t){36,-8}), &centre))
        return fail("place near tank target");
    target->core.position = fixed3_from_fixed2(centre, 0);
    uint16_t seed = map->combat_seed;
    if (!Z_Attack(tank, &tank->info->attack, target) || map->combat_seed != seed)
        return fail("tank aim at distance six does not scatter");
    mobj_t *near = nth(MT_SHELL, 0, 0);
    if (!near || !ivec2_equal(fixed2_cell(fixed2_scale(near->movement.goal, FIXED_FROM_INT(Z_TILE))),
                              ivec2_add(start, (ivec2_t){48,0})))
        return fail("tank targets unit origin plus twelve and eight");
    P_RemoveMobj(near);
    if (!Z_UnitCentre(map, Z_UnitType(target), ivec2_add(start, (ivec2_t){84,-8}), &centre))
        return fail("place distant tank target");
    target->core.position = fixed3_from_fixed2(centre, 0);
    uint32_t tank_id = tank->id, target_id = target->id;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!G_SaveGameBlob(&save, &size, "Tank aim", &app, rts_game_model_ai(model), &hud)) return fail("save tank aim seed");
    uint16_t next = seed;
    int radius = (128 - (80 + 80 / 4)) / 4;
    int distance = Z_NextRandom(&next) / (65535 / radius);
    int angle = Z_NextRandom(&next) & 0x3fe;
    int sx = (int16_t)((map->trig.bytes[angle] << 8) | map->trig.bytes[angle + 1]);
    int sy = (int16_t)((map->trig.bytes[angle + 0x100] << 8) | map->trig.bytes[angle + 0x101]);
    ivec2_t expected = ivec2_add(ivec2_add(start, (ivec2_t){96,0}),
        (ivec2_t){((sx >> 5) * distance) >> 10, ((sy >> 5) * distance) >> 10});
    if (!Z_Attack(tank, &tank->info->attack, target) || map->combat_seed != next) return fail("tank draws native scatter values");
    mobj_t *shell = nth(MT_SHELL, 0, 0);
    if (!shell || !ivec2_equal(fixed2_cell(fixed2_scale(shell->movement.goal, FIXED_FROM_INT(Z_TILE))), expected))
        return fail("tank uses muzzle-relative native scatter");
    if (!G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("restore tank aim seed");
    free(save);
    tank = P_MobjById(tank_id); target = P_MobjById(target_id); map = level.native_data;
    if (!tank || !target || !Z_Attack(tank, &tank->info->attack, target) || map->combat_seed != next)
        return fail("repeat tank aim after load");
    shell = nth(MT_SHELL, 0, 0);
    if (!shell || !ivec2_equal(fixed2_cell(fixed2_scale(shell->movement.goal, FIXED_FROM_INT(Z_TILE))), expected))
        return fail("saved combat RNG reproduces tank aim");
    rts_game_model_destroy(model);
    return 0;
}

static int test_tank_muzzle(void) {
    const int types[] = {MT_UNIT_MED_TANK, MT_UNIT_HEAVY_TANK, MT_UNIT_LIGHT_TANK};
    static const ivec2_t offsets[3][8] = {
        {{8,-13},{28,-7},{30,3},{24,17},{9,19},{-4,15},{-14,2},{-7,-12}},
        {{7,-15},{21,-12},{31,3},{24,19},{8,22},{-10,17},{-18,4},{-8,-12}},
        {{3,-6},{15,-2},{22,9},{13,16},{3,19},{-6,14},{-14,7},{-6,-5}}
    };
    RtsGameModel *model = load();
    if (!model) return fail("load tank muzzle fixture");
    fixed2_t at = fixed2_cell_center((ivec2_t){25,25});
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 96, 0), 500);
    if (!target) return fail("spawn tank muzzle target");
    for (int t = 0; t < 3; ++t) {
        mobj_t *tank = blast_unit(types[t], 0, at, 500);
        if (!tank) return fail("spawn tank muzzle attacker");
        for (int facing = 0; facing < 8; ++facing) {
            tank->core.angle = direction_to_angle((facing + 3) & 7, 8, ANG90, true);
            tank->attack.aim = direction_to_angle(facing, 8, ANG90, true) - tank->core.angle;
            if (!Z_Attack(tank, &tank->info->attack, target)) return fail("launch tank by turret facing");
            mobj_t *shell = nth(MT_SHELL, 0, 0);
            if (!shell || !ivec2_equal(shell->z.grenade.start, ivec2_add(Z_AITopLeft(tank), offsets[t][facing])))
                return fail("tank uses native turret-facing muzzle");
            P_RemoveMobj(shell);
        }
        P_RemoveMobj(tank);
    }
    rts_game_model_destroy(model);
    return 0;
}

static int test_tank_shell_arc(void) {
    const int types[] = {MT_UNIT_LIGHT_TANK, MT_UNIT_MED_TANK, MT_UNIT_HEAVY_TANK, MT_UNIT_LIGHT_GUN, MT_UNIT_HOWITZER};
    const int speeds[] = {12, 10, 8, 10, 5};
    RtsGameModel *model = load();
    if (!model) return fail("load tank arc fixture");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    zmap_t *map = level.native_data;
    fixed2_t at = fixed2_cell_center((ivec2_t){25,25});
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 96, 0), 500);
    if (!map || !target) return fail("spawn tank arc target");
    for (int y = 23; y <= 27; ++y) for (int x = 23; x <= 34; ++x)
        map->ground_attr[y * Z_MAP_STRIDE + x] = 0;
    Z_RebuildCliffs(&level);
    target->traits &= ~(MF_ATTACK | MF_MOBILE);
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); ++i) {
        mobj_t *tank = blast_unit(types[i], 0, at, 500);
        if (!tank) return fail("spawn tank arc attacker");
        tank->traits &= ~MF_ATTACK;
        if (!Z_Attack(tank, &tank->info->attack, target)) return fail("launch tank arc shell");
        mobj_t *shell = nth(MT_SHELL, 0, (int)i);
        if (!shell || shell->z.grenade.speed != speeds[i] || shell->z.water.major || shell->core.frame != 1634)
            return fail("tank selects native arc projectile and artwork");
    }
    if (!tick(model, 1)) return fail("tick tank arc shells");
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); ++i) {
        mobj_t *shell = nth(MT_SHELL, 0, (int)i);
        if (!shell || shell->core.state_id != S_SHELL || shell->missile.age < 2 || shell->missile.age > 3 ||
            shell->z.grenade.major - shell->z.grenade.remaining != shell->missile.age * speeds[i])
            return fail("tank arc shell travels at constant native speed");
    }
    mobj_t *first = nth(MT_SHELL, 0, 0);
    uint32_t id = first->id, hash = G_Consistency();
    int remaining = first->z.grenade.remaining, sample = first->z.grenade.sample;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!G_SaveGameBlob(&save, &size, "Tank arc flight", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save tank arc flight");
    free(save);
    first = P_MobjById(id);
    if (!first || first->z.grenade.remaining != remaining || first->z.grenade.sample != sample || G_Consistency() != hash)
        return fail("restore tank arc flight");
    rts_game_model_destroy(model);
    model = load();
    if (!model) return fail("load tank arc terrain fixture");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    map = level.native_data;
    mobj_t *tank = blast_unit(MT_UNIT_LIGHT_TANK, 0, at, 500);
    target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 80, 0), 500);
    if (!map || !tank || !target) return fail("spawn tank arc terrain fixture");
    for (int y = 23; y <= 27; ++y) for (int x = 23; x <= 34; ++x)
        map->ground_attr[y * Z_MAP_STRIDE + x] = x == 27 ? 1 : 0;
    Z_RebuildCliffs(&level);
    tank->traits &= ~MF_ATTACK; target->traits &= ~(MF_ATTACK | MF_MOBILE);
    if (!Z_Attack(tank, &tank->info->attack, target)) return fail("launch tank shell into terrain");
    first = nth(MT_SHELL, 0, 0);
    if (!first) return fail("find terrain tank shell");
    for (int i = 0; i < 40 && first->core.state_id == S_SHELL; ++i)
        if (!tick(model, 1)) return fail("tick tank shell into terrain");
    ivec2_t hit = fixed2_cell(fixed2_scale(fixed3_xy(first->core.position), FIXED_FROM_INT(Z_TILE)));
    if (first->core.state_id != S_SHELL_BLAST || hit.x / Z_TILE != 27 ||
        !fixed2_equal(first->movement.goal, fixed3_xy(first->core.position)))
        return fail("tank arc terrain impact uses actual impact point");
    rts_game_model_destroy(model);
    return 0;
}

static int test_gun_aim(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load gun aim fixture");
    zmap_t *map = level.native_data;
    fixed2_t at = fixed2_cell_center((ivec2_t){25,25});
    mobj_t *gun = blast_unit(MT_UNIT_MISSILE_GUN, 0, at, 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 96, 0), 500);
    if (!map || !gun || !target) return fail("spawn gun aim fixture");
    gun->core.angle = direction_to_angle(0, 8, ANG90, true);
    gun->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1; gun->z.crew_count = 1;
    uint16_t seed = map->combat_seed, next = seed;
    int radius = (128 - (60 + 60 / 4)) / 4;
    int distance = Z_NextRandom(&next) / (65535 / radius);
    int angle = Z_NextRandom(&next) & 0x3fe;
    int sx = (int16_t)((map->trig.bytes[angle] << 8) | map->trig.bytes[angle + 1]);
    int sy = (int16_t)((map->trig.bytes[angle + 0x100] << 8) | map->trig.bytes[angle + 0x101]);
    ivec2_t expected = ivec2_add(ivec2_add(Z_AITopLeft(target), (ivec2_t){0,12}),
        (ivec2_t){((sx >> 5) * distance) >> 10, ((sy >> 5) * distance) >> 10});
    uint32_t gun_id = gun->id, target_id = target->id;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!G_SaveGameBlob(&save, &size, "Gun aim", &app, rts_game_model_ai(model), &hud)) return fail("save gun aim seed");
    if (!Z_Attack(gun, &gun->info->attack, target) || map->combat_seed != next) return fail("gun salvo draws one scatter pair");
    mobj_t *a = nth(MT_SHELL, 0, 0), *b = nth(MT_SHELL, 0, 1);
    if (!a || !b || !ivec2_equal(fixed2_cell(fixed2_scale(a->movement.goal, FIXED_FROM_INT(Z_TILE))), ivec2_add(expected, (ivec2_t){-3,0})) ||
        !ivec2_equal(fixed2_cell(fixed2_scale(b->movement.goal, FIXED_FROM_INT(Z_TILE))), ivec2_add(expected, (ivec2_t){3,0})))
        return fail("gun rockets share native scattered aim before port offsets");
    if (!G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("restore gun aim seed");
    free(save);
    gun = P_MobjById(gun_id); target = P_MobjById(target_id); map = level.native_data;
    if (!gun || !target || !Z_Attack(gun, &gun->info->attack, target) || map->combat_seed != next)
        return fail("repeat gun aim after load");
    a = nth(MT_SHELL, 0, 0); b = nth(MT_SHELL, 0, 1);
    if (!a || !b || !ivec2_equal(fixed2_cell(fixed2_scale(a->movement.goal, FIXED_FROM_INT(Z_TILE))), ivec2_add(expected, (ivec2_t){-3,0})) ||
        !ivec2_equal(fixed2_cell(fixed2_scale(b->movement.goal, FIXED_FROM_INT(Z_TILE))), ivec2_add(expected, (ivec2_t){3,0})))
        return fail("saved combat RNG reproduces gun salvo aim");
    rts_game_model_destroy(model);
    return 0;
}

static int test_rocket_salvos(void) {
    static const ivec2_t body[] = {{2,-2},{2,-2},{4,0},{1,-3},{2,1},{1,-2},{3,-3},{1,-4}};
    static const ivec2_t nose[] = {{-10,-4},{-3,-3},{0,0},{1,0},{3,2},{4,4},{-5,6},{-3,3}};
    static const ivec2_t mobile_side[2][8] = {
        {{-6,0},{-6,-6},{0,-6},{3,-6},{6,0},{6,6},{0,6},{-6,6}},
        {{6,0},{6,6},{0,6},{-6,6},{-6,0},{-6,-6},{0,-6},{6,-6}}
    };
    static const ivec2_t muzzle[2][8] = {
        {{4,-5},{11,-2},{25,6},{18,14},{10,16},{1,15},{-8,8},{-1,-2}},
        {{14,-5},{21,2},{25,13},{12,18},{-1,16},{-6,9},{-8,1},{2,-8}}
    };
    static const ivec2_t spread[2][8] = {
        {{-3,0},{-3,-3},{0,-3},{3,-3},{3,0},{3,3},{0,3},{-3,3}},
        {{3,0},{3,3},{0,3},{-3,3},{-3,0},{-3,-3},{0,-3},{3,-3}}
    };
    RtsGameModel *model = load();
    if (!model) return fail("load rocket salvo fixture");
    fixed2_t at = fixed2_cell_center((ivec2_t){25,25});
    mobj_t *mobile = blast_unit(MT_UNIT_M_MISSILE, 0, at, 500);
    mobj_t *gun = blast_unit(MT_UNIT_MISSILE_GUN, 0, at, 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 96, 0), 500);
    if (!mobile || !gun || !target) return fail("spawn rocket salvo fixture");
    ivec2_t aim = Z_BlastAnchor(target);
    for (int hull = 0; hull < 8; ++hull) for (int facing = 0; facing < 8; ++facing) {
        mobile->core.angle = direction_to_angle(hull, 8, ANG90, true);
        mobile->attack.aim = direction_to_angle(facing, 8, ANG90, true) - mobile->core.angle;
        if (!Z_Attack(mobile, &mobile->info->attack, target)) return fail("launch mobile missile salvo");
        for (int i = 0; i < 3; ++i) {
            mobj_t *shot = nth(MT_SHELL, 0, i);
            ivec2_t side = i ? mobile_side[i - 1][facing] : (ivec2_t){0,0};
            ivec2_t start = ivec2_add(Z_AITopLeft(mobile), ivec2_add(body[hull], ivec2_add(nose[facing], (ivec2_t){0,-8})));
            start = ivec2_add(start, side);
            if (!shot || shot->z.water.sprite != 5425 || shot->core.frame != 5425 ||
                !ivec2_equal(shot->z.water.start, start) ||
                !ivec2_equal(fixed2_cell(fixed2_scale(shot->movement.goal, FIXED_FROM_INT(Z_TILE))), ivec2_add(aim, side)))
                return fail("mobile missile uses native three-port geometry");
        }
        if (nth(MT_SHELL, 0, 3)) return fail("mobile missile fires exactly three rockets");
        for (int i = 0; i < 3; ++i) P_RemoveMobj(nth(MT_SHELL, 0, 0));
    }
    fixed2_t near_target;
    if (!Z_UnitCentre(level.native_data, Z_UnitType(target), ivec2_add(Z_AITopLeft(gun), (ivec2_t){16,0}), &near_target))
        return fail("place near missile-gun target");
    target->core.position = fixed3_from_fixed2(near_target, 0);
    ivec2_t gun_aim = ivec2_add(Z_AITopLeft(target), (ivec2_t){0,12});
    for (int facing = 0; facing < 8; ++facing) {
        gun->core.angle = direction_to_angle(facing, 8, ANG90, true);
        if (!Z_Attack(gun, &gun->info->attack, target)) return fail("launch missile gun salvo");
        for (int i = 0; i < 2; ++i) {
            mobj_t *shot = nth(MT_SHELL, 0, i);
            if (!shot || shot->z.water.sprite != 5345 || shot->core.frame != 5345 ||
                !ivec2_equal(shot->z.water.start, ivec2_add(Z_AITopLeft(gun), muzzle[i][facing])) ||
                !ivec2_equal(fixed2_cell(fixed2_scale(shot->movement.goal, FIXED_FROM_INT(Z_TILE))), ivec2_add(gun_aim, spread[i][facing])))
                return fail("missile gun uses native two-port geometry");
        }
        if (nth(MT_SHELL, 0, 2)) return fail("missile gun fires exactly two rockets");
        for (int i = 0; i < 2; ++i) P_RemoveMobj(nth(MT_SHELL, 0, 0));
    }
    zmap_t *map = level.native_data;
    target->core.position = fixed3_from_fixed2(pixel_at(at, 96, 0), 0);
    for (int y = 23; y <= 27; ++y) for (int x = 23; x <= 34; ++x)
        map->ground_attr[y * Z_MAP_STRIDE + x] = 0;
    Z_RebuildCliffs(&level);
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    mobile->traits &= ~MF_ATTACK; gun->traits &= ~MF_ATTACK; target->traits &= ~(MF_ATTACK | MF_MOBILE);
    gun->core.angle = direction_to_angle(2, 8, ANG90, true);
    if (!Z_Attack(gun, &gun->info->attack, target) || !tick(model, 1)) return fail("tick missile gun salvo");
    mobj_t *first = nth(MT_SHELL, 0, 0), *second = nth(MT_SHELL, 0, 1);
    if (!first || !second || first->core.state_id != S_SHELL || second->core.state_id != S_SHELL ||
        first->core.frame != 5345 || second->core.frame != 5345)
        return fail("both missile gun rockets remain in flight");
    uint32_t gun_id = gun->id, target_id = target->id, first_id = first->id, second_id = second->id;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!G_SaveGameBlob(&save, &size, "Missile gun salvo", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save missile gun rockets");
    free(save);
    gun = P_MobjById(gun_id); target = P_MobjById(target_id);
    first = P_MobjById(first_id); second = P_MobjById(second_id);
    if (!gun || !target || !first || !second || first->z.water.sprite != 5345 || second->z.water.sprite != 5345)
        return fail("restore both missile gun rockets");
    P_RemoveMobj(gun);
    if (!tick(model, RTS_TICRATE) || target->hp != 300)
        return fail("saved missile gun rockets hit independently after gun removal");
    rts_game_model_destroy(model);
    return 0;
}

static int test_shell_terrain(void) {
    for (int raised = 0; raised < 2; ++raised) {
        RtsGameModel *model = load();
        if (!model) return fail("load shell terrain fixture");
        P_AiSetFeatures(rts_game_model_ai(model), 0);
        zmap_t *map = level.native_data;
        fixed2_t at = fixed2_cell_center((ivec2_t){25,25});
        mobj_t *gun = blast_unit(MT_UNIT_M_MISSILE, 0, at, 500);
        mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 80, 0), 500);
        if (!map || !gun || !target) return fail("spawn shell terrain fixture");
        for (int y = 23; y <= 27; ++y) for (int x = 23; x <= 34; ++x)
            map->ground_attr[y * Z_MAP_STRIDE + x] = 0;
        Z_RebuildCliffs(&level);
        gun->traits &= ~MF_ATTACK; target->traits &= ~(MF_ATTACK | MF_MOBILE);
        if (!Z_Attack(gun, &gun->info->attack, target)) return fail("launch shell over terrain");
        mobj_t *shell = nth(MT_SHELL, 0, 0);
        if (!shell) return fail("find terrain shell");
        int barrier = shell->z.water.start.x / Z_TILE + 2;
        for (int y = 23; y <= 27; ++y) map->ground_attr[y * Z_MAP_STRIDE + barrier] = raised ? 1 : 11;
        Z_RebuildCliffs(&level);
        uint32_t id = shell->id;
        if (!tick(model, 1) || shell->core.state_id != S_SHELL) return fail("shell begins flight");
        int remaining = shell->z.water.remaining, error = shell->z.water.error;
        uint32_t hash = G_Consistency();
        app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
        if (!G_SaveGameBlob(&save, &size, "Shell terrain flight", &app, rts_game_model_ai(model), &hud) ||
            !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save shell terrain flight");
        free(save);
        shell = P_MobjById(id);
        if (!shell || shell->z.water.remaining != remaining || shell->z.water.error != error || G_Consistency() != hash)
            return fail("restore shell terrain flight");
        for (int i = 0; i < 80 && shell->core.state_id == S_SHELL; ++i)
            if (!tick(model, 1)) return fail("tick shell to terrain or arrival");
        ivec2_t hit = fixed2_cell(fixed2_scale(fixed3_xy(shell->core.position), FIXED_FROM_INT(Z_TILE)));
        if (shell->core.state_id != S_SHELL_BLAST || shell->core.position.z != 0 ||
            (raised ? hit.x / Z_TILE != barrier : hit.x / Z_TILE <= barrier))
            return fail("shell stops at high terrain and passes equal height");
        rts_game_model_destroy(model);
    }
    RtsGameModel *model = load();
    if (!model) return fail("load shell range fixture");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    zmap_t *map = level.native_data;
    fixed2_t at = fixed2_cell_center((ivec2_t){25,25});
    mobj_t *gun = blast_unit(MT_UNIT_M_MISSILE, 0, at, 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 200, 0), 500);
    if (!map || !gun || !target) return fail("spawn shell range fixture");
    for (int y = 23; y <= 27; ++y) for (int x = 23; x <= 40; ++x)
        map->ground_attr[y * Z_MAP_STRIDE + x] = 0;
    Z_RebuildCliffs(&level);
    gun->traits &= ~MF_ATTACK; target->traits &= ~(MF_ATTACK | MF_MOBILE);
    if (!Z_Attack(gun, &gun->info->attack, target)) return fail("launch shell beyond range");
    mobj_t *shell = nth(MT_SHELL, 0, 0);
    if (!shell || shell->z.water.range != 160 || states[S_SHELL].frame != 1622)
        return fail("shell uses native range and projectile artwork");
    ivec2_t start = shell->z.water.start;
    for (int i = 0; i < 80 && shell->core.state_id == S_SHELL; ++i)
        if (!tick(model, 1)) return fail("tick shell to range limit");
    ivec2_t hit = fixed2_cell(fixed2_scale(fixed3_xy(shell->core.position), FIXED_FROM_INT(Z_TILE)));
    if (shell->core.state_id != S_SHELL_BLAST || target->hp != 500 ||
        ivec2_distance_squared64(start, hit) < 160 * 160 || hit.x >= start.x + 200)
        return fail("shell stops at native weapon range");
    mobj_t *launcher = blast_unit(MT_UNIT_MISSILE_GUN, 0, at, 500);
    fixed2_t target_centre;
    ivec2_t source = fixed2_cell(fixed2_scale(at, FIXED_FROM_INT(Z_TILE)));
    if (!launcher || !Z_UnitCentre(map, MT_UNIT_GRUNT - MT_UNITS,
        ivec2_add(source, (ivec2_t){192,-8}), &target_centre)) return fail("spawn shell range boundary fixture");
    target->core.position = fixed3_from_fixed2(target_centre, 0);
    if (!Z_Attack(launcher, &launcher->info->attack, target)) return fail("launch boundary shell");
    mobj_t *boundary = nth(MT_SHELL, 0, 3);
    if (!boundary) return fail("find boundary shell");
    boundary->z.water.range = 40; /* A valid native range multiple, reached exactly at 5 px/update. */
    boundary->z.water.direction = (ivec2_t){1,0};
    boundary->z.water.major = boundary->z.water.remaining = 200;
    boundary->z.water.minor = 0; boundary->z.water.error = 100; boundary->z.water.vertical = false;
    for (int i = 0; i < 30 && boundary->core.state_id == S_SHELL; ++i)
        if (!tick(model, 1)) return fail("tick shell to exact range boundary");
    hit = fixed2_cell(fixed2_scale(fixed3_xy(boundary->core.position), FIXED_FROM_INT(Z_TILE)));
    if (boundary->core.state_id != S_SHELL_BLAST || ivec2_distance_squared64(boundary->z.water.start, hit) != 40 * 40)
        return fail("shell explodes on the exact range boundary");
    rts_game_model_destroy(model);
    return 0;
}

static int test_shell_flight(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for shell flight");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    disarm_except(0, NULL);
    fixed2_t start = fixed2_cell_center((ivec2_t){5, 5});
    mobj_t *gun = blast_unit(MT_UNIT_M_MISSILE, 0, start, 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(start, 96, 0), 500);
    if (!gun || !target || !Z_Attack(gun, &gun->info->attack, target)) return fail("launch shell");
    mobj_t *shell = nth(MT_SHELL, 0, 0);
    if (!shell || shell->z.water.remaining <= 1 || target->hp != 500) return fail("shell takes time to travel");
    uint32_t shell_id = shell->id, target_id = target->id;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!tick(model, 1) || target->hp != 500 ||
        !G_SaveGameBlob(&save, &size, "Shell flight", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save shell in flight");
    free(save);
    shell = P_MobjById(shell_id); target = P_MobjById(target_id);
    if (!shell || !target || !shell->target || shell->attack.target != target) return fail("load shell references");
    P_RemoveMobj(shell->target);
    if (!tick(model, RTS_TICRATE) || target->hp != 140) return fail("shell completes after its shooter is removed");
    puts("PASS: z shell travels and survives save and shooter removal");
    rts_game_model_destroy(model);
    return 0;
}

static int test_shell_aim(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for shell aim");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    disarm_except(0, NULL);
    zmap_t *map = level.native_data;
    for (int y = 3; y <= 9; ++y) for (int x = 3; x <= 17; ++x)
        map->ground_attr[y * Z_MAP_STRIDE + x] = 0;
    Z_RebuildCliffs(&level);
    fixed2_t start = fixed2_cell_center((ivec2_t){5, 5});
    mobj_t *gun = blast_unit(MT_UNIT_M_MISSILE, 0, start, 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(start, 96, 0), 500);
    mobj_t *first = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(start, 96, 0), 500);
    mobj_t *second = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(start, 128, 0), 500);
    if (!gun || !target || !first || !second) return fail("spawn shell aim fixtures");
    gun->traits &= ~MF_ATTACK; target->traits &= ~MF_ATTACK;
    first->traits &= ~MF_ATTACK; second->traits &= ~MF_ATTACK;
    if (!Z_Attack(gun, &gun->info->attack, target)) return fail("launch aimed shell");
    target->core.position = fixed3_from_fixed2(pixel_at(start, 128, 0), 0);
    if (!tick(model, RTS_TICRATE) || first->hp != 140 || target->hp != 500 || second->hp != 500)
        return fail("moving target does not redirect shell");
    if (!Z_Attack(gun, &gun->info->attack, target)) return fail("launch at removable target");
    P_RemoveMobj(target);
    if (!tick(model, RTS_TICRATE) || second->hp != 260) return fail("shell survives removal of its target");
    puts("PASS: z shells retain their aim when targets move or are removed");
    rts_game_model_destroy(model);
    return 0;
}

static int test_shell_speed(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for shell speed");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    disarm_except(0, NULL);
    fixed2_t start = fixed2_cell_center((ivec2_t){5, 5});
    isize2_t robot_size;
    if (!Z_SpriteSize(level.native_data, 66, &robot_size)) return fail("shell timing sprite size");
    int x = 96 - 8 + robot_size.w / 2;
    int y = -8 + robot_size.h / 2;
    mobj_t *gun = blast_unit(MT_UNIT_M_MISSILE, 0, start, 500);
    mobj_t *cardinal = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(start, x, y), 500);
    mobj_t *diagonal = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(start, x, y + 96), 500);
    if (!gun || !cardinal || !diagonal) return fail("spawn shell speed fixtures");
    gun->traits &= ~MF_ATTACK; cardinal->traits &= ~MF_ATTACK; diagonal->traits &= ~MF_ATTACK;
    if (!Z_Attack(gun, &gun->info->attack, cardinal) || !Z_Attack(gun, &gun->info->attack, diagonal))
        return fail("launch cardinal and diagonal shells");
    mobj_t *shots[6];
    for (int i = 0; i < 6; ++i) shots[i] = nth(MT_SHELL, 0, i);
    mobj_t *a = shots[0], *b = shots[3];
    for (int i = 0; i < 6; ++i) if (i != 0 && i != 3 && shots[i]) P_RemoveMobj(shots[i]);
    if (a && b) {
        ivec2_t origin = fixed2_cell(fixed2_scale(start, FIXED_FROM_INT(Z_TILE)));
        a->core.position = b->core.position = fixed3_from_fixed2(start, 0);
        a->z.water.start = b->z.water.start = origin;
        a->z.water.direction = (ivec2_t){1,0}; b->z.water.direction = (ivec2_t){1,1};
        a->z.water.major = b->z.water.major = a->z.water.remaining = b->z.water.remaining = 96;
        a->z.water.minor = 0; b->z.water.minor = 96;
        a->z.water.error = b->z.water.error = 48;
        a->z.water.vertical = b->z.water.vertical = false;
        a->movement.goal = pixel_at(start, 96, 0); b->movement.goal = pixel_at(start, 96, 96);
    }
    /* Both travel 96 major-axis pixels and accelerate to 4 pixels per native step. */
    if (!a || !b || a->z.water.remaining != 96 || b->z.water.remaining != 96 ||
        a->z.water.speed != 4 || b->z.water.speed != 4)
        return fail("cardinal and diagonal shots use the same major-axis rate");
    if (!tick(model, 1) || a->missile.age < 2 || a->missile.age > 3 ||
        a->missile.age != b->missile.age || a->z.water.remaining != 96 - a->missile.age * (a->missile.age + 1) / 2 ||
        a->z.water.remaining != b->z.water.remaining)
        return fail("cardinal and diagonal shots accelerate identically");
    for (int i = 0; i < 30 && a->core.state_id == S_SHELL; ++i)
        if (!tick(model, 1)) return fail("tick shell to arrival");
    if (a->core.state_id != S_SHELL_BLAST || b->core.state_id != S_SHELL_BLAST || cardinal->hp != 500 || diagonal->hp != 500)
        return fail("shells arrive together before unit damage");
    uint32_t cardinal_id = cardinal->id, diagonal_id = diagonal->id, gun_id = gun->id;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!G_SaveGameBlob(&save, &size, "Shell impacts", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud))
        return fail("save pending shell impacts");
    free(save);
    cardinal = P_MobjById(cardinal_id); diagonal = P_MobjById(diagonal_id); gun = P_MobjById(gun_id);
    if (!cardinal || !diagonal || !gun) return fail("restore pending shell impacts");
    if (!tick(model, 3) || cardinal->hp != 380 || diagonal->hp != 380)
        return fail("shells damage units after the impact delay");
    if (!tick(model, 1) || cardinal->hp != 380 || diagonal->hp != 380)
        return fail("shell unit impacts do not repeat");
    puts("PASS: z shell flight uses native major-axis speed");
    rts_game_model_destroy(model);
    return 0;
}

static int test_body_slowdown(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for body damage");
    fixed2_t at = fixed2_cell_center((ivec2_t){5, 5});
    mobj_t *tank = blast_unit(MT_UNIT_MED_TANK, 0, at, 200);
    mobj_t *direct = blast_unit(MT_UNIT_MED_TANK, 0, at, 200);
    mobj_t *robot = blast_unit(MT_UNIT_GRUNT, 0, at, 200);
    mobj_t *jeep = blast_unit(MT_UNIT_JEEP, 0, at, 200);
    if (!tank || !direct || !robot || !jeep) return fail("spawn bodies for slowdown");
    P_DamageMobj(tank, NULL, 49);
    if (tank->z.body_speed != 40 || tank->speed != Z_LevelSpeed(4)) return fail("above three quarters keeps speed");
    P_DamageMobj(tank, NULL, 1);
    if (tank->z.body_speed != 36 || tank->z.body_fire != 63 || tank->z.body_accuracy != 72 || tank->speed != Z_LevelSpeed(3) ||
        G_WeaponCooldown(tank, &tank->info->attack, tank->info->attack.cooldown_ms) != 37 * 1000 / Z_NATIVE_HZ)
        return fail("three quarters scales speed and firing rate to 90 percent");
    P_DamageMobj(tank, NULL, 50);
    if (tank->z.body_speed != 30 || tank->z.body_fire != 53 || tank->z.body_accuracy != 61 || tank->speed != Z_LevelSpeed(3)) return fail("half compounds the 85 percent scale");
    P_DamageMobj(tank, NULL, 50);
    if (tank->z.body_speed != 24 || tank->z.body_fire != 42 || tank->z.body_accuracy != 48 || tank->speed != Z_LevelSpeed(2)) return fail("quarter compounds the 80 percent scale");
    P_DamageMobj(direct, NULL, 151);
    if (direct->z.body_speed != 32 || direct->speed != Z_LevelSpeed(3)) return fail("one hit crossing multiple thresholds applies only the lowest");
    P_DamageMobj(robot, NULL, 151);
    if (robot->z.body_tracked || robot->speed != robot->info->speed) return fail("robot health does not scale vehicle body speed");
    P_DamageMobj(jeep, NULL, 50);
    if (jeep->z.body_fire != 98 || G_WeaponCooldown(jeep, &jeep->info->attack, jeep->info->attack.cooldown_ms) != jeep->info->attack.cooldown_ms)
        return fail("Jeep retains its native firing rate");
    mobj_t *victim = blast_unit(MT_UNIT_JEEP, 1, at, 200);
    if (!victim) return fail("spawn driver accuracy victim");
    victim->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    jeep->z.body_accuracy = 0;
    for (int i = 0; i < 128; ++i, ++leveltime) Z_HitDamage(jeep, &jeep->info->attack, victim, 1, 1);
    if (victim->z.crew_hurt) return fail("zero accuracy cannot hit a driver");
    jeep->z.body_accuracy = 127;
    for (int i = 0; i < 128 && !victim->z.crew_hurt; ++i, ++leveltime)
        Z_HitDamage(jeep, &jeep->info->attack, victim, 1, 1);
    if (!victim->z.crew_hurt) return fail("degraded accuracy is used for driver hits");
    uint8_t squad = Z_AllocateSquad(victim->team);
    victim->z.squad = squad;
    zmap_t *map = level.native_data;
    for (int i = 0; i < 128 && victim->z.driver; ++i, ++leveltime)
        Z_HitDamage(jeep, &jeep->info->attack, victim, 255, 1);
    if (!squad || victim->z.driver || victim->team != Z_NEUTRAL || victim->z.squad ||
        map->squad_cooldown[squad - 1] != 200)
        return fail("driver casualty releases the occupied vehicle's group");
    direct->hp = 160;
    P_DamageMobj(direct, NULL, 10);
    if (direct->z.body_speed != 28) return fail("damage after healing uses actual previous health");
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    uint32_t id = tank->id;
    if (!G_SaveGameBlob(&save, &size, "Body damage", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save and load damaged body");
    free(save);
    tank = P_MobjById(id);
    if (!tank || tank->z.body_speed != 24 || tank->z.body_fire != 42 || tank->z.body_accuracy != 48 || tank->speed != Z_LevelSpeed(2))
        return fail("save retains mutable body speed, firing rate and accuracy");
    uint32_t hash = G_Consistency();
    tank->z.body_speed++;
    if (G_Consistency() == hash) return fail("body speed contributes to consistency");
    tank->z.body_speed--;
    tank->z.body_fire++;
    if (G_Consistency() == hash) return fail("body fire rate contributes to consistency");
    tank->z.body_fire--;
    tank->z.body_accuracy++;
    if (G_Consistency() == hash) return fail("body accuracy contributes to consistency");
    tank->z.body_accuracy--;
    tank->z.body_tracked = false;
    if (G_Consistency() == hash) return fail("body initialization contributes to consistency");
    puts("PASS: z vehicle damage scales its native speed at health thresholds");
    rts_game_model_destroy(model);
    return 0;
}

/* Only `squad`'s members and `keep` fight; everyone else stands down. */
static void disarm_except(int squad, const mobj_t *keep) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo != keep && (!squad || mo->z.squad != squad)) mo->traits &= ~MF_ATTACK;
        else if (mo->info) mo->traits |= mo->info->traits & MF_ATTACK;
    }
}

static int cells_apart(const mobj_t *a, const mobj_t *b) {
    ivec2_t d = ivec2_sub(fixed2_cell(fixed3_xy(a->core.position)), fixed2_cell(fixed3_xy(b->core.position)));
    return abs(d.x) > abs(d.y) ? abs(d.x) : abs(d.y);
}

/* A shot robot bursts into bits (290..295) and is gone within a second;
 * it no longer sits on the field for seconds. */
static int test_death(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for a death");
    mobj_t *grunt = nth(MT_UNIT_GRUNT, 1, 0);
    if (!grunt) return fail("a computer Grunt");
    const state_t *die = &states[mobjinfo[grunt->type_id].deathstate];
    if (die->frame != Z_ROBOT_DIE || P_StateFrames(die) != Z_ROBOT_DIE_FRAMES - 1)
        return fail("the death runs the burst pictures");
    P_DamageMobj(grunt, NULL, grunt->hp);
    if (grunt->core.state_id != S_GRUNT_DIE) return fail("the Grunt starts dying");
    int frames = 0, last = -1;
    for (int t = 0; t < RTS_TICRATE && alive(grunt); ++t) {
        if (grunt->core.frame != last) frames++;
        last = grunt->core.frame;
        if (!tick(model, 1)) return fail("tick");
    }
    if (alive(grunt)) return fail("the dead Grunt goes");
    if (frames < Z_ROBOT_DIE_FRAMES) return fail("it shows every burst picture");
    puts("PASS: z a robot bursts and is gone");
    rts_game_model_destroy(model);
    return 0;
}

/* A Grunt ordered onto a robot beyond its reach keeps that target and
 * walks in, even with another enemy in reach on the way. */
static int test_ordered_target(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for an order");
    mobj_t *grunt = nth(MT_UNIT_GRUNT, 0, 0), *mark = nth(MT_UNIT_GRUNT, 1, 0);
    mobj_t *other = mark ? other_squad(mark->z.squad) : NULL;
    if (!grunt || !mark || !other) return fail("a Grunt and two computer squads");
    disarm_except(0, grunt);
    ivec2_t at = fixed2_cell(fixed3_xy(mark->core.position));
    place(grunt, (ivec2_t){at.x, at.y + 9});
    place(other, (ivec2_t){at.x + 3, at.y + 9});
    grunt->hp = grunt->max_hp = 10000;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        P_MobjSetSelected((mobj_t *)th, (mobj_t *)th == grunt);
    RtsGameCommand order = { .kind = RTS_GAME_COMMAND_ATTACK_UNIT, .data.attack_unit = { .target_id = mark->id } };
    if (!rts_game_model_command(model, &order)) return fail("order the attack");
    int start = cells_apart(grunt, mark);
    for (int t = 0; t < 2 * RTS_TICRATE; ++t) {
        if (!tick(model, 1)) return fail("tick");
        if (grunt->attack.target != mark) return fail("the ordered target sticks");
    }
    if (cells_apart(grunt, mark) >= start) return fail("the Grunt closes in");
    puts("PASS: z an ordered target is chased, not swapped for one in reach");
    rts_game_model_destroy(model);
    return 0;
}

/* A Grunt shot at from beyond its squad's reach turns on the shooter and
 * its squad mates go for it too; an idle squad notices an enemy that comes
 * within a few cells of its reach. */
static int test_alert(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for alerts");
    /* Idle squads, as p_alert.c sees them: no computer player sends them off. */
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    mobj_t *mark = nth(MT_UNIT_GRUNT, 1, 0), *jeep = nth(MT_UNIT_JEEP, Z_NEUTRAL, 0);
    if (!mark || !jeep || !mark->z.squad) return fail("a squad and a Jeep");
    int squad = mark->z.squad;
    Z_SetTeam(jeep, 0);
    jeep->traits |= jeep->info->traits & (MF_SELECTABLE | MF_MOBILE);
    jeep->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    jeep->hp = jeep->max_hp = 10000;
    disarm_except(squad, NULL);
    ivec2_t at = fixed2_cell(fixed3_xy(mark->core.position));
    place(jeep, (ivec2_t){at.x + 12, at.y});
    if (!tick(model, RTS_TICRATE)) return fail("tick");
    for (int i = 0; member(squad, i); ++i)
        if (member(squad, i)->attack.target) return fail("the squad starts idle");
    P_DamageMobj(mark, jeep, 1);
    if (mark->attack.target != jeep) return fail("the shot Grunt turns on the Jeep");
    if (!tick(model, RTS_TICRATE)) return fail("tick");
    for (int i = 0; member(squad, i); ++i)
        if (member(squad, i)->attack.target != jeep) return fail("its squad joins in");

    mobj_t *guard = other_squad(squad);
    if (!guard) return fail("a second squad");
    disarm_except(guard->z.squad, NULL);
    at = fixed2_cell(fixed3_xy(guard->core.position));
    place(jeep, (ivec2_t){at.x, at.y + 12});
    if (!tick(model, RTS_TICRATE)) return fail("tick");
    if (guard->attack.target) return fail("a far Jeep goes unnoticed");
    place(jeep, (ivec2_t){at.x, at.y + 7});
    if (!tick(model, RTS_TICRATE)) return fail("tick");
    if (guard->attack.target != jeep) return fail("a near Jeep is noticed");
    puts("PASS: z squads answer for their members and notice enemies nearby");
    rts_game_model_destroy(model);
    return 0;
}

static int test_shell_wreck_preemption(void) {
    static const int types[] = {MT_EX_DEAD_TRUCK, MT_DEAD_DML, MT_DEAD_GATTLING,
                                MT_DEAD_FIELD_GUN, MT_DEAD_HOWITZER};
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); ++i) {
        RtsGameModel *model = load();
        if (!model) return fail("load wreck impact fixture");
        fixed2_t at = fixed2_cell_center((ivec2_t){25, 25});
        ivec2_t centre = fixed2_cell(fixed2_scale(at, FIXED_FROM_INT(Z_TILE)));
        ivec2_t origin = ivec2_sub(centre, (ivec2_t){8, 8});
        isize2_t size;
        if (!Z_SpriteSize(level.native_data, states[mobjinfo[types[i]].spawnstate].frame, &size))
            return fail("wreck sprite size");
        fixed2_t wreck_at = fixed2_scale(fixed2_from_cell(ivec2_add(origin, (ivec2_t){size.w / 2, size.h / 2})), FIXED_ONE / Z_TILE);
        mobj_t *wreck = blast_unit(types[i], Z_NEUTRAL, wreck_at, 100);
        mobj_t *box = blast_unit(MT_GRENADES, Z_NEUTRAL, at, 100);
        mobj_t *ordinary = blast_unit(MT_BOX, Z_NEUTRAL, at, 100);
        mobj_t *shell = P_SpawnMobj(fixed3_from_fixed2(at, 0), MT_SHELL);
        if (!wreck || !box || !ordinary || !shell) return fail("spawn wreck impact fixture");
        ordinary->z.object_slot = 1; wreck->z.object_slot = 2; box->z.object_slot = 3;
        wreck->z.object_hp = box->z.object_hp = 2;
        shell->movement.goal = fixed2_scale(fixed2_from_cell(ivec2_add(origin, (ivec2_t){33, 8})), FIXED_ONE / Z_TILE);
        shell->missile.damage = 1; shell->team = 1; shell->attack.target = wreck;
        ivec2_t cell = {origin.x / Z_TILE, (origin.y + 16) / Z_TILE};
        int index = cell.y * Z_MAP_STRIDE + cell.x;
        zmap_t *map = level.native_data;
        map->ground_attr[index] = 0x87;
        A_ZBlastImpact(shell);
        if (wreck->z.object_hp != 2 || box->z.object_hp != 2) return fail("wreck probe excludes 25 pixels");
        shell->movement.goal = fixed2_scale(fixed2_from_cell(ivec2_add(origin, (ivec2_t){32, 8})), FIXED_ONE / Z_TILE);
        A_ZBlastImpact(shell);
        if (wreck->remove || wreck->z.object_hp != 1 || wreck->hp != 25 || box->z.object_hp != 2 ||
            ordinary->remove || map->ground_attr[index] != 0x87)
            return fail("eligible wreck preempts the crate and takes one effect hit");
        shell->movement.goal = fixed2_scale(fixed2_from_cell(ivec2_add(origin, (ivec2_t){8, 8})), FIXED_ONE / Z_TILE);
        uint32_t wreck_id = wreck->id, box_id = box->id, shell_id = shell->id;
        app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t bytes = 0;
        if (!G_SaveGameBlob(&save, &bytes, "Damaged wreck", &app, rts_game_model_ai(model), &hud) ||
            !G_LoadGameBlob(save, bytes, &app, rts_game_model_ai(model), &hud)) return fail("save damaged wreck");
        free(save);
        wreck = P_MobjById(wreck_id); box = P_MobjById(box_id); shell = P_MobjById(shell_id);
        map = level.native_data;
        if (!wreck || !box || !shell || wreck->z.object_hp != 1) return fail("restore damaged wreck");
        A_ZBlastImpact(shell);
        if (!wreck->remove || box->z.object_hp != 2 || map->ground_attr[index] != (i < 2 ? 0x87 : 0x96))
            return fail("fatal wreck hit updates only its native terrain and stops at the wreck");
        A_ZBlastImpact(shell);
        if (box->remove || box->z.object_hp != 1) return fail("next impact reaches the crate");
        save = NULL; bytes = 0;
        if (!G_SaveGameBlob(&save, &bytes, "Destroyed wreck", &app, rts_game_model_ai(model), &hud) ||
            !G_LoadGameBlob(save, bytes, &app, rts_game_model_ai(model), &hud)) return fail("save destroyed wreck");
        free(save);
        map = level.native_data;
        if (map->ground_attr[index] != (i < 2 ? 0x87 : 0x96) || map->terrain[index] != (i < 2 ? 0x87 : 0x96))
            return fail("restore wreck terrain change");
        rts_game_model_destroy(model);
    }
    return 0;
}

static int test_ejected_hull_impact(void) {
    static const int types[] = {MT_UNIT_APC, MT_UNIT_LIGHT_GUN, MT_UNIT_GATLING,
                                MT_UNIT_HOWITZER, MT_UNIT_MISSILE_GUN, MT_UNIT_APC};
    static const int objects[] = {MT_A_P_C, MT_GUN, MT_GATTLING, MT_HOWITZER, MT_MISSILE, MT_A_P_C};
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); ++i) {
        RtsGameModel *model = load();
        if (!model) return fail("load ejected hull impact");
        fixed2_t at = fixed2_cell_center((ivec2_t){25, 25});
        if (i == 5) for (int slot = 1; slot <= 200; ++slot) {
            mobj_t *occupied = blast_unit(MT_GRENADES, Z_NEUTRAL, fixed2_cell_center((ivec2_t){1, 1}), 1);
            if (!occupied) return fail("fill represented effect slots");
            occupied->z.object_slot = (uint8_t)slot;
        }
        mobj_t *hull = blast_unit(types[i], 0, at, 100);
        if (!hull) return fail("spawn ejected hull");
        hull->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
        if (!Z_EjectCrew(hull) || hull->z.object != objects[i] || hull->z.object_hp != 4 ||
            (!!hull->z.object_slot != (i != 5)) || !fixed2_equal(fixed3_xy(hull->core.position), at))
            return fail("ejection registers its neutral hull and preserves position");
        isize2_t size;
        if (!Z_SpriteSize(level.native_data, states[hull->core.state_id].frame, &size))
            return fail("ejected hull sprite");
        ivec2_t centre = fixed2_cell(fixed2_scale(at, FIXED_FROM_INT(Z_TILE)));
        ivec2_t origin = ivec2_sub(centre, (ivec2_t){size.w / 2, size.h / 2});
        mobj_t *shell = P_SpawnMobj(fixed3_from_fixed2(at, 0), MT_SHELL);
        if (!shell) return fail("spawn ejected hull shell");
        shell->movement.goal = fixed2_scale(fixed2_from_cell(ivec2_add(origin, (ivec2_t){8, 8})), FIXED_ONE / Z_TILE);
        shell->missile.damage = 1; shell->attack.target = hull; shell->team = 1;
        uint32_t hull_id = hull->id, shell_id = shell->id;
        app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t bytes = 0;
        if (!G_SaveGameBlob(&save, &bytes, "Ejected hull", &app, rts_game_model_ai(model), &hud) ||
            !G_LoadGameBlob(save, bytes, &app, rts_game_model_ai(model), &hud))
            return fail("save ejected hull");
        free(save);
        hull = P_MobjById(hull_id); shell = P_MobjById(shell_id);
        if (!hull || !shell || hull->z.object != objects[i] || hull->z.object_hp != 4 ||
            (!!hull->z.object_slot != (i != 5))) return fail("restore ejected hull effect identity");
        A_ZBlastImpact(shell);
        A_ZBlastHit(shell);
        if (hull->remove || hull->z.object_hp != 3 || hull->hp != 75)
            return fail("shell damages an ejected hull once");
        mobj_t *robot = blast_unit(MT_UNIT_GRUNT, 0, fixed3_xy(hull->core.position), 30);
        if (!robot || !Z_DetourBoard(robot, hull) || hull->hp != 75 || hull->z.object_slot)
            return fail("reboarding retains damage and releases the effect slot");
        rts_game_model_destroy(model);
    }
    return 0;
}

int main(void) {
    if (test_jeep_bullet() || test_gatling_bullet() || test_specialist_bullets() || test_grunt_bullet() || test_bullet_terrain_gate() || test_shell_wreck_preemption() || test_ejected_hull_impact() || test_bad_crew_combat() || test_apc_driver_cooldown() || test_apc_driver_range() || test_apc_firing_intervals() || test_apc_bullet_salvo() || test_apc_pyro_phase() || test_apc_shell_salvo() || test_death() || test_ordered_target() || test_alert() || test_pyro_cadence() || test_grenade_cooldown() || test_gun_drivers() || test_shell_blast() || test_shell_building_contact() || test_shell_grenade_box() || test_shell_empty_object() || test_grenade_impact() || test_grenade_speed() || test_grenade_terrain() || test_grenade_launch() || test_grenade_scatter() || test_fire_truck_water() || test_water_terrain() || test_water_directions() || test_water_launch() || test_body_slowdown() ||
        test_gun_muzzle() || test_tank_aim() || test_tank_muzzle() || test_tank_shell_arc() || test_gun_aim() || test_rocket_salvos() || test_shell_terrain() || test_shell_flight() || test_shell_aim() || test_shell_speed()) return 1;
    return 0;
}
