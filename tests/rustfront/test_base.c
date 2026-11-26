#include "t_local.h"
#include "engine.h"
#include "../../games/rustfront/info.h"
#include "../../games/rustfront/rustfront.h"

#include <stdlib.h>
#include <string.h>

#define CHECK(c) RTS_CHECK(c, "rustfront base", #c)

/* Blue (owner 0, the local player) top left, Red bottom right (maps/skirmish.txt). */

static mobj_t *find(int owner, uint16_t type) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (th->function == P_MobjThinker && u->owner == owner && u->type_id == type && !u->remove && u->hp > 0)
            return u;
    }
    return NULL;
}

static int count_owned(int owner, uint16_t type) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        n += th->function == P_MobjThinker && u->owner == owner && u->type_id == type && !u->remove && u->hp > 0;
    }
    return n;
}

static RtsGameModel *load(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = g_game_default_root};
    if (!rts_game_model_load(model, &config)) return NULL;
    /* Keep the computer still: these tests are about the rules. */
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    return model;
}

static void run(RtsGameModel *model, int tics) {
    for (int t = 0; t < tics; ++t) rts_game_model_tick(model, RTS_TICK_MS);
}

static void order(int player, ticorder_t what, int product, const mobj_t *unit, fixed2_t at) {
    ticcmd_t cmd = {.order = what, .product = product, .count = 1, .position = fixed3_from_fixed2(at, 0)};
    cmd.units[0] = unit->id;
    G_RunTiccmd(player, &cmd);
}

/* Unexplored ground is black, Blue's own base is lit; a scout driven to Red's base and back
 * leaves Red's structures remembered in the fog, and the radar marks only what Blue knows. */
static int test_fog(void) {
    RtsGameModel *model = load();
    CHECK(model);
    CHECK(R_Policy()->sight == SIGHT_RADIAL && R_Policy()->memory == MEMORY_STRUCTURES);
    P_UpdateSight();
    mobj_t *red_hq = find(1, MT_HQ), *scout = find(0, MT_SCOUT);
    CHECK(red_hq && scout);
    ivec2_t red_cell = fixed2_cell(fixed3_xy(red_hq->core.position));
    CHECK(P_SightBrightness(&level, red_cell) == 0);           /* unexplored */
    CHECK(P_SightBrightness(&level, (ivec2_t){3, 4}) == 16);   /* Blue's HQ */
    CHECK(!P_VisibleToPlayer(red_hq));
    const sightmemory_t *memory;
    CHECK(P_SightMemory(&memory) == 0);
    /* The scout looks in (placed: its drive is the pathfinder's business, not the fog's). */
    scout->hp = scout->max_hp = 100000;
    scout->core.position = fixed3_from_fixed2(FIXED2_LIT(36.5, 22.5), 0);
    P_UpdateSight();
    bool seen = P_VisibleToPlayer(red_hq);
    CHECK(seen);
    run(model, 8);
    int remembered = P_SightMemory(&memory);
    /* And out again: the base stays as last seen, dimmed. */
    scout->core.position = fixed3_from_fixed2(FIXED2_LIT(12.5, 12.5), 0);
    run(model, 8);
    CHECK(P_SightBrightness(&level, red_cell) == 10);          /* explored, out of sight */
    CHECK(!P_VisibleToPlayer(red_hq));
    remembered = P_SightMemory(&memory);
    bool hq_remembered = false;
    for (int i = 0; i < remembered; ++i) {
        CHECK(memory[i].owner == 1);
        hq_remembered |= memory[i].id == red_hq->id && memory[i].type_id == MT_HQ;
    }
    CHECK(hq_remembered);
    /* Red's structures are only pictures to Blue now: destroying one unseen leaves its memory. */
    mobj_t *red_power = find(1, MT_POWER);
    CHECK(red_power);
    uint32_t power_id = red_power->id;
    bool power_seen = false;
    for (int i = 0; i < remembered; ++i) power_seen |= memory[i].id == power_id;
    P_DamageMobj(red_power, NULL, 100000);
    run(model, 8);
    remembered = P_SightMemory(&memory);
    bool power_kept = false;
    for (int i = 0; i < remembered; ++i) power_kept |= memory[i].id == power_id;
    CHECK(power_kept == power_seen);
    rts_game_model_destroy(model);
    puts("PASS: fog of war: black shroud, dimmed explored ground, structures remembered as last seen");
    return 0;
}

/* Power: Blue makes 130 and draws 120; another War Factory puts it on low power, which halves
 * production speed and turret fire rate; selling it restores full power and pays half back. */
