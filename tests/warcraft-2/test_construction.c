#include "t_local.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#include <stdlib.h>

#define CHECK(c) RTS_CHECK(c, "Warcraft II construction", #c)

/* An open 24x20 field of plain land, as the combat tests use. */
static void fixture(void) {
    P_FreeLevel(&level);
    P_InitThinkers();
    G_InitGame();
    consoleplayer = 0;
    level.width = 24;
    level.height = 20;
    int cells = level.width * level.height;
    level.tile_ids = calloc((size_t)cells, sizeof(*level.tile_ids));
    level.blocked = calloc((size_t)cells, 1);
    level.cell_solid = calloc((size_t)cells, 1);
    level.cell_terrain = calloc((size_t)cells, 1);
    level.resource_vents = calloc(8, sizeof(*level.resource_vents));
    level.speeds = calloc(1, sizeof(*level.speeds));
    level.speeds->class_count = 2;
    level.speeds->terrain[1][0] = 100;
    for (int owner = 0; owner < 8; ++owner) {
        level.player_resources[owner][0] = 1000;
        level.player_resources[owner][1] = 1000;
    }
}

static mobj_t *spawn(int type, int x, int y, int owner) {
    isize2_t foot = mobjinfo[type].w2.footprint;
    bool structure = (mobjinfo[type].w2.flags & W2_STRUCTURE) != 0;
    fixed2_t at = structure ? (fixed2_t){FIXED_FROM_INT(x) + foot.w * (FIXED_ONE / 2), FIXED_FROM_INT(y) + foot.h * (FIXED_ONE / 2)} : fixed2_cell_center((ivec2_t){x, y});
    mobj_t *unit = P_SpawnMobj(fixed3_from_fixed2(at, 0), (uint16_t)type);
    assert(unit);
    unit->owner = (uint8_t)owner;
    unit->team = (uint8_t)(owner < 8 ? owner : 8);
    unit->allegiance = owner == 0 ? ALLEGIANCE_PLAYER : owner >= 8 ? ALLEGIANCE_NEUTRAL : ALLEGIANCE_ENEMY;
    if (structure) w2_mark_footprint(x, y, foot);
    return unit;
}

static mobj_t *mine(int x, int y) {
    mobj_t *deposit = spawn(MT_GOLD_MINE, x, y, 15);
    level.resource_vents[level.resource_vent_count++] = (resourcevent_t){
        .cell = {x, y}, .attachment = {FIXED_FROM_INT(x) + FIXED_LIT(1.5), FIXED_FROM_INT(y) + FIXED_LIT(1.5)}, .footprint = {3, 3},
        .amount = 5000, .rate = 100, .active = true, .source_id = deposit->id,
    };
    return deposit;
}

static void tree(int x, int y) {
    int i = L_Index(&level, x, y);
    level.cell_terrain[i] = 2;
    level.blocked[i] = 1;
    level.resource_vents[level.resource_vent_count++] = (resourcevent_t){
        .cell = {x, y}, .attachment = fixed2_cell_center((ivec2_t){x, y}), .footprint = {1, 1},
        .amount = 100, .rate = 100, .active = true, .resource_type = 1,
    };
}

static void tick(int count) { while (count-- > 0) P_Ticker(); }

static mobj_t *find_type(int owner, int type) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *unit = (mobj_t *)th;
        if (th->function == P_MobjThinker && !unit->remove && unit->owner == owner && unit->type_id == type) return unit;
    }
    return NULL;
}

static mobj_t *find_id(uint32_t id) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *unit = (mobj_t *)th;
        if (th->function == P_MobjThinker && unit->id == id && !unit->remove) return unit;
    }
    return NULL;
}

static bool solid(int x, int y) { return level.cell_solid[L_Index(&level, x, y)] != 0; }

static bool hidden(const mobj_t *unit) {
    return P_MobjIsHidden(unit) && !(unit->traits & (MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE)) &&
        (unit->traits & MF_NOBLOCKMAP);
}

/* Walk the worker to its site and start the structure. */
static mobj_t *arrive(mobj_t *worker) {
    for (int i = 0; i < 900 && worker->w2.build_phase == W2_BUILD_TO_SITE; ++i) tick(1);
    return worker->w2.build_phase == W2_BUILD_WORKING ? P_MobjById(worker->w2.site) : NULL;
}

