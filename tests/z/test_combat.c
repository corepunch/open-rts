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
    if (robot->z.grenades != 1 || robot->attack.target != infantry || infantry->hp >= 500 ||
        robot->attack.cooldown_left_ms != 20 * 1000 / Z_NATIVE_HZ)
        return fail("retargeted rifle shot keeps ordinary cooldown and grenade");
    rts_game_model_destroy(model);
    return 0;
}

static int test_gun_drivers(void) {
    static const int types[] = {MT_UNIT_LIGHT_GUN, MT_UNIT_GATLING, MT_UNIT_HOWITZER, MT_UNIT_MISSILE_GUN};
    RtsGameModel *model = load();
    if (!model) return fail("load for gun drivers");
    fixed2_t at = fixed2_cell_center((ivec2_t){5, 5});
    mobj_t *shooter = blast_unit(MT_UNIT_SNIPER, 0, at, 500);
    if (!shooter) return fail("spawn gun driver attacker");
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); ++i) {
        mobj_t *gun = blast_unit(types[i], 1, pixel_at(at, 16, 0), 500);
        if (!gun) return fail("spawn gun driver");
        gun->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
        for (int n = 0; n < 256 && !gun->z.crew_hurt; ++n, ++leveltime)
            Z_HitDamage(shooter, &shooter->info->attack, gun, 1, 1);
        if (!gun->z.crew_hurt) return fail("all class-2 guns can take driver hits");
        for (int n = 0; n < 256 && gun->z.driver; ++n, ++leveltime)
            Z_HitDamage(shooter, &shooter->info->attack, gun, 255, 1);
        if (gun->z.driver || gun->team != Z_NEUTRAL)
            return fail("fatal driver hit leaves each gun neutral");
    }
    mobj_t *tank = blast_unit(MT_UNIT_MED_TANK, 1, pixel_at(at, 16, 0), 500);
    if (!tank) return fail("spawn protected tank driver");
    tank->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    for (int n = 0; n < 256; ++n, ++leveltime)
        Z_HitDamage(shooter, &shooter->info->attack, tank, 1, 1);
    if (tank->z.crew_hurt) return fail("ordinary tank does not use the open gun driver rule");
    mobj_t *boundary = blast_unit(MT_UNIT_JEEP, 1, pixel_at(at, 16, 0), 500);
    if (!boundary) return fail("spawn driver health boundary fixture");
    boundary->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    boundary->z.crew_count = 1;
    boundary->z.crew[0] = (zcrew_t){.hp = 15, .type = MT_UNIT_GRUNT - MT_UNITS};
    for (int n = 0; n < 256 && !boundary->z.crew_hurt; ++n, ++leveltime)
        Z_HitDamage(shooter, &shooter->info->attack, boundary, 15, 1);
    if (boundary->z.crew_hurt != 15 || !boundary->z.driver || boundary->team != 1)
        return fail("driver at zero health remains aboard at native signed boundary");
    for (int n = 0; n < 256 && boundary->z.driver; ++n, ++leveltime)
        Z_HitDamage(shooter, &shooter->info->attack, boundary, 1, 1);
    if (boundary->z.driver || boundary->team != Z_NEUTRAL)
        return fail("next driver hit below zero empties the Jeep");
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
        if (!gun->z.driver && gun->hp != before) return fail("fatal driver hit does not damage gun body");
    }
    if (gun->z.driver || gun->team != Z_NEUTRAL) return fail("real attack empties the gun");
    mobj_t *robot = blast_unit(MT_UNIT_GRUNT, 0, fixed3_xy(gun->core.position), 30);
    if (!robot || !Z_DetourBoard(robot, gun) || gun->team != 0 ||
        gun->z.driver != MT_UNIT_GRUNT - MT_UNITS + 1)
        return fail("a robot can board a gun after its driver is killed");
    rts_game_model_destroy(model);
    return 0;
}