static int test_power_and_sell(void) {
    RtsGameModel *model = load();
    CHECK(model);
    int made, used;
    RF_OwnerPower(0, &made, &used);
    CHECK(made == 130 && used == 120 && !RF_LowPower(0));
    mobj_t *barracks = find(0, MT_BARRACKS), *turret = find(0, MT_GUN_TURRET);
    CHECK(barracks && turret);
    const StaticProductDefinition *rifleman = G_ModelProductByUIId(NULL, MT_RIFLEMAN);
    CHECK(rifleman);
    int full = G_WeaponCooldown(turret, &turret->info->attack, 1200);
    /* Time one rifleman at full power. */
    int before = count_owned(0, MT_RIFLEMAN), tics_full = 0;
    CHECK(G_QueueProduct(barracks, rifleman));
    while (count_owned(0, MT_RIFLEMAN) == before && tics_full < 60 * RTS_TICRATE) { run(model, 1); tics_full++; }
    CHECK(count_owned(0, MT_RIFLEMAN) == before + 1);
    /* A second factory: 160 drawn against 130. */
    mobj_t *extra = P_SpawnMobj(fixed3_from_fixed2(P_BuildingPosition(MT_FACTORY, (ivec2_t){14, 12}), 0), MT_FACTORY);
    CHECK(extra);
    extra->owner = extra->team = 0;
    CHECK(RF_LowPower(0));
    CHECK(G_WeaponCooldown(turret, &turret->info->attack, 1200) == 2 * full);
    before = count_owned(0, MT_RIFLEMAN);
    int tics_low = 0;
    CHECK(G_QueueProduct(barracks, rifleman));
    while (count_owned(0, MT_RIFLEMAN) == before && tics_low < 60 * RTS_TICRATE) { run(model, 1); tics_low++; }
    CHECK(count_owned(0, MT_RIFLEMAN) == before + 1);
    printf("rifleman: %d tics on full power, %d on low\n", tics_full, tics_low);
    CHECK(tics_low >= tics_full * 18 / 10 && tics_low <= tics_full * 22 / 10);
    /* Sell it at three-quarters health: half of 2000 x 0.75 back. */
    extra->hp = extra->max_hp * 3 / 4;
    int credits = level.player_resources[0][0];
    CHECK(RF_SellValue(extra) == 750);
    order(0, TC_REPAIR, RF_ORDER_SELL, extra, (fixed2_t){0});
    CHECK(extra->remove);
    CHECK(level.player_resources[0][0] == credits + 750);
    CHECK(!RF_LowPower(0));
    rts_game_model_destroy(model);
    puts("PASS: low power halves production and defences; selling pays back half and frees power");
    return 0;
}

/* Repair mends a structure at a cost and stops when it is whole; a second order stops it. */
static int test_repair(void) {
    RtsGameModel *model = load();
    CHECK(model);
    mobj_t *factory = find(0, MT_FACTORY), *harvester;
    CHECK(factory);
    while ((harvester = find(0, MT_HARVESTER))) P_RemoveMobj(harvester); /* no income meanwhile */
    factory->hp = factory->max_hp / 2;
    int credits = level.player_resources[0][0];
    order(0, TC_REPAIR, RF_ORDER_REPAIR, factory, (fixed2_t){0});
    CHECK(factory->rf_repairing);
    run(model, 2 * RTS_TICRATE);
    CHECK(factory->hp > factory->max_hp / 2 && level.player_resources[0][0] < credits);
    order(0, TC_REPAIR, RF_ORDER_REPAIR, factory, (fixed2_t){0}); /* stop */
    CHECK(!factory->rf_repairing);
    int hp = factory->hp;
    run(model, RTS_TICRATE);
    CHECK(factory->hp == hp);
    order(0, TC_REPAIR, RF_ORDER_REPAIR, factory, (fixed2_t){0});
    run(model, 30 * RTS_TICRATE);
    CHECK(factory->hp == factory->max_hp && !factory->rf_repairing);
    /* Half of the half that was mended: 2000 x 0.5 / 2 = 500, give or take rounding a step. */
    int spent = credits - level.player_resources[0][0];
    printf("repairing half a War Factory cost %d\n", spent);
    CHECK(spent >= 480 && spent <= 540);
    rts_game_model_destroy(model);
    puts("PASS: repair mends a structure at half its price per hit point and can be stopped");
    return 0;
}