/* What a worker may put up, the tier rules, and that structures never go
 * through the training queue. */
static int test_catalog(void) {
    fixture();
    CHECK(W2_Buildable(MT_FARM) && W2_Buildable(MT_TOWN_HALL) && W2_Buildable(MT_HUMAN_SHIPYARD) &&
          W2_Buildable(MT_ORC_WATCH_TOWER) && W2_Buildable(MT_DRAGON_ROOST));
    CHECK(!W2_Buildable(MT_KEEP) && !W2_Buildable(MT_FORTRESS) && !W2_Buildable(MT_GOLD_MINE) &&
          !W2_Buildable(MT_HUMAN_WALL) && W2_Buildable(MT_HUMAN_OIL_PLATFORM) && !W2_Buildable(MT_FOOTMAN) &&
          !W2_Buildable(MT_DARK_PORTAL) && !W2_Buildable(MT_CIRCLE_OF_POWER) && !W2_Buildable(0));
    CHECK(W2_CountsAs(MT_KEEP, MT_TOWN_HALL) && W2_CountsAs(MT_CASTLE, MT_KEEP) && W2_CountsAs(MT_FORTRESS, MT_GREAT_HALL));
    CHECK(!W2_CountsAs(MT_TOWN_HALL, MT_KEEP) && !W2_CountsAs(MT_KEEP, MT_GREAT_HALL) && W2_CountsAs(MT_FARM, MT_FARM));
    const StaticProductDefinition *farm = G_ModelProductByUIId(NULL, W2_UI_FARM);
    const StaticProductDefinition *stables = G_ModelProductByUIId(NULL, W2_UI_STABLES);
    const StaticProductDefinition *roost = G_ModelProductByUIId(NULL, W2_UI_DRAGON_ROOST);
    CHECK(farm && farm->product_class == RTS_PRODUCT_BUILDING && farm->product_type == MT_FARM &&
          farm->cost == 500 && W2_ProductLumber(farm) == 250 && farm->maker_count == 1 && farm->makers[0] == MT_PEASANT);
    CHECK(farm->prerequisite_count == 0 && stables && stables->prerequisite_count == 1 && stables->prerequisites[0] == MT_KEEP);
    CHECK(roost && roost->makers[0] == MT_PEON && roost->prerequisites[0] == MT_FORTRESS);
    mobj_t *peasant = spawn(MT_PEASANT, 3, 3, 0);
    CHECK(G_ModelProductAvailable(NULL, 0, farm) && !G_ModelProductAvailable(NULL, 0, stables));
    CHECK(!G_ModelProducerHasTech(peasant, farm) && !G_FindProducer(0, farm) && !G_QueueProduct(peasant, farm));
    CHECK(!G_PlayerBuildProduct(peasant, farm) && level.player_resources[0][0] == 1000);
    spawn(MT_CASTLE, 10, 10, 0); /* A castle is a keep. */
    CHECK(G_ModelProductAvailable(NULL, 0, stables) && !G_ModelProductAvailable(NULL, 1, stables));
    /* Research still queues on its building, and the keep still transforms. */
    CHECK(G_ModelProducerHasTech(spawn(MT_HUMAN_BLACKSMITH, 16, 10, 0), G_ModelProductByUIId(NULL, W2_UI_SWORD1)));
    return 0;
}

/* Footprints need plain land, nothing standing on them but the builder,
 * coast inside a shipyard, and three clear cells between a hall and a mine. */
