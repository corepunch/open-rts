#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c,"StarCraft units",#c)
/* Units that carry, launch, attach or merge, and every spell: Carrier
 * interceptors, Reaver scarabs, siege mode, add-ons, Bunkers, Archons,
 * research, and what the computer player does with them. */

static mobj_t *spawn(int type, fvec2_t at, int owner) {
    mobj_t *u = P_SpawnMobj(fixed3_from_fvec2(at, 0), (uint16_t)type);
    if (u) { u->owner = u->team = (uint8_t)owner; u->allegiance = owner == consoleplayer ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY; }
    return u;
}
static const StaticProductDefinition *product(int ui) { return G_ModelProductByUIId(NULL, ui); }
static void tick(int tics) {
    for (int t = 0; t < tics; t++) { P_Ticker(); G_ProductionTicker(FIXED_DT); }
}
/* Long enough for a product to finish (production counts whole milliseconds). */
static void finish(int ui) { tick(G_ModelProductTrainingTimeMs(product(ui)) * RTS_TICRATE / 1000 * 21 / 20 + 2); }
static void reset(void) {
    P_FreeThinkers();
    P_InitThinkers();
    memset(level.upgrades, 0, sizeof(level.upgrades));
    memset(level.cell_solid, 0, (size_t)level.width * level.height);
    level.player_resources[0][0] = level.player_resources[0][1] = 5000;
    level.player_resources[1][0] = level.player_resources[1][1] = 5000;
}
static void learn(int owner, int tech) { level.upgrades[SC_UPGRADES + tech][owner].weapon = 1; }
static int count_of(int type, uint32_t parent) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (th->function == P_MobjThinker && !mo->remove && mo->hp > 0 && mo->type_id == type &&
            (!parent || mo->sc.parent == parent)) ++n;
    }
    return n;
}

static int carrier(void) {
    mobj_t *c = spawn(MT_CARRIER, (fvec2_t){8.5f, 20.5f}, 0);
    CHECK(c && (c->traits & MF_ATTACK) && c->info->attack.range == 8);
    /* Interceptors are built at the Carrier and wait in its hangar. */
    CHECK(sc_hangar_capacity(c) == 4);
    CHECK(G_QueueProduct(c, product(MT_INTERCEPTOR)));
    finish(MT_INTERCEPTOR);
    CHECK(c->sc.hangar == 1 && count_of(MT_INTERCEPTOR, 0) == 0);
    c->sc.hangar = 4;
    CHECK(!G_ModelProducerHasTech(c, product(MT_INTERCEPTOR))); /* full */
    level.upgrades[43][0].weapon = 1; /* Carrier Capacity */
    CHECK(sc_hangar_capacity(c) == 8 && G_ModelProducerHasTech(c, product(MT_INTERCEPTOR)));
    /* In combat the Carrier launches them; they kill the target and come home. */
    mobj_t *target = spawn(MT_MARINE, (fvec2_t){14.5f, 20.5f}, 1);
    CHECK(target);
    c->attack.target = target;
    int launched = 0;
    for (int t = 0; t < RTS_TICRATE * 30 && target->hp > 0; t++) {
        tick(1);
        if (count_of(MT_INTERCEPTOR, c->id) > launched) launched = count_of(MT_INTERCEPTOR, c->id);
    }
    CHECK(launched == 4 && target->hp <= 0 && c->sc.hangar == 0);
    tick(RTS_TICRATE * 15);
    CHECK(count_of(MT_INTERCEPTOR, 0) == 0 && c->sc.hangar == 4);
    /* A Carrier's strength for the AI follows its hangar. */
    AiUnitInfo full, empty;
    P_AiUnitInfo(NULL, MT_CARRIER, &full);
    empty = full;
    G_AiInterface()->describe_unit(c, &full);
    c->sc.hangar = 0;
    G_AiInterface()->describe_unit(c, &empty);
    CHECK(full.air_strength > 0 && empty.air_strength == 0);
    reset();
    return 0;
}