/* A move order to a barracks sets its rally point; what it trains goes there. */
static int test_rally(void) {
    RtsGameModel *model = load();
    CHECK(model);
    mobj_t *barracks = find(0, MT_BARRACKS);
    CHECK(barracks);
    fixed2_t flag = FIXED2_LIT(14.5, 16.5);
    order(0, TC_ORDER, 0, barracks, flag);
    CHECK(RF_HasRally(barracks) && fixed2_near(RF_Rally(barracks), flag, FIXED_ONE / 64));
    int before = count_owned(0, MT_RIFLEMAN);
    CHECK(G_QueueProduct(barracks, G_ModelProductByUIId(NULL, MT_RIFLEMAN)));
    run(model, 30 * RTS_TICRATE);
    CHECK(count_owned(0, MT_RIFLEMAN) == before + 1);
    mobj_t *fresh = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (th->function == P_MobjThinker && u->owner == 0 && u->type_id == MT_RIFLEMAN && u->hp > 0 &&
            fixed2_distance_squared64(fixed3_xy(u->core.position), flag) < FIXED_LIT(2.0) * (int64_t)FIXED_LIT(2.0))
            fresh = u;
    }
    CHECK(fresh);
    rts_game_model_destroy(model);
    puts("PASS: a producer's rally point sends its new units there");
    return 0;
}

/* Kills raise a unit's rank; a rank adds a fifth of the damage per step. */
static int test_veterancy(void) {
    RtsGameModel *model = load();
    CHECK(model);
    mobj_t *tank = find(0, MT_TANK);
    CHECK(tank && RF_Rank(tank) == 0 && RF_RankDamage(tank, 100) == 100);
    for (int k = 0; k < RF_ELITE_KILLS; ++k) {
        mobj_t *victim = P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(20.5, 15.5), 0), MT_RIFLEMAN);
        CHECK(victim);
        victim->owner = victim->team = 1;
        P_DamageMobj(victim, tank, 1000);
        if (k + 1 == RF_VETERAN_KILLS) CHECK(RF_Rank(tank) == 1 && RF_RankDamage(tank, 100) == 120);
    }
    CHECK(tank->rf_kills == RF_ELITE_KILLS && RF_Rank(tank) == 2 && RF_RankDamage(tank, 100) == 140);
    /* Own losses do not count. */
    mobj_t *own = P_SpawnMobj(fixed3_from_fixed2(FIXED2_LIT(20.5, 15.5), 0), MT_RIFLEMAN);
    own->owner = own->team = 0;
    P_DamageMobj(own, tank, 1000);
    CHECK(tank->rf_kills == RF_ELITE_KILLS);
    /* An elite mends itself. */
    tank->hp = tank->max_hp / 2;
    run(model, 5 * RTS_TICRATE);
    CHECK(tank->hp > tank->max_hp / 2);
    rts_game_model_destroy(model);
    puts("PASS: kills make veterans and elites, who hit harder and mend");
    return 0;
}

static mobj_t *spawn(uint16_t type, int owner, fixed2_t at) {
    const mobjtype_t *info = P_ActorType(type);
    if (info && info->footprint.w > 0 && !(info->traits & MF_MOBILE))
        at = P_BuildingPosition(type, fixed2_cell(at));
    mobj_t *u = P_SpawnMobj(fixed3_from_fixed2(at, 0), type);
    if (u) { u->owner = u->team = (uint8_t)owner; u->allegiance = owner ? ALLEGIANCE_ENEMY : ALLEGIANCE_PLAYER; }
    return u;
}

static int count_type(uint16_t type) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        n += th->function == P_MobjThinker && ((mobj_t *)th)->type_id == type && !((mobj_t *)th)->remove;
    return n;
}

/* Each faction's top tier: a tech structure from the headquarters, after the factory, unlocks
 * one unit. The Hammer fires a pair of splashing missiles and cannot fire point-blank; a
 * Wrecker goes up with its target and the enemies round it, and also when shot; an Aegis's
 * field takes a third off hits on its side and mends their vehicles. */
