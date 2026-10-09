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
static mobj_t *first_of(int type) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && !mo->remove && mo->hp > 0 && mo->type_id == type) return mo;
    }
    return NULL;
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
    mobj_t *archon = first_of(MT_ARCHON);
    CHECK(archon && archon->hp == archon->max_hp && sc_shields(archon) == 350 && (archon->traits & MF_ATTACK));
    reset();
    return 0;
}

/* A caster with full energy. */
static mobj_t *caster(int type, fvec2_t at) {
    mobj_t *mo = spawn(type, at, 0);
    sc_start(mo);
    mo->sc.energy = 200 << 8;
    return mo;
}
static ticcmd_t spell(mobj_t *by, int tech, const mobj_t *target, fvec2_t at) {
    return (ticcmd_t){.order = TC_SPELL, .count = 1, .units = {by->id}, .product = tech,
                      .target = target ? target->id : 0, .position = fixed3_from_fvec2(at, 0)};
}
static int frames(int n) { return n * RTS_TICRATE / 24 + 2; }
static fvec2_t at_of(const mobj_t *mo) { return fixed3_xy_to_fvec2(mo->core.position); }

static int protoss_spells(void) {
    /* Psionic Storm: researched first, 75 energy, 112 over 2.6 s ignoring
     * armor, storms do not stack, buildings stand. */
    mobj_t *ht = caster(MT_HIGH_TEMPLAR, (fvec2_t){4.5f, 4.5f}), *ht2 = caster(MT_HIGH_TEMPLAR, (fvec2_t){4.5f, 6.5f});
    mobj_t *ultra = spawn(MT_ULTRALISK, (fvec2_t){10.5f, 4.5f}, 1), *ling = spawn(MT_ZERGLING, (fvec2_t){10.9f, 5.0f}, 1);
    mobj_t *pool = building(MT_SPAWNING_POOL, (ivec2_t){11, 2}, 1);
    CHECK(ht && ht2 && ultra && ling && pool);
    ultra->traits &= ~MF_MOBILE; /* rooted in the storm for the count */
    ling->traits &= ~MF_MOBILE;
    uint32_t ling_id = ling->id;
    CHECK(!sc_cast(ht, SC_TECH_PSIONIC_STORM, NULL, at_of(ultra)));
    learn(0, SC_TECH_PSIONIC_STORM);
    ticcmd_t cmd = spell(ht, SC_TECH_PSIONIC_STORM, NULL, at_of(ultra));
    G_RunTiccmd(0, &cmd);
    CHECK(sc_energy(ht) == 125 && count_of(MT_MAP_REVEALER, 0) == 1);
    CHECK(sc_cast(ht2, SC_TECH_PSIONIC_STORM, NULL, at_of(ultra)));
    tick(frames(70));
    CHECK(ultra->max_hp - ultra->hp == 112 && !P_MobjById(ling_id) && pool->hp == pool->max_hp);
    CHECK(count_of(MT_MAP_REVEALER, 0) == 0);
    /* A cast from afar walks into range first. */
    ultra->hp = ultra->max_hp;
    ht->sc.energy = 200 << 8;
    ht->core.position = fixed3_from_fvec2((fvec2_t){4.5f, 30.5f}, 0);
    CHECK(sc_cast(ht, SC_TECH_PSIONIC_STORM, NULL, at_of(ultra)) && sc_energy(ht) == 200 && P_HasMoveOrder(ht));
    tick(RTS_TICRATE * 12);
    CHECK(sc_energy(ht) < 200 && ultra->hp < ultra->max_hp && ht->sc.order.kind == SC_ORDER_NONE);
    reset();

    /* Hallucination: two copies that deal nothing and take double damage. */
    learn(0, SC_TECH_HALLUCINATION);
    ht = caster(MT_HIGH_TEMPLAR, (fvec2_t){4.5f, 4.5f});
    mobj_t *zealot = spawn(MT_ZEALOT, (fvec2_t){7.5f, 4.5f}, 0), *marine = spawn(MT_MARINE, (fvec2_t){8.5f, 5.8f}, 1);
    CHECK(sc_cast(ht, SC_TECH_HALLUCINATION, zealot, at_of(zealot)) && count_of(MT_ZEALOT, 0) == 3);
    int supply, cap;
    sc_supply_counts(0, &supply, &cap);
    CHECK(supply == sc_units[MT_ZEALOT - 1].supply_required + sc_units[MT_HIGH_TEMPLAR - 1].supply_required);
    mobj_t *copy = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->type_id == MT_ZEALOT && (mo->sc.flags & SC_HALLUCINATION)) copy = mo;
    }
    CHECK(copy && copy->owner == 0 && shoot(copy, marine) && marine->hp == marine->max_hp);
    CHECK(shoot(marine, copy) && shoot(marine, zealot) && sc_shields(copy) == 80 - 12 && sc_shields(zealot) == 80 - 6);
    tick(frames(1350));
    CHECK(count_of(MT_ZEALOT, 0) == 1);
    reset();
    return 0;
}