static int reaver(void) {
    mobj_t *r = spawn(MT_REAVER, (fvec2_t){6.5f, 6.5f}, 0);
    mobj_t *a = spawn(MT_ULTRALISK, (fvec2_t){12.5f, 6.5f}, 1), *b = spawn(MT_ULTRALISK, (fvec2_t){13.9f, 6.5f}, 1);
    mobj_t *far = spawn(MT_ULTRALISK, (fvec2_t){12.5f, 10.5f}, 1);
    CHECK(r && a && b && far && sc_hangar_capacity(r) == 5);
    /* No scarab, no shot. */
    r->attack.target = a;
    tick(RTS_TICRATE * 3);
    CHECK(a->hp == a->max_hp && count_of(MT_SCARAB, 0) == 0);
    r->sc.hangar = 2;
    r->attack.target = a;
    bool flew = false;
    for (int t = 0; t < RTS_TICRATE * 5 && a->hp == a->max_hp; t++) { tick(1); flew |= count_of(MT_SCARAB, r->id) > 0; }
    /* Scarab: 100 normal damage less armor; enemy splash on the neighbour. */
    CHECK(flew && r->sc.hangar == 1);
    CHECK(a->max_hp - a->hp == 100 - sc_units[MT_ULTRALISK - 1].armor);
    CHECK(b->hp < b->max_hp && a->max_hp - a->hp > b->max_hp - b->hp && far->hp == far->max_hp);
    tick(2);
    CHECK(count_of(MT_SCARAB, 0) == 0);
    /* Scarab Damage adds 25. */
    level.upgrades[35][0].weapon = 1;
    int hp = a->hp;
    r->attack.target = a;
    for (int t = 0; t < RTS_TICRATE * 4 && a->hp == hp; t++) tick(1);
    CHECK(hp - a->hp == 125 - sc_units[MT_ULTRALISK - 1].armor && r->sc.hangar == 0);
    reset();
    return 0;
}

static int siege(void) {
    mobj_t *tank = spawn(MT_SIEGE_TANK, (fvec2_t){10.5f, 10.5f}, 0);
    CHECK(tank && (tank->traits & MF_MOBILE) && tank->info->attack.range == 7 && tank->info->attack.min_range == 0);
    /* Siege Tech first. */
    ticcmd_t deploy = {.order = TC_DEPLOY, .count = 1, .units = {tank->id}};
    G_RunTiccmd(0, &deploy);
    tick(RTS_TICRATE * 3);
    CHECK(tank->type_id == MT_SIEGE_TANK);
    learn(0, SC_TECH_SIEGE_MODE);
    G_RunTiccmd(0, &deploy);
    CHECK(!(tank->traits & MF_MOBILE) && tank->type_id == MT_SIEGE_TANK);
    tick(RTS_TICRATE * 3);
    const weapondef_t *w = &tank->info->attack;
    CHECK(tank->type_id == MT_SIEGE_MODE && !(tank->traits & MF_MOBILE));
    CHECK(w->range == 12 && w->min_range == 2 && w->splash == SPLASH_RADIAL);
    /* The dead zone: too near to shell, in reach further out. */
    mobj_t *near = spawn(MT_ZERGLING, (fvec2_t){11.5f, 10.5f}, 1), *mid = spawn(MT_ZERGLING, (fvec2_t){18.5f, 10.5f}, 1);
    CHECK(near && mid && !P_InAttackRange(tank, near) && P_InAttackRange(tank, mid));
    P_RemoveMobj(near);
    tick(RTS_TICRATE);
    CHECK(mid->hp < mid->max_hp);
    P_RemoveMobj(mid);
    /* And back: tank mode moves again with its own cannon. */
    G_RunTiccmd(0, &deploy);
    tick(RTS_TICRATE * 3);
    CHECK(tank->type_id == MT_SIEGE_TANK && (tank->traits & MF_MOBILE) && tank->info->attack.range == 7);
    reset();
    return 0;
}