static int test_top_tier(void) {
    RtsGameModel *model = load();
    CHECK(model);
    struct { uint16_t tech, unit, maker, hq, factory; } tiers[] = {
        {MT_UPLINK, MT_HAMMER, MT_FACTORY, MT_HQ, MT_FACTORY},
        {MT_CL_SHOP, MT_CL_WRECKER, MT_CL_YARD, MT_CL_HALL, MT_CL_YARD},
        {MT_LC_SANCTUM, MT_LC_AEGIS, MT_LC_FOUNDRY, MT_LC_SPIRE, MT_LC_FOUNDRY},
    };
    for (int f = 0; f < RF_FACTION_COUNT; ++f) {
        const StaticProductDefinition *tech = G_ModelProductByUIId(NULL, tiers[f].tech);
        const StaticProductDefinition *unit = G_ModelProductByUIId(NULL, tiers[f].unit);
        CHECK(tech && unit && tech->icon_frame >= 0 && unit->icon_frame >= 0);
        CHECK(tech->makers[0] == tiers[f].hq && tech->prerequisites[0] == tiers[f].factory);
        CHECK(unit->makers[0] == tiers[f].maker && unit->prerequisites[0] == tiers[f].tech);
        CHECK(RF_FactionOfType(tiers[f].tech) == f && RF_FactionOfType(tiers[f].unit) == f);
        CHECK(RF_PowerOf(tiers[f].tech) < 0 && RF_SightCells(tiers[f].unit) > 0);
    }
    /* Blue has a War Factory; the Hammer waits for an Uplink. */
    CHECK(!G_ModelProductAvailable(NULL, 0, G_ModelProductByUIId(NULL, MT_HAMMER)));
    mobj_t *uplink = spawn(MT_UPLINK, 0, FIXED2_LIT(14.0, 12.0));
    CHECK(uplink && G_ModelProductAvailable(NULL, 0, G_ModelProductByUIId(NULL, MT_HAMMER)));

    /* Hammer: two heavy missiles; nothing inside its minimum range. */
    mobj_t *hammer = spawn(MT_HAMMER, 0, FIXED2_LIT(20.5, 15.5));
    mobj_t *mark = spawn(MT_MASTODON, 1, FIXED2_LIT(28.5, 15.5));
    CHECK(hammer && mark && hammer->info->attack.min_range > 0);
    int missiles = count_type(MT_HEAVY_MISSILE);
    CHECK(RF_Attack(hammer, &hammer->info->attack, mark));
    CHECK(count_type(MT_HEAVY_MISSILE) == missiles + 2);
    P_RemoveMobj(mark);
    P_RemoveMobj(hammer);

    /* Wrecker: into a Red turret with a rifleman beside it. */
    mobj_t *turret = spawn(MT_GUN_TURRET, 1, FIXED2_LIT(24.5, 20.5));
    mobj_t *guard = spawn(MT_TANK, 1, FIXED2_LIT(25.5, 20.5));
    mobj_t *wrecker = spawn(MT_CL_WRECKER, 0, FIXED2_LIT(23.5, 20.5));
    CHECK(turret && guard && wrecker);
    int turret_hp = turret->hp, guard_hp = guard->hp;
    CHECK(RF_Attack(wrecker, &wrecker->info->attack, turret));
    CHECK(wrecker->hp <= 0);
    CHECK(turret_hp - turret->hp == wrecker->info->attack.damage || turret->hp <= 0);
    CHECK(guard_hp - guard->hp == wrecker->info->attack.damage / 2);
    /* Shot on the way, it still goes up. */
    wrecker = spawn(MT_CL_WRECKER, 0, FIXED2_LIT(26.5, 20.5));
    guard_hp = guard->hp;
    P_DamageMobj(wrecker, guard, 100000);
    CHECK(guard->hp < guard_hp);

    /* Aegis: a third off, and mending. */
    mobj_t *glaive = spawn(MT_LC_GLAIVE, 1, FIXED2_LIT(10.5, 25.5));
    CHECK(glaive && RF_ShieldDamage(glaive, 90) == 90);
    mobj_t *aegis = spawn(MT_LC_AEGIS, 1, FIXED2_LIT(12.5, 25.5));
    CHECK(aegis && RF_ShieldDamage(glaive, 90) == 60 && RF_ShieldDamage(aegis, 90) == 60);
    CHECK(RF_ShieldDamage(find(0, MT_TANK), 90) == 90); /* not Blue's */
    glaive->hp = glaive->max_hp / 2;
    run(model, 3 * RTS_TICRATE);
    CHECK(glaive->hp >= glaive->max_hp / 2 + 24);
    rts_game_model_destroy(model);
    puts("PASS: the Uplink's Hammer, the Chop Shop's Wrecker and the Sanctum's Aegis");
    return 0;
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    RTS_RUN(test_fog());
    RTS_RUN(test_power_and_sell());
    RTS_RUN(test_repair());
    RTS_RUN(test_rally());
    RTS_RUN(test_veterancy());
    RTS_RUN(test_top_tier());
    SDL_Quit();
    return 0;
}