static int terran_spells(void) {
    /* Defensive Matrix (no research): absorbs 250, then hits land. */
    mobj_t *vessel = caster(MT_SCIENCE_VESSEL, (fvec2_t){4.5f, 4.5f}), *marine = spawn(MT_MARINE, (fvec2_t){6.5f, 4.5f}, 0);
    mobj_t *hydra = spawn(MT_HYDRALISK, (fvec2_t){9.5f, 4.5f}, 1);
    CHECK(sc_cast(vessel, SC_TECH_DEFENSIVE_MATRIX, marine, at_of(marine)) && sc_energy(vessel) == 100);
    for (int i = 0; i < 25; i++) CHECK(shoot(hydra, marine));
    CHECK(marine->hp == marine->max_hp && marine->sc.matrix == 0);
    CHECK(shoot(hydra, marine) && marine->hp < marine->max_hp);
    CHECK(sc_cast(vessel, SC_TECH_DEFENSIVE_MATRIX, marine, at_of(marine)));
    tick(frames(168 * 8));
    CHECK(marine->sc.matrix == 0 && !marine->sc.timers[SC_TIMER_MATRIX]);
    reset();

    /* EMP Shockwave: shields and energy in the area are gone, the Vessel's kept. */
    learn(0, SC_TECH_EMP);
    vessel = caster(MT_SCIENCE_VESSEL, (fvec2_t){4.5f, 4.5f});
    mobj_t *z1 = spawn(MT_ZEALOT, (fvec2_t){10.5f, 4.5f}, 1), *z2 = spawn(MT_ZEALOT, (fvec2_t){11.3f, 4.5f}, 1);
    mobj_t *templar = spawn(MT_HIGH_TEMPLAR, (fvec2_t){10.5f, 5.3f}, 1), *far = spawn(MT_ZEALOT, (fvec2_t){10.5f, 9.5f}, 1);
    CHECK(sc_cast(vessel, SC_TECH_EMP, NULL, at_of(z1)));
    CHECK(sc_shields(z1) == 0 && sc_shields(z2) == 0 && sc_shields(templar) == 0 && sc_energy(templar) == 0);
    CHECK(sc_shields(far) == 80 && sc_energy(vessel) == 100 && z1->hp == z1->max_hp);
    reset();

    /* Irradiate: 250 over time to organic units around the host. */
    learn(0, SC_TECH_IRRADIATE);
    vessel = caster(MT_SCIENCE_VESSEL, (fvec2_t){4.5f, 4.5f});
    mobj_t *ultra = spawn(MT_ULTRALISK, (fvec2_t){10.5f, 4.5f}, 1), *ling = spawn(MT_ZERGLING, (fvec2_t){11.1f, 4.5f}, 1);
    mobj_t *goliath = spawn(MT_GOLIATH, (fvec2_t){10.5f, 5.1f}, 1);
    uint32_t ling_id = ling->id;
    CHECK(!sc_cast(vessel, SC_TECH_IRRADIATE, NULL, at_of(ultra))); /* needs a unit */
    CHECK(sc_cast(vessel, SC_TECH_IRRADIATE, ultra, at_of(ultra)) && sc_energy(vessel) == 125);
    tick(frames(37 * 8 + 16));
    CHECK(ultra->max_hp - ultra->hp == 250 && !P_MobjById(ling_id) && goliath->hp == goliath->max_hp);
    reset();

    /* Lockdown: a machine stands and holds fire; flesh is no target. */
    learn(0, SC_TECH_LOCKDOWN);
    mobj_t *ghost = caster(MT_GHOST, (fvec2_t){4.5f, 4.5f});
    goliath = spawn(MT_GOLIATH, (fvec2_t){10.5f, 4.5f}, 1);
    marine = spawn(MT_MARINE, (fvec2_t){10.5f, 6.5f}, 1);
    mobj_t *bait = spawn(MT_MARINE, (fvec2_t){12.5f, 4.5f}, 0);
    CHECK(!sc_cast(ghost, SC_TECH_LOCKDOWN, marine, at_of(marine)));
    CHECK(sc_cast(ghost, SC_TECH_LOCKDOWN, goliath, at_of(goliath)));
    P_RemoveMobj(marine);
    ghost->traits &= ~MF_ATTACK;
    fvec2_t was = at_of(goliath);
    P_MoveUnitTo(&level, goliath, (fvec2_t){20.5f, 4.5f});
    tick(RTS_TICRATE * 3);
    CHECK(fvec2_near(at_of(goliath), was, 0.01f) && bait->hp == bait->max_hp);
    goliath->sc.timers[SC_TIMER_LOCKDOWN] = 1;
    tick(RTS_TICRATE * 2);
    CHECK(bait->hp < bait->max_hp);
    reset();

    /* Yamato Gun and Scanner Sweep. */
    learn(0, SC_TECH_YAMATO_GUN);
    mobj_t *bc = caster(MT_BATTLECRUISER, (fvec2_t){4.5f, 4.5f});
    ultra = spawn(MT_ULTRALISK, (fvec2_t){12.5f, 4.5f}, 1);
    CHECK(sc_cast(bc, SC_TECH_YAMATO_GUN, ultra, at_of(ultra)) && sc_energy(bc) == 50);
    CHECK(ultra->max_hp - ultra->hp == 250 - sc_units[MT_ULTRALISK - 1].armor);
    reset();
    CHECK(P_InitSight());
    mobj_t *comsat = caster(MT_COMSAT_STATION, (fvec2_t){4.5f, 4.5f});
    marine = spawn(MT_MARINE, (fvec2_t){30.5f, 30.5f}, 0);
    mobj_t *dt = spawn(75 + 1, (fvec2_t){32.5f, 30.5f}, 1); /* Dark Templar */
    P_UpdateSight();
    CHECK(!P_VisibleTo(marine, dt));
    CHECK(sc_cast(comsat, SC_TECH_SCANNER_SWEEP, NULL, at_of(dt)) && sc_energy(comsat) == 125);
    P_UpdateSight();
    CHECK(P_VisibleTo(marine, dt));
    tick(frames(262));
    P_UpdateSight();
    CHECK(!P_VisibleTo(marine, dt) && count_of(MT_MAP_REVEALER, 0) == 0);
    reset();

    /* A Nuclear Silo keeps one nuke; a Ghost paints the spot for 14 s. */
    mobj_t *silo = building(MT_NUCLEAR_SILO, (ivec2_t){2, 40}, 0);
    CHECK(building(MT_SUPPLY_DEPOT, (ivec2_t){8, 40}, 0)); /* a nuke takes 8 supply */
    CHECK(G_ModelProducerHasTech(silo, product(MT_NUCLEAR_MISSILE)));
    silo->sc.hangar = 1;
    CHECK(!G_ModelProducerHasTech(silo, product(MT_NUCLEAR_MISSILE)) && sc_armed_silo(0) == silo);
    ghost = caster(MT_GHOST, (fvec2_t){4.5f, 20.5f});
    mobj_t *hit = spawn(MT_ULTRALISK, (fvec2_t){10.5f, 20.5f}, 1), *edge = spawn(MT_ULTRALISK, (fvec2_t){16.0f, 20.5f}, 1);
    mobj_t *safe = spawn(MT_ULTRALISK, (fvec2_t){10.5f, 30.5f}, 1);
    uint32_t hit_id = hit->id;
    CHECK(sc_cast(ghost, SC_TECH_NUCLEAR_STRIKE, NULL, at_of(hit)) && silo->sc.hangar == 0);
    tick(frames(13 * 24));
    CHECK(hit->hp == hit->max_hp && ghost->sc.order.kind == SC_ORDER_NUKE);
    tick(frames(2 * 24));
    CHECK(!P_MobjById(hit_id) && edge->hp < edge->max_hp && edge->hp > 0 && safe->hp == safe->max_hp);
    CHECK(!sc_cast(ghost, SC_TECH_NUCLEAR_STRIKE, NULL, at_of(edge))); /* no nuke left */
    free(level.sight.cells);
    level.sight.cells = NULL;
    reset();
    return 0;
}