/* A building put down as a map's would be: its cells solid. */
static mobj_t *building(int type, ivec2_t cell, int owner) {
    mobj_t *mo = spawn(type, P_BuildingPosition((uint16_t)type, cell), owner);
    P_SyncBuildingBlocking();
    return mo;
}

static int addons(void) {
    mobj_t *factory = building(MT_FACTORY, (ivec2_t){4, 4}, 0), *bare = building(MT_FACTORY, (ivec2_t){4, 12}, 0);
    CHECK(factory && bare);
    CHECK(building(MT_SUPPLY_DEPOT, (ivec2_t){30, 30}, 0)); /* room for a tank */
    /* Tanks need the Machine Shop on the Factory that makes them. */
    CHECK(!G_ModelProducerHasTech(factory, product(MT_SIEGE_TANK)));
    CHECK(G_ModelProducerHasTech(factory, product(MT_MACHINE_SHOP)));
    CHECK(!G_QueueProduct(factory, product(MT_CONTROL_TOWER))); /* a Starport's */
    ticcmd_t build = {.order = TC_BUILD, .count = 1, .units = {factory->id}, .product = MT_MACHINE_SHOP};
    G_RunTiccmd(0, &build);
    CHECK(factory->production && factory->production->product_type == MT_MACHINE_SHOP);
    finish(MT_MACHINE_SHOP);
    mobj_t *shop = sc_addon_of(factory);
    CHECK(shop && shop->type_id == MT_MACHINE_SHOP && shop->sc.parent == factory->id && !factory->production);
    /* Attached right of the Factory, level with its lower edge. */
    irect_t f = P_MobjCells(factory), s = P_MobjCells(shop);
    CHECK(s.x == f.x + f.w && s.y + s.h == f.y + f.h && s.w == 2 && s.h == 2);
    CHECK(level.cell_solid[L_Index(&level, s.x, s.y)] & 2);
    CHECK(G_ModelProducerHasTech(factory, product(MT_SIEGE_TANK)));
    CHECK(!G_ModelProducerHasTech(bare, product(MT_SIEGE_TANK)));
    CHECK(!G_ModelProducerHasTech(factory, product(MT_MACHINE_SHOP))); /* one each */
    /* The shop researches Siege Tech. */
    CHECK(!sc_has_tech(0, SC_TECH_SIEGE_MODE) && G_QueueProduct(shop, product(SC_TECH_UI + SC_TECH_SIEGE_MODE)));
    CHECK(!G_ModelProductAvailable(NULL, 0, product(SC_TECH_UI + SC_TECH_SIEGE_MODE))); /* already under way */
    finish(SC_TECH_UI + SC_TECH_SIEGE_MODE);
    CHECK(sc_has_tech(0, SC_TECH_SIEGE_MODE) && !G_ModelProductAvailable(NULL, 0, product(SC_TECH_UI + SC_TECH_SIEGE_MODE)));
    /* A blocked place holds the add-on until it clears. */
    mobj_t *in_way = building(MT_SUPPLY_DEPOT, (ivec2_t){8, 13}, 0);
    CHECK(in_way && !G_ModelProducerHasTech(bare, product(MT_MACHINE_SHOP)));
    /* The retail tree for the other add-ons and what they unlock. */
    const StaticProductDefinition *bc = product(MT_BATTLECRUISER), *ghost = product(MT_GHOST);
    CHECK(bc->prerequisite_count == 2 && bc->prerequisites[0] == MT_CONTROL_TOWER && bc->prerequisites[1] == MT_PHYSICS_LAB);
    CHECK(ghost->prerequisites[1] == MT_COVERT_OPS && product(MT_DROPSHIP)->prerequisites[0] == MT_CONTROL_TOWER);
    CHECK(product(MT_COMSAT_STATION)->makers[0] == MT_COMMAND_CENTER && product(MT_NUCLEAR_SILO)->makers[0] == MT_COMMAND_CENTER);
    CHECK(product(MT_COVERT_OPS)->makers[0] == MT_SCIENCE_FACILITY && product(MT_PHYSICS_LAB)->makers[0] == MT_SCIENCE_FACILITY);
    CHECK(product(SC_TECH_UI + SC_TECH_YAMATO_GUN)->makers[0] == MT_PHYSICS_LAB);
    CHECK(product(SC_TECH_UI + SC_TECH_LOCKDOWN)->makers[0] == MT_COVERT_OPS);
    CHECK(product(SC_TECH_UI + SC_TECH_CLOAKING_FIELD)->makers[0] == MT_CONTROL_TOWER);
    reset();
    return 0;
}