static int test_apc_bullet_salvo(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for APC bullet salvo");
    fixed2_t at = fixed2_cell_center((ivec2_t){5, 5});
    mobj_t *apc = blast_unit(MT_UNIT_APC, 0, at, 500);
    mobj_t *robot = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 16, 0), 500);
    mobj_t *tank = blast_unit(MT_UNIT_MED_TANK, 1, pixel_at(at, 32, 0), 500);
    if (!apc || !robot || !tank) return fail("spawn APC bullet salvo fixture");
    apc->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    apc->z.crew_count = 3;
    if (!Z_Attack(apc, &apc->info->attack, robot) || robot->hp != 485)
        return fail("three Grunt bullets independently hit infantry");
    apc->z.driver = MT_UNIT_SNIPER - MT_UNITS + 1;
    if (!Z_Attack(apc, &apc->info->attack, tank) || tank->hp != 485)
        return fail("vehicle armor is applied to each Sniper bullet");
    mobj_t *jeep = blast_unit(MT_UNIT_JEEP, 1, pixel_at(at, 32, 0), 500);
    if (!jeep) return fail("spawn APC driver-hit fixture");
    jeep->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    bool mixed = false;
    for (int i = 0; i < 128 && !mixed; ++i, ++leveltime) {
        jeep->hp = 500; jeep->z.crew_hurt = 0;
        if (!Z_Attack(apc, &apc->info->attack, jeep)) return fail("fire at APC driver-hit fixture");
        mixed = jeep->z.crew_hurt && jeep->hp < 500;
    }
    if (!mixed)
        return fail("APC bullets independently hit driver and armored body");
    bool stopped = false;
    for (int i = 0; i < 128 && !stopped; ++i, ++leveltime) {
        Z_SetTeam(jeep, 1);
        jeep->hp = 500; jeep->z.crew_hurt = 0;
        jeep->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
        jeep->z.crew_count = 1;
        jeep->z.crew[0] = (zcrew_t){.hp = 1, .type = MT_UNIT_GRUNT - MT_UNITS};
        if (!Z_Attack(apc, &apc->info->attack, jeep)) return fail("fire fatal APC driver salvo");
        stopped = jeep->team == Z_NEUTRAL && jeep->hp == 500;
    }
    if (!stopped) return fail("APC stops salvo after target driver is killed");
    robot->hp = 500;
    apc->z.driver = MT_UNIT_PYRO - MT_UNITS + 1;
    apc->attack.target = robot;
    P_UpdateSight();
    if (!P_Attack(apc) || robot->hp != 440)
        return fail("three eligible APC Pyros each hit");
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
        A_ZShellBlast(shell);
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
    P_ExplodeMissile(shell);
    if (target->hp != 450 || left->hp != 450 || right->hp != 450 || after->hp != 450 ||
        vehicle->hp != 450 || killed->hp > 0)
        return fail("shell hits primary and boundary units once, including after a kill");
    if (outx->hp != 500 || outy->hp != 500 || ally->hp != 500 || gun->hp != 500)
        return fail("shell excludes outside and friendly units");
    if (Z_Attack(ally, &ally->info->attack, target)) return fail("bullets do not use shell area damage");
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
    A_ZShellBlast(shell);
    if (building->hp != 450) return fail("shell hits friendly non-target building at inclusive origin");
    building->hp = 500;
    point.x--;
    shell->movement.goal = fixed2_scale(fixed2_from_cell(point), FIXED_ONE / Z_TILE);
    A_ZShellBlast(shell);
    if (building->hp != 500) return fail("shell misses just outside building footprint");
    point = ivec2_add(origin, (ivec2_t){building->info->footprint.w * Z_TILE - 4,
                                         building->info->footprint.h * Z_TILE + 8});
    shell->movement.goal = fixed2_scale(fixed2_from_cell(point), FIXED_ONE / Z_TILE);
    A_ZShellBlast(shell);
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
    A_ZShellBlast(shell);
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
    if (!shell || shell->missile.duration <= 1 || target->hp != 500) return fail("shell takes time to travel");
    uint32_t shell_id = shell->id, target_id = target->id;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!tick(model, 1) || target->hp != 500 ||
        !G_SaveGameBlob(&save, &size, "Shell flight", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save shell in flight");
    free(save);
    shell = P_MobjById(shell_id); target = P_MobjById(target_id);
    if (!shell || !target || !shell->target || shell->attack.target != target) return fail("load shell references");
    P_RemoveMobj(shell->target);
    if (!tick(model, RTS_TICRATE) || target->hp != 380) return fail("shell completes after its shooter is removed");
    puts("PASS: z shell travels and survives save and shooter removal");
    rts_game_model_destroy(model);
    return 0;
}