static int test_placement(void) {
    fixture();
    mobj_t *peasant = spawn(MT_PEASANT, 8, 8, 0);
    CHECK(W2_CanPlace(MT_FARM, (ivec2_t){5, 5}, NULL));
    CHECK(!W2_CanPlace(MT_FARM, (ivec2_t){23, 5}, NULL) && !W2_CanPlace(MT_FARM, (ivec2_t){-1, 5}, NULL));
    CHECK(!W2_CanPlace(MT_FARM, (ivec2_t){7, 7}, NULL) && W2_CanPlace(MT_FARM, (ivec2_t){7, 7}, peasant));
    CHECK(!W2_CanPlace(MT_KEEP, (ivec2_t){5, 5}, NULL));
    level.cell_terrain[L_Index(&level, 6, 5)] = 1; /* Water. */
    CHECK(!W2_CanPlace(MT_FARM, (ivec2_t){5, 5}, NULL) && W2_CanPlace(MT_FARM, (ivec2_t){4, 6}, NULL));
    CHECK(!W2_CanPlace(MT_HUMAN_SHIPYARD, (ivec2_t){12, 12}, NULL)); /* Needs the shore. */
    CHECK(!W2_CanPlace(MT_HUMAN_SHIPYARD, (ivec2_t){7, 4}, NULL));
    level.cell_terrain[L_Index(&level, 6, 5)] = 0;
    spawn(MT_FARM, 12, 12, 1);
    CHECK(!W2_CanPlace(MT_FARM, (ivec2_t){13, 13}, NULL) && W2_CanPlace(MT_FARM, (ivec2_t){14, 12}, NULL));
    mine(16, 2); /* Cells 16..18. Stratagus distance: touching footprints are one apart. */
    CHECK(!W2_CanPlace(MT_TOWN_HALL, (ivec2_t){12, 4}, NULL)); /* Touching: distance 1. */
    CHECK(!W2_CanPlace(MT_TOWN_HALL, (ivec2_t){10, 2}, NULL)); /* Two free cells: distance 3. */
    CHECK(W2_CanPlace(MT_TOWN_HALL, (ivec2_t){9, 2}, NULL));   /* Three free cells: distance 4. */
    CHECK(W2_CanPlace(MT_HUMAN_BARRACKS, (ivec2_t){12, 2}, NULL)); /* Only halls keep away. */
    return 0;
}

/* The builder walks over, pays on arrival, works inside while the site
 * grows through the three stages and its hit points, then steps out. */
static int test_build_cycle(void) {
    fixture();
    mobj_t *peasant = spawn(MT_PEASANT, 3, 3, 0);
    int *stock = level.player_resources[0];
    CHECK(!W2_ConstructOrder(peasant, MT_KEEP, (ivec2_t){8, 8}));
    stock[0] = 400;
    CHECK(!W2_ConstructOrder(peasant, MT_FARM, (ivec2_t){8, 8})); /* Cannot afford it. */
    stock[0] = 1000;
    CHECK(W2_ConstructOrder(peasant, MT_FARM, (ivec2_t){8, 8}));
    CHECK(peasant->w2.build_phase == W2_BUILD_TO_SITE && peasant->w2.build_type == MT_FARM && P_HasMoveOrder(peasant));
    CHECK(stock[0] == 1000 && stock[1] == 1000 && !find_type(0, MT_FARM)); /* Paid on arrival. */
    mobj_t *site = arrive(peasant);
    CHECK(site && site->type_id == MT_FARM && site->owner == 0 && W2_UnderConstruction(site));
    CHECK(stock[0] == 500 && stock[1] == 750);
    CHECK(site->hp == 1 && site->max_hp == 400 && W2_BuildProgress(site) == 0);
    CHECK(site->core.state_id == W2_BUILD_STATE(MT_FARM - 1) && states[site->core.state_id].group == W2_GROUP_BUILD);
    CHECK(site->core.sprite_id == W2_SPRITE_CONSTRUCTION && site->core.frame == 0 &&
          !strcmp(site->core.sprite_name, "construction-site"));
    CHECK(solid(8, 8) && solid(9, 9) && !solid(10, 10));
    CHECK(hidden(peasant) && peasant->w2.site == site->id && site->w2.builder == peasant->id);
    CHECK(!G_ModelHasActorType(NULL, 0, MT_FARM)); /* Not ready while it rises. */
    fixed2_t bay = fixed3_xy(peasant->core.position);
    CHECK(fixed_floor_int(bay.x) >= 7 && fixed_floor_int(bay.x) <= 11 &&
          fixed_floor_int(bay.y) >= 7 && fixed_floor_int(bay.y) <= 11); /* Beside the footprint. */
    int ticks = 0;
    while (ticks < 4000 && W2_BuildProgress(site) < 25) { tick(1); ++ticks; }
    CHECK(ticks == 150); /* Cost 100: 600 reference cycles (20 seconds). */
    CHECK(site->core.state_id == W2_BUILD_STATE(MT_FARM - 1) + 1 && site->core.frame == 1 &&
          site->core.sprite_id == W2_SPRITE_CONSTRUCTION);
    CHECK(site->hp >= 95 && site->hp <= 105);
    while (ticks < 4000 && W2_BuildProgress(site) < 50) { tick(1); ++ticks; }
    CHECK(site->core.state_id == W2_BUILD_STATE(MT_FARM - 1) + 2 && site->core.sprite_id == MT_FARM - 1 &&
          site->core.frame == 1 && !strcmp(site->core.sprite_name, "farm"));
    CHECK(hidden(peasant) && W2_UnderConstruction(site));
    while (ticks < 4000 && W2_UnderConstruction(site)) { tick(1); ++ticks; }
    CHECK(ticks == 600);
    CHECK(site->hp == 400 && site->core.state_id == mobjinfo[MT_FARM].spawnstate && site->w2.builder == 0);
    CHECK(G_ModelHasActorType(NULL, 0, MT_FARM));
    CHECK(!hidden(peasant) && (peasant->traits & (MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE)) &&
          peasant->w2.build_phase == W2_BUILD_NONE && peasant->w2.site == 0);
    CHECK(states[peasant->core.state_id].group == W2_GROUP_STAND);
    /* The finished farm is an ordinary structure: it dies to rubble. */
    site->hp = 1;
    P_DamageMobj(site, peasant, 1);
    CHECK(site->core.sprite_id == W2_SPRITE_RUBBLE);
    return 0;
}