static bool shoot(mobj_t *attacker, mobj_t *target) {
    attacker->attack.target = target;
    attacker->attack.cooldown_left_ms = 0;
    return P_Attack(attacker);
}
static bool inside(const mobj_t *mo, irect_t r) {
    ivec2_t c = fvec2_cell(fixed3_xy_to_fvec2(mo->core.position));
    return c.x >= r.x && c.y >= r.y && c.x < r.x + r.w && c.y < r.y + r.h;
}

static int bunker(void) {
    mobj_t *b = building(MT_BUNKER, (ivec2_t){10, 10}, 0);
    mobj_t *m[5];
    for (int i = 0; i < 5; i++) m[i] = spawn(MT_MARINE, (fvec2_t){9.5f + i, 15.5f}, 0);
    mobj_t *scv = spawn(MT_SCV, (fvec2_t){9.5f, 17.5f}, 0), *zealot = spawn(MT_ZEALOT, (fvec2_t){10.5f, 17.5f}, 0);
    mobj_t *vulture = spawn(MT_VULTURE, (fvec2_t){11.5f, 17.5f}, 0), *foe = spawn(MT_MARINE, (fvec2_t){12.5f, 17.5f}, 1);
    CHECK(b && m[4] && scv && zealot && vulture && foe && sc_units[MT_BUNKER - 1].space_provided == 4);
    /* Terran infantry only, and only its owner's. */
    CHECK(sc_can_board(m[0], b) && !sc_can_board(scv, b) && !sc_can_board(zealot, b) &&
          !sc_can_board(vulture, b) && !sc_can_board(foe, b));
    /* Marines walk in on the board order, or on a right click on the Bunker. */
    ticcmd_t board = {.order = TC_BOARD, .count = 3, .units = {m[0]->id, m[1]->id, m[2]->id}, .target = b->id};
    G_RunTiccmd(0, &board);
    ticcmd_t click = {.order = TC_ORDER, .count = 1, .units = {m[3]->id}, .target = b->id,
                      .position = b->core.position};
    G_RunTiccmd(0, &click);
    tick(RTS_TICRATE * 4);
    for (int i = 0; i < 4; i++)
        CHECK((m[i]->sc.flags & SC_LOADED) && m[i]->sc.parent == b->id && P_MobjIsHidden(m[i]) &&
              !(m[i]->traits & (MF_SELECTABLE | MF_MOBILE)));
    CHECK(sc_cargo_space(b) == 4 && !sc_can_board(m[4], b));
    /* Fire from it with a cell more reach; out of the enemy's reach and splash. */
    mobj_t *ling = spawn(MT_ZERGLING, (fvec2_t){16.5f, 10.5f}, 1);
    CHECK(ling && sc_range_bonus(m[0], &m[0]->info->attack) == 1 && P_InAttackRange(m[0], ling));
    uint32_t ling_id = ling->id;
    CHECK(!P_CanTarget(ling, m[0]) && !P_CanTarget(foe, m[0]) && P_CanTarget(foe, b));
    mobj_t *tank = spawn(MT_SIEGE_MODE, (fvec2_t){11.5f, 21.5f}, 1);
    CHECK(tank && shoot(tank, b) && b->hp < b->max_hp && m[0]->hp == m[0]->max_hp);
    P_RemoveMobj(tank);
    P_RemoveMobj(foe);
    tick(RTS_TICRATE);
    CHECK(!P_MobjById(ling_id)); /* shot dead from inside */
    /* The AI counts a Bunker as the infantry it holds. */
    AiUnitInfo marine, held;
    P_AiUnitInfo(NULL, MT_MARINE, &marine);
    P_AiUnitInfo(NULL, MT_BUNKER, &held);
    G_AiInterface()->describe(MT_BUNKER, &held);
    CHECK(held.roles & AI_ROLE_DEFENSE);
    G_AiInterface()->describe_unit(b, &held);
    CHECK(held.ground_strength == 4 * marine.ground_strength && held.air_strength == 4 * marine.air_strength);
    /* When the Bunker falls they step out of it. */
    irect_t cells = P_MobjCells(b);
    P_DamageMobj(b, NULL, b->hp);
    tick(2);
    for (int i = 0; i < 4; i++)
        CHECK(!(m[i]->sc.flags & SC_LOADED) && m[i]->hp > 0 && (m[i]->traits & MF_SELECTABLE) &&
              (m[i]->traits & MF_MOBILE) && !P_MobjIsHidden(m[i]) && !inside(m[i], cells));
    /* Unload All empties one on order. */
    mobj_t *b2 = building(MT_BUNKER, (ivec2_t){20, 20}, 0);
    mobj_t *a = spawn(MT_GHOST, (fvec2_t){21.5f, 22.6f}, 0), *f = spawn(MT_FIREBAT, (fvec2_t){20.5f, 22.6f}, 0);
    CHECK(b2 && a && f && sc_board(a, b2) && sc_board(f, b2) && (a->sc.flags & f->sc.flags & SC_LOADED));
    ticcmd_t unload = {.order = TC_UNLOAD, .count = 1, .units = {b2->id}};
    G_RunTiccmd(0, &unload);
    CHECK(!(a->sc.flags & SC_LOADED) && !(f->sc.flags & SC_LOADED) && sc_cargo_space(b2) == 0 &&
          !inside(a, P_MobjCells(b2)) && !inside(f, P_MobjCells(b2)));
    reset();
    return 0;
}