static int zerg_spells(void) {
    /* Parasite (no research): the host's sight is the caster's too. */
    CHECK(P_InitSight());
    mobj_t *queen = caster(MT_QUEEN, (fvec2_t){10.5f, 30.5f});
    mobj_t *host = spawn(MT_MARINE, (fvec2_t){18.5f, 30.5f}, 1), *behind = spawn(MT_MARINE, (fvec2_t){24.5f, 30.5f}, 1);
    P_UpdateSight();
    CHECK(!P_VisibleTo(queen, behind));
    CHECK(sc_cast(queen, SC_TECH_PARASITE, host, at_of(host)) && sc_energy(queen) == 150);
    P_UpdateSight();
    CHECK(P_VisibleTo(queen, behind) && (host->sc.parasite & (UINT32_C(0x40000000) >> queen->team)));
    free(level.sight.cells);
    level.sight.cells = NULL;
    reset();

    /* Spawn Broodling kills a ground, non-robotic unit and hatches two. */
    learn(0, SC_TECH_SPAWN_BROODLING);
    queen = caster(MT_QUEEN, (fvec2_t){4.5f, 4.5f});
    mobj_t *zealot = spawn(MT_ZEALOT, (fvec2_t){9.5f, 4.5f}, 1), *reaver = spawn(MT_REAVER, (fvec2_t){9.5f, 6.5f}, 1);
    mobj_t *muta = spawn(MT_MUTALISK, (fvec2_t){9.5f, 8.5f}, 1);
    uint32_t zealot_id = zealot->id;
    CHECK(!sc_cast(queen, SC_TECH_SPAWN_BROODLING, reaver, at_of(reaver)) &&
          !sc_cast(queen, SC_TECH_SPAWN_BROODLING, muta, at_of(muta)));
    CHECK(sc_cast(queen, SC_TECH_SPAWN_BROODLING, zealot, at_of(zealot)) && sc_energy(queen) == 50);
    tick(2);
    CHECK(!P_MobjById(zealot_id) && count_of(MT_BROODLING, 0) == 2);
    tick(frames(1800));
    CHECK(count_of(MT_BROODLING, 0) == 0);
    reset();

    /* Ensnare slows a clump for a while. */
    learn(0, SC_TECH_ENSNARE);
    queen = caster(MT_QUEEN, (fvec2_t){4.5f, 4.5f});
    mobj_t *m1 = spawn(MT_MARINE, (fvec2_t){10.5f, 4.5f}, 1), *m2 = spawn(MT_MARINE, (fvec2_t){11.5f, 4.5f}, 1);
    CHECK(sc_cast(queen, SC_TECH_ENSNARE, NULL, at_of(m1)));
    CHECK(m1->speed == m1->info->speed * 0.5f && m2->speed == m2->info->speed * 0.5f && queen->speed == queen->info->speed);
    tick(frames(75 * 8));
    CHECK(m1->speed == m1->info->speed);
    reset();

    /* Dark Swarm: ranged attacks miss the units under it, blows land. */
    mobj_t *defiler = caster(MT_DEFILER, (fvec2_t){4.5f, 4.5f});
    mobj_t *ling = spawn(MT_ZERGLING, (fvec2_t){10.5f, 4.5f}, 0), *marine = spawn(MT_MARINE, (fvec2_t){13.5f, 4.5f}, 1);
    zealot = spawn(MT_ZEALOT, (fvec2_t){11.4f, 4.5f}, 1);
    CHECK(sc_cast(defiler, SC_TECH_DARK_SWARM, NULL, at_of(ling)) && count_of(MT_DARK_SWARM, 0) == 1);
    CHECK(shoot(marine, ling) && ling->hp == ling->max_hp);
    CHECK(shoot(zealot, ling) && ling->hp < ling->max_hp);
    tick(frames(900));
    CHECK(count_of(MT_DARK_SWARM, 0) == 0);
    reset();

    /* Plague: 300 over time that never kills. */
    learn(0, SC_TECH_PLAGUE);
    defiler = caster(MT_DEFILER, (fvec2_t){4.5f, 4.5f});
    marine = spawn(MT_MARINE, (fvec2_t){10.5f, 4.5f}, 1);
    mobj_t *ultra = spawn(MT_ULTRALISK, (fvec2_t){11.3f, 4.5f}, 1);
    CHECK(sc_cast(defiler, SC_TECH_PLAGUE, NULL, at_of(marine)) && sc_energy(defiler) == 50);
    tick(frames(75 * 8 + 16));
    CHECK(marine->hp == 1 && ultra->max_hp - ultra->hp == 300 && defiler->hp == defiler->max_hp);
    reset();

    /* Consume: an own zerg unit for 50 energy. */
    learn(0, SC_TECH_CONSUME);
    defiler = caster(MT_DEFILER, (fvec2_t){4.5f, 4.5f});
    defiler->sc.energy = 20 << 8;
    ling = spawn(MT_ZERGLING, (fvec2_t){5.3f, 4.5f}, 0);
    mobj_t *foe = spawn(MT_ZERGLING, (fvec2_t){5.3f, 5.3f}, 1), *pool = building(MT_SPAWNING_POOL, (ivec2_t){8, 8}, 0);
    uint32_t ling_id = ling->id;
    CHECK(!sc_cast(defiler, SC_TECH_CONSUME, foe, at_of(foe)) && !sc_cast(defiler, SC_TECH_CONSUME, pool, at_of(pool)));
    CHECK(sc_cast(defiler, SC_TECH_CONSUME, ling, at_of(ling)));
    tick(2);
    CHECK(!P_MobjById(ling_id) && sc_energy(defiler) == 70);
    reset();
    return 0;
}