/* Cancelling returns the whole price, frees the ground and the builder.
 * Orders through the tic command path, and new orders interrupt a walk. */
static int test_cancel_and_orders(void) {
    fixture();
    mobj_t *peasant = spawn(MT_PEASANT, 3, 3, 0);
    int *stock = level.player_resources[0];
    ticcmd_t build = {.order = TC_CONSTRUCT, .product = MT_HUMAN_BARRACKS, .count = 1, .units = {peasant->id},
                      .position = fixed3_from_fixed2(fixed2_cell_center((ivec2_t){8, 8}), 0)};
    G_RunTiccmd(0, &build);
    CHECK(peasant->w2.build_phase == W2_BUILD_TO_SITE && peasant->w2.build_type == MT_HUMAN_BARRACKS);
    ticcmd_t move = {.order = TC_MOVE, .count = 1, .units = {peasant->id},
                     .position = fixed3_from_fixed2(FIXED2_LIT(3.5, 5.5), 0)};
    G_RunTiccmd(0, &move);
    CHECK(peasant->w2.build_phase == W2_BUILD_NONE && peasant->w2.build_type == 0);
    tick(10);
    G_RunTiccmd(0, &build);
    mobj_t *site = arrive(peasant);
    CHECK(site && site->type_id == MT_HUMAN_BARRACKS && stock[0] == 300 && stock[1] == 550);
    CHECK(solid(8, 8) && solid(10, 10));
    uint32_t site_id = site->id;
    ticcmd_t cancel = {.order = TC_CONSTRUCT, .product = 0, .count = 1, .units = {site_id}};
    G_RunTiccmd(1, &cancel); /* Not its owner. */
    CHECK(find_id(site_id) == site && W2_UnderConstruction(site));
    G_RunTiccmd(0, &cancel);
    CHECK(!find_id(site_id) && stock[0] == 1000 && stock[1] == 1000);
    CHECK(!solid(8, 8) && !solid(10, 10));
    CHECK(!hidden(peasant) && peasant->w2.build_phase == W2_BUILD_NONE);
    CHECK(!W2_CancelConstruction(peasant)); /* Only sites cancel. */
    /* The spot is taken before the builder arrives: it gives up, unpaid. */
    CHECK(W2_ConstructOrder(peasant, MT_FARM, (ivec2_t){8, 8}));
    spawn(MT_FARM, 9, 9, 1);
    CHECK(!arrive(peasant) && peasant->w2.build_phase == W2_BUILD_NONE && stock[0] == 1000);
    return 0;
}

/* Razing a site frees its builder; the builder inside cannot be hit.
 * Siege damage must overcome the corrected 600-cycle farm construction. */