static int archon(void) {
    mobj_t *a = spawn(MT_HIGH_TEMPLAR, (fvec2_t){5.5f, 30.5f}, 0), *b = spawn(MT_HIGH_TEMPLAR, (fvec2_t){8.5f, 30.5f}, 0);
    CHECK(a && b);
    ticcmd_t merge = {.order = TC_SPELL, .count = 2, .units = {a->id, b->id}, .product = SC_TECH_ARCHON_WARP};
    G_RunTiccmd(0, &merge);
    tick(RTS_TICRATE * 3);
    CHECK(count_of(MT_HIGH_TEMPLAR, 0) == 2 && count_of(MT_ARCHON, 0) == 0); /* still warping */
    tick(sc_units[MT_ARCHON - 1].build_time * RTS_TICRATE / 24);
    CHECK(count_of(MT_HIGH_TEMPLAR, 0) == 0 && count_of(MT_ARCHON, 0) == 1);
    mobj_t *archon = a->remove ? b : a;
    CHECK(archon->type_id == MT_ARCHON && archon->hp == archon->max_hp && sc_shields(archon) == 350 &&
          (archon->traits & MF_ATTACK));
    reset();
    return 0;
}

int main(void) {
    G_InitGame();
    P_InitThinkers();
    consoleplayer = 0;
    level.width = level.height = 48;
    level.blocked = calloc(48 * 48, 1);
    level.cell_solid = calloc(48 * 48, 1);
    CHECK(level.blocked && level.cell_solid);
    reset();
    CHECK(!carrier());
    CHECK(!reaver());
    CHECK(!siege());
    CHECK(!addons());
    CHECK(!bunker());
    CHECK(!archon());
    P_FreeLevel(&level);
    puts("PASS: interceptors, scarabs, siege mode, add-ons, Bunkers, Archons");
    return 0;
}