static int test_shell_aim(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for shell aim");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    disarm_except(0, NULL);
    fixed2_t start = fixed2_cell_center((ivec2_t){5, 5});
    mobj_t *gun = blast_unit(MT_UNIT_M_MISSILE, 0, start, 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(start, 96, 0), 500);
    mobj_t *first = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(start, 96, 0), 500);
    mobj_t *second = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(start, 160, 0), 500);
    if (!gun || !target || !first || !second) return fail("spawn shell aim fixtures");
    gun->traits &= ~MF_ATTACK; target->traits &= ~MF_ATTACK;
    first->traits &= ~MF_ATTACK; second->traits &= ~MF_ATTACK;
    if (!Z_Attack(gun, &gun->info->attack, target)) return fail("launch aimed shell");
    target->core.position = fixed3_from_fixed2(pixel_at(start, 160, 0), 0);
    if (!tick(model, RTS_TICRATE) || first->hp != 380 || target->hp != 500 || second->hp != 500)
        return fail("moving target does not redirect shell");
    if (!Z_Attack(gun, &gun->info->attack, target)) return fail("launch at removable target");
    P_RemoveMobj(target);
    if (!tick(model, RTS_TICRATE) || second->hp != 380) return fail("shell survives removal of its target");
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
    mobj_t *a = nth(MT_SHELL, 0, 0), *b = nth(MT_SHELL, 0, 1);
    /* 96 major-axis pixels, 4 pixels per 70 Hz step, at 30 engine tics/s. */
    if (!a || !b || a->missile.duration != 11 || b->missile.duration != 11)
        return fail("cardinal and diagonal shots use the same major-axis rate");
    if (!tick(model, 10) || cardinal->hp != 500 || diagonal->hp != 500)
        return fail("shells remain in flight before their final tick");
    if (!tick(model, 1) || cardinal->hp != 380 || diagonal->hp != 380)
        return fail("shells arrive on their scheduled tick");
    mobj_t *far = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(start, 1920, 0), 500);
    if (!far || !Z_Attack(gun, &gun->info->attack, far)) return fail("launch long-flight shell");
    mobj_t *long_shot = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->type_id == MT_SHELL && (mo->traits & MF_MISSILE)) long_shot = mo;
    }
    if (!long_shot || long_shot->missile.duration <= 100) return fail("long flight exceeds 100 tics");
    for (int i = 0; i < 100; ++i) P_MobjThinker(long_shot);
    if (long_shot->missile.age != 100 || !(long_shot->traits & MF_MISSILE))
        return fail("long flight takes exactly one step per engine tic");
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

int main(void) {
    if (test_bad_crew_combat() || test_apc_driver_cooldown() || test_apc_driver_range() || test_apc_firing_intervals() || test_apc_bullet_salvo() || test_apc_pyro_phase() || test_apc_shell_salvo() || test_death() || test_ordered_target() || test_alert() || test_pyro_cadence() || test_grenade_cooldown() || test_gun_drivers() || test_shell_blast() || test_shell_building_contact() || test_shell_grenade_box() || test_body_slowdown() ||
        test_shell_flight() || test_shell_aim() || test_shell_speed()) return 1;
    return 0;
}