static int test_site_destroyed(void) {
    fixture();
    mobj_t *peasant = spawn(MT_PEASANT, 3, 3, 0);
    mobj_t *attackers[3];
    for (int i = 0; i < 3; ++i) attackers[i] = spawn(MT_CATAPULT, 13 + i, 14, 1);
    CHECK(W2_ConstructOrder(peasant, MT_FARM, (ivec2_t){8, 8}));
    mobj_t *site = arrive(peasant);
    CHECK(site && hidden(peasant));
    CHECK(!P_CanTarget(attackers[0], peasant) && P_CanTarget(attackers[0], site));
    uint32_t site_id = site->id;
    ticcmd_t attack = {.order = TC_ATTACK, .count = 3, .units = {attackers[0]->id, attackers[1]->id, attackers[2]->id},
                       .target = site_id, .position = site->core.position};
    G_RunTiccmd(1, &attack);
    for (int i = 0; i < 900 && P_MobjById(site_id); ++i) tick(1);
    CHECK(!P_MobjById(site_id) && find_id(site_id) == site && site->core.sprite_id == W2_SPRITE_RUBBLE);
    tick(2);
    CHECK(!hidden(peasant) && peasant->w2.build_phase == W2_BUILD_NONE && peasant->hp > 0);
    CHECK(!solid(8, 8) && !solid(9, 9));
    return 0;
}