static void think(int owner) {
    mobjlist_t all = P_ListMobjs();
    sc_ai_tactics(&level, owner, all.items, all.count);
    P_FreeMobjList(&all);
}
static int wave_plan(const level_t *map, int owner) { (void)map; return owner == 1 ? AI_LEVEL_NORMAL : AI_LEVEL_NONE; }
static bool small_plan(const level_t *map, int owner, int level_, AiPlan *out) {
    (void)map; (void)owner; (void)level_;
    *out = (AiPlan){.wave_interval_ms = 100, .wave_min_size = 1, .wave_max_size = 8};
    return true;
}

static int computer(void) {
    /* Storm where enemies clump, never over our own. */
    learn(0, SC_TECH_PSIONIC_STORM);
    mobj_t *ht = caster(MT_HIGH_TEMPLAR, (fvec2_t){4.5f, 4.5f});
    for (int i = 0; i < 3; i++) spawn(MT_ZERGLING, (fvec2_t){10.5f + 0.5f * i, 4.5f}, 1);
    mobj_t *own = spawn(MT_ZEALOT, (fvec2_t){11.0f, 4.9f}, 0);
    think(0);
    CHECK(count_of(MT_MAP_REVEALER, 0) == 0 && sc_energy(ht) == 200);
    P_RemoveMobj(own);
    think(0);
    CHECK(count_of(MT_MAP_REVEALER, 0) == 1 && sc_energy(ht) == 125);
    reset();

    /* A Defensive Matrix for a wounded frontliner under fire. */
    mobj_t *vessel = caster(MT_SCIENCE_VESSEL, (fvec2_t){4.5f, 4.5f}), *marine = spawn(MT_MARINE, (fvec2_t){6.5f, 4.5f}, 0);
    think(0);
    CHECK(!marine->sc.matrix);
    marine->hp = 20;
    marine->sc.flags |= SC_HIT;
    think(0);
    CHECK(marine->sc.matrix > 0 && sc_energy(vessel) == 100);
    reset();

    /* Tanks siege as enemies come into reach and unsiege when they are gone. */
    learn(0, SC_TECH_SIEGE_MODE);
    mobj_t *tank = spawn(MT_SIEGE_TANK, (fvec2_t){4.5f, 20.5f}, 0), *ling = spawn(MT_ZERGLING, (fvec2_t){14.5f, 20.5f}, 1);
    ling->traits &= ~(MF_MOBILE | MF_ATTACK);
    think(0);
    CHECK(tank->core.state_id == SC_SIEGE_STATE);
    tick(RTS_TICRATE * 3);
    CHECK(tank->type_id == MT_SIEGE_MODE);
    P_RemoveMobj(ling);
    tick(1);
    think(0);
    tick(RTS_TICRATE * 3);
    CHECK(tank->type_id == MT_SIEGE_TANK && (tank->traits & MF_MOBILE));
    reset();

    /* Marines man the Bunker; Carriers and Reavers refill their hangars. */
    mobj_t *b = building(MT_BUNKER, (ivec2_t){10, 10}, 0);
    marine = spawn(MT_MARINE, (fvec2_t){11.5f, 14.5f}, 0);
    mobj_t *carrier = spawn(MT_CARRIER, (fvec2_t){30.5f, 30.5f}, 0);
    think(0);
    tick(RTS_TICRATE * 3);
    CHECK((marine->sc.flags & SC_LOADED) && marine->sc.parent == b->id);
    CHECK(carrier->production && carrier->production->product_type == MT_INTERCEPTOR);
    reset();

    /* Research starts for the abilities our units have. */
    mobj_t *archives = building(MT_TEMPLAR_ARCHIVES, (ivec2_t){10, 10}, 0);
    CHECK(archives && building(MT_PYLON, (ivec2_t){14, 10}, 0)); /* power */
    think(0);
    CHECK(!archives->production);
    caster(MT_HIGH_TEMPLAR, (fvec2_t){4.5f, 4.5f});
    think(0);
    CHECK(archives->production && archives->production->product_class == RTS_PRODUCT_UPGRADE &&
          archives->production->product_type - SC_TECH_UI == SC_TECH_PSIONIC_STORM);
    reset();

    /* An add-on a product waits for is built first. */
    const AiGameInterface *ai = G_AiInterface();
    mobj_t *factory = building(MT_FACTORY, (ivec2_t){4, 4}, 0);
    CHECK(factory && building(MT_SUPPLY_DEPOT, (ivec2_t){30, 30}, 0));
    CHECK(ai->can_purchase(&level, 0, MT_SIEGE_TANK) == AI_BUY_NEED_TECH);
    CHECK(ai->develop(&level, 0, MT_SIEGE_TANK) && factory->production &&
          factory->production->product_type == MT_MACHINE_SHOP);
    finish(MT_MACHINE_SHOP);
    CHECK(sc_addon_of(factory) && ai->can_purchase(&level, 0, MT_SIEGE_TANK) == AI_BUY_OK);
    reset();

    /* Casters ride along with a wave. */
    AiGameInterface waves = *ai;
    waves.player_level = wave_plan;
    waves.plan = small_plan;
    waves.tactics = NULL;
    AiContext ctx;
    P_AiInit(&ctx);
    P_AiAttachGame(&ctx, &waves);
    P_AiSetFeatures(&ctx, AI_FEATURE_ATTACK);
    building(MT_NEXUS, (ivec2_t){4, 4}, 1);
    building(MT_COMMAND_CENTER, (ivec2_t){36, 36}, 0);
    mobj_t *zealot = spawn(MT_ZEALOT, (fvec2_t){10.5f, 10.5f}, 1);
    ht = spawn(MT_HIGH_TEMPLAR, (fvec2_t){11.5f, 10.5f}, 1);
    for (int t = 0; t < 40 && !P_HasMoveOrder(ht); t++) {
        mobjlist_t all = P_ListMobjs();
        P_AiTick(&ctx, &level, all.items, all.count, gameinfo, 1000 / RTS_TICRATE);
        P_FreeMobjList(&all);
        P_NavRunPlans(&level);
    }
    CHECK(P_AiStats(&ctx, 1)->waves == 1 && P_HasMoveOrder(zealot) && P_HasMoveOrder(ht));
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
    CHECK(!protoss_spells());
    CHECK(!terran_spells());
    CHECK(!zerg_spells());
    CHECK(!computer());
    P_FreeLevel(&level);
    puts("PASS: interceptors, scarabs, siege mode, add-ons, Bunkers, Archons, research, every spell and the computer player's use of them");
    return 0;
}