/* The computer finds room by its hall and buys through the same orders. */
static int test_computer_player(void) {
    fixture();
    const AiGameInterface *ai = G_AiInterface();
    CHECK(ai && ai->features == AI_FEATURE_ALL && ai->is_busy && ai->assign_harvester &&
          g_ruleset.faction_count == 2 && g_ruleset.factions[1].opening_count > 0);
    CHECK(ai->player_level(&level, 0) == AI_LEVEL_NONE && ai->player_level(&level, 1) == AI_LEVEL_NORMAL &&
          ai->player_level(&level, 8) == AI_LEVEL_NONE);
    mobj_t *hall = spawn(MT_TOWN_HALL, 10, 8, 1);
    mine(2, 2);
    for (int x = 18; x < 22; ++x) tree(x, 16);
    CHECK(ai->can_purchase(&level, 1, W2_UI_FARM) == AI_BUY_BLOCKED); /* No worker yet. */
    mobj_t *peasant = spawn(MT_PEASANT, 15, 9, 1);
    CHECK(ai->owned(1, W2_UI_TOWN_HALL) == 1 && ai->owned(1, W2_UI_FARM) == 0 && ai->owned(1, 3) == 1);
    CHECK(ai->can_purchase(&level, 1, W2_UI_FARM) == AI_BUY_OK);
    CHECK(ai->can_purchase(&level, 1, W2_UI_STABLES) == AI_BUY_BLOCKED);
    CHECK(ai->can_purchase(&level, 1, W2_UI_PIG_FARM) == AI_BUY_BLOCKED); /* No peon to build it. */
    level.player_resources[1][1] = 100;
    CHECK(ai->can_purchase(&level, 1, W2_UI_FARM) == AI_BUY_NEED_CREDITS);
    level.player_resources[1][1] = 1000;
    ivec2_t cell;
    CHECK(W2_FindBuildSite(1, MT_FARM, &cell) && W2_CanPlace(MT_FARM, cell, NULL));
    CHECK(abs(cell.x - 10) <= 8 && abs(cell.y - 8) <= 8);
    /* A clear ring keeps the base passable. */
    for (int y = -1; y <= 2; ++y)
        for (int x = -1; x <= 2; ++x) CHECK(!solid(cell.x + x, cell.y + y));
    CHECK(W2_FindBuildSite(1, MT_TOWN_HALL, &cell) && W2_CanPlace(MT_TOWN_HALL, cell, NULL));
    CHECK(!W2_FindBuildSite(3, MT_FARM, &cell)); /* Nothing to build from. */
    CHECK(!ai->is_busy(peasant));
    CHECK(ai->purchase(&level, 1, W2_UI_FARM));
    CHECK(peasant->w2.build_phase == W2_BUILD_TO_SITE && ai->is_busy(peasant));
    CHECK(ai->owned(1, W2_UI_FARM) == 1); /* On the way counts. */
    mobj_t *site = arrive(peasant);
    CHECK(site && ai->owned(1, W2_UI_FARM) == 1 && ai->is_busy(peasant));
    CHECK(level.player_resources[1][0] == 500);
    /* Training and research go the usual way; a keep is a town hall. */
    CHECK(ai->can_purchase(&level, 1, 3) == AI_BUY_OK && ai->purchase(&level, 1, 3));
    CHECK(hall->production && ai->owned(1, 3) == 2);
    CHECK(W2_TransformUnit(hall, MT_KEEP) && ai->owned(1, W2_UI_TOWN_HALL) == 1 && ai->owned(1, W2_UI_KEEP) == 1);
    CHECK(ai->owned(1, W2_UI_SWORD1) == 0);
    W2_ApplyUpgrade(1, W2_UPGRADE_SWORD1);
    CHECK(ai->owned(1, W2_UI_SWORD1) == 1 && ai->owned(1, W2_UI_SWORD2) == 0);
    /* Workers go to gold first, then one in three to lumber. */
    mobj_t *workers[3];
    for (int i = 0; i < 3; ++i) workers[i] = spawn(MT_PEASANT, 14 + i, 12, 1);
    for (int i = 0; i < 3; ++i) CHECK(ai->assign_harvester(&level, 1, workers[i]));
    CHECK(workers[0]->harvest.resource_type == 0 && workers[1]->harvest.resource_type == 1 &&
          workers[2]->harvest.resource_type == 0);
    for (int i = 0; i < 3; ++i) CHECK(workers[i]->harvest.phase == HARVEST_PHASE_TO_MINE);
    AiPlan plan = {0};
    CHECK(ai->plan(&level, 1, AI_LEVEL_NORMAL, &plan) && plan.goal_count >= 15 && plan.wave_min_size == 6);
    CHECK(plan.goals[0].product == W2_UI_TOWN_HALL && plan.goals[1].product == 3);
    AiPlan orc = {0};
    spawn(MT_PEON, 2, 16, 2);
    CHECK(ai->plan(&level, 2, AI_LEVEL_NORMAL, &orc) && orc.goals[0].product == W2_UI_GREAT_HALL && orc.goals[1].product == 4);
    /* Orcs attack sooner and bolder; both rosters field their own side. */
    CHECK(orc.doctrine.attack_ratio < plan.doctrine.attack_ratio &&
          orc.doctrine.retreat_ratio < plan.doctrine.retreat_ratio && orc.wave_interval_ms < plan.wave_interval_ms);
    CHECK(plan.doctrine.roster[2].product == 1 && orc.doctrine.roster[2].product == 2);
    /* Roles come from the actors: farms feed, archers shoot up, footmen do not. */
    AiContext ctx;
    P_AiInit(&ctx);
    P_AiAttachGame(&ctx, ai);
    AiUnitInfo farm, archer, footman, worker;
    P_AiUnitInfo(&ctx, MT_FARM, &farm);
    P_AiUnitInfo(&ctx, MT_ARCHER, &archer);
    P_AiUnitInfo(&ctx, MT_FOOTMAN, &footman);
    P_AiUnitInfo(&ctx, MT_PEASANT, &worker);
    CHECK((farm.roles & AI_ROLE_SUPPLY) && !(farm.roles & AI_ROLE_FIGHTER));
    CHECK((archer.roles & (AI_ROLE_FIGHTER | AI_ROLE_HITS_AIR)) == (AI_ROLE_FIGHTER | AI_ROLE_HITS_AIR) &&
          archer.air_strength > 0);
    CHECK((footman.roles & AI_ROLE_HITS_GROUND) && !(footman.roles & AI_ROLE_HITS_AIR) &&
          footman.ground_strength > 0 && footman.air_strength == 0);
    CHECK((worker.roles & AI_ROLE_WORKER) && worker.ground_strength < footman.ground_strength);
    int used = 0, cap = 0;
    CHECK(ai->supply(1, &used, &cap) && used > 0);
    return 0;
}

int main(void) {
    RTS_RUN(test_catalog());
    RTS_RUN(test_placement());
    RTS_RUN(test_build_cycle());
    RTS_RUN(test_cancel_and_orders());
    RTS_RUN(test_site_destroyed());
    RTS_RUN(test_computer_player());
    P_FreeLevel(&level);
    puts("PASS: worker-built structures, placement rules, timed construction stages, cancel refunds, razed sites and the computer player's build orders");
    return 0;
}
