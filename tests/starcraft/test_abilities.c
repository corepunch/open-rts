#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c,"StarCraft abilities",#c)
/* The base-game abilities, upgrades and carriers that were missing: Stim
 * Packs, Spider Mines, Burrow, Recall, Stasis Field, Infestation and the
 * Infested Terran, Scourge, speed/sight/cooldown upgrades and their
 * research, Dropship/Shuttle/Overlord transport and the Nydus Canal.
 * Values are OpenBW's (bwgame.h) on the retail DAT files. */

static mobj_t *spawn(int type, fixed2_t at, int owner) {
    mobj_t *u = P_SpawnMobj(fixed3_from_fixed2(at, 0), (uint16_t)type);
    if (u) {
        u->owner = u->team = (uint8_t)owner;
        u->allegiance = owner == consoleplayer ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY;
        sc_start(u);
    }
    return u;
}
static mobj_t *building(int type, ivec2_t cell, int owner) {
    mobj_t *mo = spawn(type, P_BuildingPosition((uint16_t)type, cell), owner);
    P_SyncBuildingBlocking();
    return mo;
}
static const StaticProductDefinition *product(int ui) { return G_ModelProductByUIId(NULL, ui); }
static void tick(int tics) {
    for (int t = 0; t < tics; t++) { P_Ticker(); G_ProductionTicker(RTS_TICK_MS); }
}
static int frames(int n) { return n * RTS_TICRATE / 24 + 2; }
static fixed2_t at_of(const mobj_t *mo) { return fixed3_xy(mo->core.position); }
static void reset(void) {
    P_FreeThinkers();
    P_InitThinkers();
    memset(level.upgrades, 0, sizeof(level.upgrades));
    level.player_resources[0][0] = level.player_resources[0][1] = 5000;
}
static void learn(int owner, int tech) { level.upgrades[SC_UPGRADES + tech][owner].weapon = 1; }
static void upgrade(int owner, int id) { level.upgrades[id][owner].weapon = 1; }
static bool cast(mobj_t *by, int tech, const mobj_t *target, fixed2_t at) {
    ticcmd_t cmd = {.order = TC_SPELL, .count = 1, .units = {by->id}, .product = tech,
                    .target = target ? target->id : 0, .position = fixed3_from_fixed2(at, 0)};
    G_RunTiccmd(0, &cmd);
    return true;
}
static bool makes(int ui, int maker) {
    const StaticProductDefinition *p = product(ui);
    if (!p) return false;
    for (int i = 0; i < p->maker_count; i++) if (p->makers[i] == maker) return true;
    return false;
}
static bool needs(int ui, int building) {
    const StaticProductDefinition *p = product(ui);
    if (!p) return false;
    for (int i = 0; i < p->prerequisite_count; i++) if (p->prerequisites[i] == building) return true;
    return false;
}
static fixed_t near_px(int px) { return SC_PIXELS(px); }
/* Gone, or dying in its death animation. */
static bool dead(uint32_t id) { const mobj_t *mo = P_MobjById(id); return !mo || mo->hp <= 0 || mo->remove; }

static int research(void) {
    /* Every base-game research has a building that offers it. */
    CHECK(makes(SC_TECH_UI + SC_TECH_STIM_PACKS, MT_ACADEMY));
    CHECK(makes(SC_TECH_UI + SC_TECH_SPIDER_MINES, MT_MACHINE_SHOP));
    CHECK(makes(SC_TECH_UI + SC_TECH_BURROWING, MT_HATCHERY) && makes(SC_TECH_UI + SC_TECH_BURROWING, MT_HIVE));
    CHECK(makes(SC_TECH_UI + SC_TECH_RECALL, MT_ARBITER_TRIBUNAL) && makes(SC_TECH_UI + SC_TECH_STASIS_FIELD, MT_ARBITER_TRIBUNAL));
    CHECK(makes(sc_upgrade_product(24, 1), MT_LAIR) && makes(sc_upgrade_product(24, 1), MT_HIVE)); /* Ventral Sacs */
    CHECK(makes(sc_upgrade_product(25, 1), MT_LAIR) && makes(sc_upgrade_product(26, 1), MT_LAIR));
    CHECK(makes(sc_upgrade_product(28, 1), MT_SPAWNING_POOL) && needs(sc_upgrade_product(28, 1), MT_HIVE));
    CHECK(makes(sc_upgrade_product(37, 1), MT_ROBOTICS_SUPPORT_BAY));
    CHECK(makes(sc_upgrade_product(38, 1), MT_OBSERVATORY) && makes(sc_upgrade_product(39, 1), MT_OBSERVATORY));
    CHECK(makes(sc_upgrade_product(41, 1), MT_FLEET_BEACON) && makes(sc_upgrade_product(42, 1), MT_FLEET_BEACON));
    CHECK(makes(sc_upgrade_product(44, 1), MT_ARBITER_TRIBUNAL));
    /* An infested Command Center trains Infested Terrans; no egg. */
    CHECK(makes(MT_INFESTED_TERRAN, MT_INFESTED_COMMAND_CENTER));
    /* Researching Stim Packs at an Academy grants it. */
    mobj_t *academy = building(MT_ACADEMY, (ivec2_t){30, 30}, 0);
    CHECK(academy && G_QueueProduct(academy, product(SC_TECH_UI + SC_TECH_STIM_PACKS)));
    tick(G_ModelProductTrainingTimeMs(product(SC_TECH_UI + SC_TECH_STIM_PACKS)) * RTS_TICRATE / 1000 * 21 / 20 + 2);
    CHECK(sc_has_tech(0, SC_TECH_STIM_PACKS));
    reset();
    return 0;
}

static int upgrades(void) {
    mobj_t *ling = spawn(MT_ZERGLING, FIXED2_LIT(4.5, 4.5), 0), *lord = spawn(MT_OVERLORD, FIXED2_LIT(6.5, 4.5), 0);
    mobj_t *scout = spawn(MT_SCOUT, FIXED2_LIT(8.5, 4.5), 0), *ghost = spawn(MT_GHOST, FIXED2_LIT(10.5, 4.5), 0);
    CHECK(ling && lord && scout && ghost);
    fixed_t ling_base = ling->info->speed, lord_base = lord->info->speed;
    CHECK(sc_unit_speed(ling) == ling_base);
    upgrade(0, 27); upgrade(0, 26); upgrade(0, 42);
    /* Half again; at least 3 1/3 pixels a frame; a Scout's own 6 2/3. */
    CHECK(sc_unit_speed(ling) == ling_base + ling_base / 2);
    CHECK(lord_base < SC_PIXELS(80) && sc_unit_speed(lord) == SC_PIXELS(10 * 24) / 3);
    CHECK(sc_unit_speed(scout) == SC_PIXELS(20 * 24) / 3);
    tick(1);
    CHECK(ling->speed == ling_base + ling_base / 2);
    /* Adrenal Glands halves the Zergling's cooldown, kept to five frames. */
    int cooldown = ling->info->attack.cooldown_ms;
    CHECK(G_WeaponCooldown(ling, &ling->info->attack, cooldown) == cooldown);
    upgrade(0, 28);
    CHECK(G_WeaponCooldown(ling, &ling->info->attack, cooldown) == (cooldown / 2 < 209 ? 209 : cooldown / 2));
    /* Ocular Implants, Antennae, Apial Sensors: eleven cells. */
    int radius = 9;
    CHECK(G_SightRadius(ghost, &radius) && radius == 9);
    upgrade(0, 20); upgrade(0, 25); upgrade(0, 41);
    CHECK(G_SightRadius(ghost, &radius) && radius == 11);
    radius = 9; CHECK(G_SightRadius(lord, &radius) && radius == 11);
    radius = 9; CHECK(G_SightRadius(scout, &radius) && radius == 11);
    reset();
    return 0;
}

static int stim(void) {
    mobj_t *marine = spawn(MT_MARINE, FIXED2_LIT(4.5, 4.5), 0);
    CHECK(marine);
    CHECK(!sc_cast(marine, SC_TECH_STIM_PACKS, NULL, (fixed2_t){0}));
    learn(0, SC_TECH_STIM_PACKS);
    fixed_t base = marine->info->speed;
    cast(marine, SC_TECH_STIM_PACKS, NULL, (fixed2_t){0});
    CHECK(marine->hp == marine->max_hp - 10 && marine->sc.timers[SC_TIMER_STIM] == 37 * 8);
    tick(1);
    CHECK(marine->speed == base + base / 2);
    int cooldown = marine->info->attack.cooldown_ms;
    CHECK(G_WeaponCooldown(marine, &marine->info->attack, cooldown) == cooldown / 2);
    /* At ten hit points or fewer it is refused. */
    marine->hp = 10;
    CHECK(!sc_cast(marine, SC_TECH_STIM_PACKS, NULL, (fixed2_t){0}) && marine->hp == 10);
    tick(frames(37 * 8));
    CHECK(!marine->sc.timers[SC_TIMER_STIM] && marine->speed == base);
    /* Ensnare cancels a speed boost. */
    marine->sc.timers[SC_TIMER_STIM] = 100;
    marine->sc.timers[SC_TIMER_ENSNARE] = 100;
    CHECK(sc_unit_speed(marine) == base);
    reset();
    return 0;
}

static int spider_mines(void) {
    mobj_t *vulture = spawn(MT_VULTURE, FIXED2_LIT(4.5, 4.5), 0);
    CHECK(vulture && vulture->sc.hangar == 3);
    fixed2_t spot = FIXED2_LIT(10.5, 4.5);
    CHECK(!sc_cast(vulture, SC_TECH_SPIDER_MINES, NULL, spot));
    learn(0, SC_TECH_SPIDER_MINES);
    cast(vulture, SC_TECH_SPIDER_MINES, NULL, spot);
    tick(RTS_TICRATE * 3);
    mobj_t *mine = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (((mobj_t *)th)->type_id == MT_SPIDER_MINE) mine = (mobj_t *)th;
    CHECK(mine && vulture->sc.hangar == 2 && mine->owner == 0);
    CHECK((mine->info->traits & MF_MOBILE) && mine->info->speed > 0);
    CHECK(fixed2_near(at_of(mine), spot, near_px(24)));
    CHECK((mine->sc.flags & SC_BURROWED) && (mine->traits & MF_CLOAKED) && !(mine->traits & MF_ATTACK));
    /* A hovering Probe walks by unharmed. */
    mobj_t *probe = spawn(MT_PROBE, FIXED2_LIT(11.5, 4.5), 1);
    tick(RTS_TICRATE);
    CHECK(probe->hp == probe->max_hp && (mine->sc.flags & SC_BURROWED));
    P_RemoveMobj(probe);
    /* A Zergling in three cells wakes it; it chases and bursts for 125. */
    uint32_t mine_id = mine->id;
    mobj_t *ling = spawn(MT_ZERGLING, FIXED2_LIT(12.5, 4.5), 1);
    uint32_t ling_id = ling->id;
    tick(RTS_TICRATE * 3);
    CHECK(dead(mine_id) && dead(ling_id));
    /* No mines left, no laying. */
    vulture->sc.hangar = 0;
    CHECK(!sc_cast(vulture, SC_TECH_SPIDER_MINES, NULL, spot));
    reset();
    return 0;
}

static int burrow(void) {
    mobj_t *ling = spawn(MT_ZERGLING, FIXED2_LIT(4.5, 4.5), 0), *ultra = spawn(MT_ULTRALISK, FIXED2_LIT(8.5, 4.5), 0);
    CHECK(ling && ultra);
    CHECK(!sc_cast(ling, SC_TECH_BURROWING, NULL, (fixed2_t){0}));
    learn(0, SC_TECH_BURROWING);
    int techs[8], n = sc_unit_techs(MT_ULTRALISK, techs, 8);
    for (int i = 0; i < n; i++) CHECK(techs[i] != SC_TECH_BURROWING);
    CHECK(!sc_cast(ultra, SC_TECH_BURROWING, NULL, (fixed2_t){0}));
    cast(ling, SC_TECH_BURROWING, NULL, (fixed2_t){0});
    tick(RTS_TICRATE);
    CHECK((ling->sc.flags & SC_BURROWED) && (ling->traits & MF_CLOAKED) && !(ling->traits & (MF_MOBILE | MF_ATTACK)));
    /* An enemy without a detector cannot see or shoot it. */
    mobj_t *marine = spawn(MT_MARINE, FIXED2_LIT(6.5, 4.5), 1);
    P_UpdateSight();
    CHECK(!P_VisibleTo(marine, ling) && P_VisibleTo(ling, ling));
    /* It does not move while dug in. */
    fixed2_t was = at_of(ling);
    ticcmd_t move = {.order = TC_MOVE, .count = 1, .units = {ling->id}, .position = fixed3_from_fixed2(FIXED2_LIT(20.5, 4.5), 0)};
    G_RunTiccmd(0, &move);
    tick(RTS_TICRATE);
    CHECK(fixed2_equal(at_of(ling), was));
    cast(ling, SC_TECH_BURROWING, NULL, (fixed2_t){0});
    tick(RTS_TICRATE);
    CHECK(!(ling->sc.flags & SC_BURROWED) && !(ling->traits & MF_CLOAKED) && (ling->traits & MF_MOBILE));
    reset();
    return 0;
}

static int arbiter(void) {
    learn(0, SC_TECH_RECALL);
    learn(0, SC_TECH_STASIS_FIELD);
    mobj_t *arb = spawn(MT_ARBITER, FIXED2_LIT(4.5, 4.5), 0);
    arb->sc.energy = 250 << 8;
    mobj_t *z1 = spawn(MT_ZEALOT, FIXED2_LIT(30.5, 30.5), 0), *z2 = spawn(MT_ZEALOT, FIXED2_LIT(31.5, 30.5), 0);
    mobj_t *far = spawn(MT_ZEALOT, FIXED2_LIT(36.5, 30.5), 0), *foe = spawn(MT_MARINE, FIXED2_LIT(31.5, 31.5), 1);
    CHECK(arb && z1 && z2 && far && foe);
    cast(arb, SC_TECH_RECALL, NULL, FIXED2_LIT(31, 30.5));
    CHECK(sc_energy(arb) == 100);
    tick(frames(30));
    /* Its own units in the square come; others stay. */
    CHECK(fixed2_near(at_of(z1), at_of(arb), FIXED_FROM_INT(3)) && fixed2_near(at_of(z2), at_of(arb), FIXED_FROM_INT(3)));
    CHECK(!fixed2_equal(at_of(z1), at_of(z2)));
    CHECK(fixed2_near(at_of(far), FIXED2_LIT(36.5, 30.5), FIXED_ONE / 4) && fixed2_near(at_of(foe), FIXED2_LIT(31.5, 31.5), FIXED_ONE));
    /* Stasis: everyone in the square, untouchable and still, for 131 beats. */
    arb->sc.energy = 250 << 8;
    mobj_t *m2 = spawn(MT_MARINE, FIXED2_LIT(9.5, 4.5), 1), *out = spawn(MT_MARINE, FIXED2_LIT(14.5, 4.5), 1);
    cast(arb, SC_TECH_STASIS_FIELD, NULL, at_of(m2));
    tick(1);
    CHECK(m2->sc.timers[SC_TIMER_STASIS] > 0 && !out->sc.timers[SC_TIMER_STASIS] && sc_disabled(m2));
    CHECK(!P_CanTarget(z1, m2) && !G_UnitCommandable(m2));
    int hp = m2->hp;
    CHECK(sc_hit(0, sc_weapon(0), m2, 50, 1) == 0 && m2->hp == hp);
    tick(frames(131 * 8));
    CHECK(!m2->sc.timers[SC_TIMER_STASIS] && !sc_disabled(m2) && P_CanTarget(z1, m2));
    reset();
    return 0;
}

static int infestation(void) {
    mobj_t *cc = building(MT_COMMAND_CENTER, (ivec2_t){12, 4}, 1);
    mobj_t *queen = spawn(MT_QUEEN, FIXED2_LIT(4.5, 12.5), 0);
    CHECK(cc && queen && spawn(MT_OVERLORD, FIXED2_LIT(13.5, 10.5), 0)); /* supply and sight */
    P_UpdateSight();
    CHECK(!sc_cast(queen, SC_TECH_INFESTATION, cc, at_of(cc))); /* healthy: refused */
    cc->hp = cc->max_hp / 2 - 1;
    int hp = cc->hp;
    uint32_t cc_id = cc->id;
    cast(queen, SC_TECH_INFESTATION, cc, at_of(cc));
    tick(RTS_TICRATE * 10);
    mobj_t *infested = P_MobjById(cc_id);
    /* The completed infested building regenerates while the Queen exits. */
    CHECK(infested && infested->type_id == MT_INFESTED_COMMAND_CENTER && infested->owner == 0 &&
          infested->sc.sprite_owner == 0 && infested->hp > hp);
    CHECK(!P_MobjIsHidden(queen) && (queen->traits & MF_SELECTABLE) && queen->sc.order.kind == SC_ORDER_NONE);
    /* It trains Infested Terrans, which blow up on their target and themselves. */
    CHECK(G_QueueProduct(infested, product(MT_INFESTED_TERRAN)));
    tick(G_ModelProductTrainingTimeMs(product(MT_INFESTED_TERRAN)) * RTS_TICRATE / 1000 * 21 / 20 + 2);
    mobj_t *terran = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (((mobj_t *)th)->type_id == MT_INFESTED_TERRAN && !((mobj_t *)th)->remove) terran = (mobj_t *)th;
    CHECK(terran && terran->owner == 0);
    mobj_t *victim = spawn(MT_SIEGE_TANK, fixed2_add(at_of(terran), FIXED2_LIT(0.6, 0)), 1);
    uint32_t terran_id = terran->id;
    P_UpdateSight();
    terran->attack.target = victim;
    terran->attack.cooldown_left_ms = 0;
    CHECK(P_Attack(terran));
    tick(2);
    CHECK(dead(terran_id) && victim->hp <= 0);
    /* A Scourge dies with its blow. */
    mobj_t *scourge = spawn(MT_SCOURGE, FIXED2_LIT(20.5, 20.5), 0), *wraith = spawn(MT_WRAITH, FIXED2_LIT(20.6, 20.5), 1);
    uint32_t scourge_id = scourge->id;
    P_UpdateSight();
    scourge->attack.target = wraith;
    scourge->attack.cooldown_left_ms = 0;
    CHECK(P_Attack(scourge));
    tick(2);
    CHECK(dead(scourge_id) && wraith->hp < wraith->max_hp);
    reset();
    return 0;
}

static int invincible_status_effects(void) {
    enum { IRRADIATE_BEATS = 37, PLAGUE_BEATS = 75 };
    mobj_t *host = spawn(MT_GHOST, FIXED2_LIT(10.5, 10.5), 0);
    mobj_t *neighbor = spawn(MT_MARINE, FIXED2_LIT(11, 10.5), 0);
    mobj_t *invincible_neighbor = spawn(MT_MARINE, FIXED2_LIT(10.5, 11), 0);
    mobj_t *enemy = spawn(MT_MARINE, FIXED2_LIT(10, 10.5), 1);
    mobj_t *burrowed = spawn(MT_ZERGLING, FIXED2_LIT(10.5, 10), 0);
    mobj_t *larva_home = spawn(MT_HATCHERY, FIXED2_LIT(30.5, 30.5), 0);
    mobj_t *larva = spawn(MT_LARVA, FIXED2_LIT(11, 11), 0);
    mobj_t *egg = spawn(MT_EGG, FIXED2_LIT(9.5, 10.5), 0);
    mobj_t *large_neighbor = spawn(MT_ULTRALISK, fixed2_add(FIXED2_LIT(10.5, 10.5), (fixed2_t){SC_PIXELS(36), 0}), 0);
    fixed2_t origin = FIXED2_LIT(10.5, 10.5);
    mobj_t *range32 = spawn(MT_MARINE, fixed2_add(origin, (fixed2_t){SC_PIXELS(48), 0}), 0);
    mobj_t *range33 = spawn(MT_MARINE, fixed2_add(origin, (fixed2_t){SC_PIXELS(49), 0}), 0);
    /* Ghost/Marine bounds make the box gaps (24,24) and (25,24). */
    mobj_t *diagonal32 = spawn(MT_MARINE, fixed2_add(origin, (fixed2_t){SC_PIXELS(40), SC_PIXELS(45)}), 0);
    mobj_t *diagonal33 = spawn(MT_MARINE, fixed2_add(origin, (fixed2_t){SC_PIXELS(41), SC_PIXELS(45)}), 0);
    CHECK(host && neighbor && invincible_neighbor && enemy && burrowed && larva_home && larva && egg &&
          large_neighbor && range32 && range33 && diagonal32 && diagonal33);
    larva->sc.parent = larva_home->id;
    host->traits &= ~(MF_ATTACK | MF_MOBILE);
    neighbor->traits &= ~(MF_ATTACK | MF_MOBILE);
    invincible_neighbor->traits &= ~(MF_ATTACK | MF_MOBILE);
    enemy->traits &= ~(MF_ATTACK | MF_MOBILE);
    burrowed->traits &= ~(MF_ATTACK | MF_MOBILE);
    large_neighbor->traits &= ~(MF_ATTACK | MF_MOBILE);
    range32->traits &= ~(MF_ATTACK | MF_MOBILE);
    range33->traits &= ~(MF_ATTACK | MF_MOBILE);
    diagonal32->traits &= ~(MF_ATTACK | MF_MOBILE);
    diagonal33->traits &= ~(MF_ATTACK | MF_MOBILE);
    burrowed->sc.flags |= SC_BURROWED;
    int host_hp = host->hp, neighbor_hp = neighbor->hp, invincible_neighbor_hp = invincible_neighbor->hp;
    int enemy_hp = enemy->hp, burrowed_hp = burrowed->hp, larva_hp = larva->hp, egg_hp = egg->hp;
    int large_neighbor_hp = large_neighbor->hp;
    int range32_hp = range32->hp, range33_hp = range33->hp;
    int diagonal32_hp = diagonal32->hp, diagonal33_hp = diagonal33->hp;
    uint32_t host_id = host->id, neighbor_id = neighbor->id, invincible_neighbor_id = invincible_neighbor->id;
    host->sc.timers[SC_TIMER_IRRADIATE] = IRRADIATE_BEATS;
    host->sc.flags |= SC_INVINCIBLE;
    invincible_neighbor->sc.flags |= SC_INVINCIBLE;
    leveltime = 0;
    tick(40);
    CHECK(host->hp == host_hp && host->sc.timers[SC_TIMER_IRRADIATE] > 0);
    CHECK(neighbor->hp < neighbor_hp && invincible_neighbor->hp == invincible_neighbor_hp);
    CHECK(enemy->hp < enemy_hp);
    CHECK(large_neighbor->hp < large_neighbor_hp);
    CHECK(range32->hp < range32_hp && range33->hp == range33_hp);
    CHECK(diagonal32->hp < diagonal32_hp && diagonal33->hp == diagonal33_hp);
    CHECK(burrowed->hp == burrowed_hp);
    CHECK(larva->hp == larva_hp);
    CHECK(egg->hp == egg_hp);
    tick(RTS_TICRATE * 13);
    host = P_MobjById(host_id);
    invincible_neighbor = P_MobjById(invincible_neighbor_id);
    CHECK(host && host->hp == host_hp && host->sc.timers[SC_TIMER_IRRADIATE] == 0);
    CHECK(invincible_neighbor && invincible_neighbor->hp == invincible_neighbor_hp);
    neighbor = P_MobjById(neighbor_id);
    CHECK(!neighbor || neighbor->hp < neighbor_hp);

    reset();
    host = spawn(MT_ZERGLING, FIXED2_LIT(10.5, 10.5), 0);
    neighbor = spawn(MT_MARINE, FIXED2_LIT(11, 10.5), 0);
    CHECK(host && neighbor);
    host->traits &= ~MF_ATTACK;
    neighbor->traits &= ~MF_ATTACK;
    learn(0, SC_TECH_BURROWING);
    cast(host, SC_TECH_BURROWING, NULL, (fixed2_t){0});
    tick(RTS_TICRATE);
    CHECK(host->sc.flags & SC_BURROWED);
    host_hp = host->hp;
    neighbor_hp = neighbor->hp;
    host->sc.timers[SC_TIMER_IRRADIATE] = IRRADIATE_BEATS;
    leveltime = 0;
    tick(15);
    CHECK(host->hp < host_hp);
    CHECK(neighbor->hp == neighbor_hp);

    reset();
    mobj_t *carrier = spawn(MT_OVERLORD, FIXED2_LIT(10.5, 10.5), 0);
    host = spawn(MT_MARINE, FIXED2_LIT(12.5, 10.5), 0);
    neighbor = spawn(MT_MARINE, FIXED2_LIT(11, 10.5), 0);
    CHECK(carrier && host && neighbor);
    carrier->sc.cargo[0] = host->id;
    host->sc.parent = carrier->id;
    host->sc.flags |= SC_LOADED;
    host->traits = (host->traits & ~(MF_SELECTABLE | MF_SELECTED | MF_MOBILE)) | MF_NOBLOCKMAP | MF_DONTDRAW;
    host->core.position = carrier->core.position;
    host->traits &= ~MF_ATTACK;
    neighbor->traits &= ~MF_ATTACK;
    neighbor_hp = neighbor->hp;
    int carrier_hp = carrier->hp;
    host->sc.timers[SC_TIMER_IRRADIATE] = IRRADIATE_BEATS;
    leveltime = 0;
    tick(15);
    CHECK(neighbor->hp == neighbor_hp && carrier->hp < carrier_hp);

    reset();
    carrier = spawn(MT_DROPSHIP, FIXED2_LIT(10.5, 10.5), 0);
    host = spawn(MT_MARINE, FIXED2_LIT(12.5, 10.5), 0);
    neighbor = spawn(MT_MARINE, FIXED2_LIT(11, 10.5), 0);
    CHECK(carrier && host && neighbor);
    carrier->sc.cargo[0] = host->id;
    host->sc.parent = carrier->id;
    host->sc.flags |= SC_LOADED;
    host->traits = (host->traits & ~(MF_SELECTABLE | MF_SELECTED | MF_MOBILE)) | MF_NOBLOCKMAP | MF_DONTDRAW;
    host->core.position = carrier->core.position;
    host->traits &= ~MF_ATTACK;
    neighbor->traits &= ~MF_ATTACK;
    neighbor_hp = neighbor->hp;
    carrier_hp = carrier->hp;
    host->sc.timers[SC_TIMER_IRRADIATE] = IRRADIATE_BEATS;
    leveltime = 0;
    tick(15);
    CHECK(neighbor->hp == neighbor_hp && carrier->hp == carrier_hp);

    reset();
    carrier = spawn(MT_OVERLORD, FIXED2_LIT(10.5, 10.5), 0);
    host = spawn(MT_MARINE, FIXED2_LIT(12.5, 10.5), 0);
    neighbor = spawn(MT_MARINE, FIXED2_LIT(11, 10.5), 0);
    CHECK(carrier && host && neighbor);
    carrier->sc.cargo[0] = host->id;
    host->sc.parent = carrier->id;
    host->sc.flags |= SC_LOADED;
    host->traits = (host->traits & ~(MF_SELECTABLE | MF_SELECTED | MF_MOBILE)) | MF_NOBLOCKMAP | MF_DONTDRAW;
    host->core.position = carrier->core.position;
    host->traits &= ~MF_ATTACK;
    neighbor->traits &= ~MF_ATTACK;
    neighbor_hp = neighbor->hp;
    carrier_hp = carrier->hp;
    carrier->sc.flags |= SC_INVINCIBLE;
    host->sc.timers[SC_TIMER_IRRADIATE] = IRRADIATE_BEATS;
    leveltime = 0;
    tick(15);
    CHECK(neighbor->hp == neighbor_hp && carrier->hp == carrier_hp);

    reset();
    mobj_t *plagued = spawn(MT_MARINE, FIXED2_LIT(10.5, 10.5), 0);
    CHECK(plagued);
    int hp = plagued->hp;
    plagued->sc.timers[SC_TIMER_PLAGUE] = PLAGUE_BEATS;
    plagued->sc.flags |= SC_INVINCIBLE;
    tick(RTS_TICRATE * 27);
    CHECK(plagued->hp == hp && plagued->sc.timers[SC_TIMER_PLAGUE] == 0);

    reset();
    plagued = spawn(MT_MARINE, FIXED2_LIT(10.5, 10.5), 0);
    CHECK(plagued);
    hp = plagued->hp;
    plagued->sc.timers[SC_TIMER_PLAGUE] = PLAGUE_BEATS;
    plagued->sc.flags |= SC_INVINCIBLE;
    tick(10);
    CHECK(plagued->hp == hp && plagued->sc.timers[SC_TIMER_PLAGUE] > 0);
    plagued->sc.flags &= (uint8_t)~SC_INVINCIBLE;
    for (int i = 0; i < RTS_TICRATE && plagued->hp == hp; i++) tick(1);
    CHECK(plagued->hp < hp && plagued->sc.timers[SC_TIMER_PLAGUE] > 0);
    reset();
    return 0;
}

static int transports(void) {
    mobj_t *ship = spawn(MT_DROPSHIP, FIXED2_LIT(10.5, 10.5), 0);
    mobj_t *m1 = spawn(MT_MARINE, FIXED2_LIT(8.5, 12.5), 0), *m2 = spawn(MT_MARINE, FIXED2_LIT(9.5, 12.5), 0);
    mobj_t *tank = spawn(MT_SIEGE_TANK, FIXED2_LIT(12.5, 13.5), 0), *tank2 = spawn(MT_SIEGE_TANK, FIXED2_LIT(14.5, 13.5), 0);
    mobj_t *zealot = spawn(MT_ZEALOT, FIXED2_LIT(8.5, 14.5), 0), *wraith = spawn(MT_WRAITH, FIXED2_LIT(6.5, 6.5), 0);
    CHECK(ship && m1 && m2 && tank && tank2 && zealot && wraith && sc_units[MT_DROPSHIP - 1].space_provided == 8);
    CHECK(sc_can_board(tank, ship) && !sc_can_board(wraith, ship));
    /* Right click on the ship; the ship flies over to fetch them. */
    ticcmd_t click = {.order = TC_ORDER, .count = 3, .units = {m1->id, m2->id, tank->id}, .target = ship->id,
                      .position = ship->core.position};
    G_RunTiccmd(0, &click);
    tick(RTS_TICRATE * 5);
    CHECK((m1->sc.flags & m2->sc.flags & tank->sc.flags & SC_LOADED) && sc_cargo_space(ship) == 6);
    CHECK(!sc_can_board(tank2, ship) && !P_CanTarget(zealot, m1));
    /* Load from the ship: it picks up a two-slot Zealot, of any owner's race. */
    ticcmd_t pick = {.order = TC_BOARD, .count = 1, .units = {ship->id}, .target = zealot->id};
    G_RunTiccmd(0, &pick);
    tick(RTS_TICRATE * 5);
    CHECK((zealot->sc.flags & SC_LOADED) && sc_cargo_space(ship) == 8);
    /* Passengers ride along and see nothing of their own. */
    ticcmd_t fly = {.order = TC_MOVE, .count = 1, .units = {ship->id}, .position = fixed3_from_fixed2(FIXED2_LIT(30.5, 30.5), 0)};
    G_RunTiccmd(0, &fly);
    tick(RTS_TICRATE * 12);
    CHECK(fixed2_near(at_of(ship), FIXED2_LIT(30.5, 30.5), FIXED_ONE) && fixed2_near(at_of(m1), at_of(ship), FIXED_ONE / 8));
    int radius = 7;
    CHECK(!G_SightRadius(m1, &radius));
    /* Unload All at a spot: it flies there and sets them down one by one. */
    ticcmd_t drop = {.order = TC_UNLOAD, .count = 1, .units = {ship->id}, .position = fixed3_from_fixed2(FIXED2_LIT(24.5, 30.5), 0)};
    G_RunTiccmd(0, &drop);
    for (int t = 0; t < RTS_TICRATE * 6 && sc_cargo_space(ship) == 8; t++) tick(1);
    /* One at a time: the first is down, the rest still aboard. */
    CHECK(ship->sc.order.kind == SC_ORDER_UNLOAD && sc_cargo_space(ship) > 0 && sc_cargo_space(ship) < 8);
    tick(RTS_TICRATE * 4);
    CHECK(sc_cargo_space(ship) == 0 && ship->sc.order.kind == SC_ORDER_NONE);
    CHECK(!(m1->sc.flags & SC_LOADED) && (m1->traits & MF_SELECTABLE) && fixed2_near(at_of(m1), FIXED2_LIT(24.5, 30.5), FIXED_FROM_INT(3)));
    /* A transport shot down takes its passengers. */
    CHECK(sc_board(m1, ship));
    tick(RTS_TICRATE * 2);
    uint32_t m1_id = m1->id;
    CHECK(m1->sc.flags & SC_LOADED);
    P_DamageMobj(ship, NULL, ship->hp);
    tick(2);
    CHECK(dead(m1_id));
    /* An Overlord carries only with Ventral Sacs. */
    mobj_t *lord = spawn(MT_OVERLORD, FIXED2_LIT(4.5, 20.5), 0), *ling = spawn(MT_ZERGLING, FIXED2_LIT(4.5, 21.5), 0);
    CHECK(!sc_carries(lord) && !sc_can_board(ling, lord));
    upgrade(0, 24);
    CHECK(sc_carries(lord) && sc_board(ling, lord));
    tick(RTS_TICRATE * 2);
    CHECK(ling->sc.flags & SC_LOADED);
    reset();
    return 0;
}

static int transport_unload_failures(void) {
    mobj_t *ship = spawn(MT_DROPSHIP, FIXED2_LIT(20.6, 20.6), 0);
    mobj_t *marine = spawn(MT_MARINE, FIXED2_LIT(20.6, 20.6), 0);
    CHECK(ship && marine && sc_board(marine, ship) && (marine->sc.flags & SC_LOADED));
    ship->sc.timers[SC_TIMER_STASIS] = 3;
    ticcmd_t addressed = {.order = TC_UNLOAD, .count = 1, .units = {ship->id}, .target = marine->id};
    G_RunTiccmd(0, &addressed);
    CHECK(marine->sc.flags & SC_LOADED);
    sc_unload_at(ship, at_of(ship));
    tick(1);
    CHECK(ship->sc.order.kind == SC_ORDER_NONE && (marine->sc.flags & SC_LOADED));
    ship->sc.timers[SC_TIMER_STASIS] = 0;

    memset(level.blocked, 1, (size_t)level.width * level.height);
    for (int y = 18; y <= 22; ++y) for (int x = 24; x <= 27; ++x)
        level.blocked[y * level.width + x] = 0;
    P_NavFree(&level);
    P_NavBeginTick();
    sc_unload_at(ship, at_of(ship));
    tick(1);
    /* Nearby ground is clear, but blocked terrain at the exact origin makes
     * native find_unit_placement fail before displacement search. */
    CHECK(ship->sc.order.kind == SC_ORDER_NONE && (marine->sc.flags & SC_LOADED));

    memset(level.blocked, 0, (size_t)level.width * level.height);
    P_NavFree(&level);
    P_NavBeginTick();
    mobj_t *blocker = spawn(MT_MARINE, at_of(ship), 0);
    CHECK(blocker);
    marine->sc.timers[SC_TIMER_STIM] = RTS_TICRATE;
    int expected_cooldown = sc_modified_cooldown_frames(marine, sc_weapon(sc_units[MT_MARINE - 1].ground_weapon)->cooldown);
    marine->attack.cooldown_left_ms = 0;
    sc_unload_at(ship, at_of(ship));
    tick(1);
    CHECK(!(marine->sc.flags & SC_LOADED));
    CHECK(fixed2_distance_squared64(at_of(marine), at_of(ship)) <= fixed_sq64(SC_PIXELS(128)));
    int x_remainder = (at_of(marine).x / SC_PIXELS(1)) & 7;
    int y_remainder = (at_of(marine).y / SC_PIXELS(1)) & 7;
    CHECK((x_remainder <= 1 || x_remainder >= 7) && (y_remainder <= 1 || y_remainder >= 7));
    /* The modified weapon cooldown runs in its own ground and air timers, not the engine's shared one. */
    CHECK(marine->attack.cooldown_left_ms == 0 && expected_cooldown == 7);
    CHECK(marine->sc.timers[SC_TIMER_GROUND_COOLDOWN] >= expected_cooldown - 1 && marine->sc.timers[SC_TIMER_GROUND_COOLDOWN] <= expected_cooldown);
    CHECK(marine->sc.timers[SC_TIMER_AIR_COOLDOWN] == marine->sc.timers[SC_TIMER_GROUND_COOLDOWN]);
    CHECK(marine->core.state_id == mobjinfo[marine->type_id].spawnstate);
    CHECK(ship->sc.timers[SC_TIMER_MAIN_ORDER] == 15);

    /* A second passenger cannot use an addressed command to bypass the
     * transport's 15 native-frame main-order timer. A replacement Unload All
     * order must preserve the same deadline and unload at expiry. */
    mobj_t *second = spawn(MT_MARINE, at_of(ship), 0);
    CHECK(second && sc_board(second, ship) && (second->sc.flags & SC_LOADED));
    addressed.target = second->id;
    G_RunTiccmd(0, &addressed);
    CHECK(second->sc.flags & SC_LOADED);
    while (ship->sc.timers[SC_TIMER_MAIN_ORDER] > 1) {
        tick(1);
        CHECK(second->sc.flags & SC_LOADED);
    }
    CHECK(ship->sc.timers[SC_TIMER_MAIN_ORDER] == 1);
    G_RunTiccmd(0, &addressed);
    CHECK(second->sc.flags & SC_LOADED);
    ticcmd_t unload_all = {.order = TC_UNLOAD, .count = 1, .units = {ship->id},
                           .position = ship->core.position};
    G_RunTiccmd(0, &unload_all);
    CHECK(ship->sc.timers[SC_TIMER_MAIN_ORDER] == 1 && (second->sc.flags & SC_LOADED));
    while (second->sc.flags & SC_LOADED) tick(1);
    CHECK(!(second->sc.flags & SC_LOADED));
    CHECK(ship->sc.timers[SC_TIMER_MAIN_ORDER] == 15);
    tick(RTS_TICRATE);
    CHECK(ship->sc.order.kind == SC_ORDER_NONE);

    /* Native's northern displacement edge is inclusive: dy=-128 is in the
     * search box even though +128 is outside it. Keep only that passenger
     * footprint traversable so a rejected edge cannot choose another point. */
    reset();
    ship = spawn(MT_DROPSHIP, FIXED2_LIT(20.5, 20.5), 0);
    marine = spawn(MT_MARINE, FIXED2_LIT(20.5, 20.5), 0);
    CHECK(ship && marine && sc_board(marine, ship));
    memset(level.blocked, 1, (size_t)level.width * level.height);
    level.blocked[16 * level.width + 20] = 0;
    P_NavFree(&level);
    P_NavBeginTick();
    blocker = spawn(MT_MARINE, at_of(ship), 1);
    CHECK(blocker);
    sc_unload_at(ship, at_of(ship));
    tick(1);
    CHECK(!(marine->sc.flags & SC_LOADED));
    CHECK(fixed2_cell(at_of(marine)).x == 20 && fixed2_cell(at_of(marine)).y == 16);

    /* A clear passenger origin whose bounding box crosses the map edge must
     * run the displacement search instead of treating the edge as blocked
     * terrain at an otherwise in-map origin. */
    memset(level.blocked, 0, (size_t)level.width * level.height);
    P_NavFree(&level);
    P_NavBeginTick();
    reset();
    ship = spawn(MT_DROPSHIP, FIXED2_LIT(0.1, 10.5), 0);
    marine = spawn(MT_MARINE, FIXED2_LIT(0.1, 10.5), 0);
    CHECK(ship && marine && sc_board(marine, ship));
    sc_unload_at(ship, at_of(ship));
    tick(1);
    CHECK(!(marine->sc.flags & SC_LOADED));
    CHECK(fixed2_cell(at_of(marine)).x >= 0);

    memset(level.blocked, 0, (size_t)level.width * level.height);
    P_NavFree(&level);
    reset();
    return 0;
}

static int transport_unload_queue_completion(void) {
    reset();
    mobj_t *ship = spawn(MT_DROPSHIP, FIXED2_LIT(20.5, 20.5), 0);
    mobj_t *marine = spawn(MT_MARINE, FIXED2_LIT(20.5, 20.5), 0);
    CHECK(ship && marine && sc_board(marine, ship));
    sc_unload_at(ship, at_of(ship));
    ship->sc.queue = (sc_order_queue_t){.active = TC_UNLOAD, .owner = 0, .count = 1,
        .pending = {{.order = TC_MOVE, .position = fixed3_from_fixed2(FIXED2_LIT(26.5, 20.5), 0)}}};
    tick(1);
    CHECK(!(marine->sc.flags & SC_LOADED));
    CHECK(ship->sc.timers[SC_TIMER_MAIN_ORDER] == 15);
    CHECK(ship->sc.order.kind == SC_ORDER_NONE);
    CHECK(ship->sc.queue.active == TC_MOVE && ship->sc.queue.count == 0);
    CHECK(fixed2_equal(ship->movement.goal, FIXED2_LIT(26.5, 20.5)));
    reset();
    return 0;
}

static int storm_unit_box_range(void) {
    fixed2_t center = FIXED2_LIT(10.5, 10.5);
    learn(0, SC_TECH_PSIONIC_STORM);
    mobj_t *templar = spawn(MT_HIGH_TEMPLAR, center, 0);
    mobj_t *range48 = spawn(MT_ULTRALISK, fixed2_add(center, (fixed2_t){SC_PIXELS(67), 0}), 0);
    mobj_t *range49 = spawn(MT_ULTRALISK, fixed2_add(center, (fixed2_t){SC_PIXELS(68), 0}), 0);
    mobj_t *diagonal48 = spawn(MT_ULTRALISK, fixed2_add(center, (fixed2_t){SC_PIXELS(56), SC_PIXELS(51)}), 0);
    mobj_t *diagonal49 = spawn(MT_ULTRALISK, fixed2_add(center, (fixed2_t){SC_PIXELS(57), SC_PIXELS(51)}), 0);
    CHECK(templar && range48 && range49 && diagonal48 && diagonal49);
    templar->sc.energy = 250 << 8;
    templar->traits &= ~(MF_ATTACK | MF_MOBILE);
    range48->traits &= ~(MF_ATTACK | MF_MOBILE);
    range49->traits &= ~(MF_ATTACK | MF_MOBILE);
    diagonal48->traits &= ~(MF_ATTACK | MF_MOBILE);
    diagonal49->traits &= ~(MF_ATTACK | MF_MOBILE);
    int hp48 = range48->hp, hp49 = range49->hp;
    int hp_diagonal48 = diagonal48->hp, hp_diagonal49 = diagonal49->hp;
    CHECK(sc_cast(templar, SC_TECH_PSIONIC_STORM, NULL, center));
    CHECK(range48->hp < hp48 && range49->hp == hp49);
    CHECK(diagonal48->hp < hp_diagonal48 && diagonal49->hp == hp_diagonal49);
    reset();
    return 0;
}

static int nydus(void) {
    mobj_t *hatch = building(MT_HATCHERY, (ivec2_t){4, 4}, 0), *hatch2 = building(MT_HATCHERY, (ivec2_t){30, 30}, 0);
    mobj_t *canal = building(MT_NYDUS_CANAL, (ivec2_t){9, 4}, 0);
    CHECK(hatch && hatch2 && canal && !sc_nydus_other(canal));
    /* The exit is placed like a spell, on creep. */
    cast(canal, SC_TECH_NYDUS_EXIT, NULL, FIXED2_LIT(35, 31));
    mobj_t *exit = sc_nydus_other(canal);
    CHECK(exit && exit->type_id == MT_NYDUS_CANAL && sc_nydus_other(exit) == canal);
    mobj_t *ling = spawn(MT_ZERGLING, FIXED2_LIT(9.5, 8.5), 0);
    ticcmd_t click = {.order = TC_ORDER, .count = 1, .units = {ling->id}, .target = canal->id, .position = canal->core.position};
    G_RunTiccmd(0, &click);
    tick(RTS_TICRATE * 3);
    CHECK(fixed2_near(at_of(ling), at_of(exit), FIXED_FROM_INT(3)) && ling->sc.order.kind == SC_ORDER_NONE);
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
    level.mission = calloc(1, sizeof(sc_mission_t));
    CHECK(level.blocked && level.cell_solid && level.mission && P_InitSight());
    ((sc_mission_t *)level.mission)->rng=2;
    reset();
    CHECK(!research());
    CHECK(!upgrades());
    CHECK(!stim());
    CHECK(!spider_mines());
    CHECK(!burrow());
    CHECK(!arbiter());
    CHECK(!infestation());
    CHECK(!invincible_status_effects());
    CHECK(!storm_unit_box_range());
    CHECK(!transports());
    CHECK(!transport_unload_failures());
    CHECK(!transport_unload_queue_completion());
    CHECK(!nydus());
    free(level.mission);level.mission=NULL;
    P_FreeLevel(&level);
    puts("PASS: Stim Packs, Spider Mines, Burrow, Recall, Stasis Field, Infestation, invincible status effects, Psionic Storm, suicide attacks, speed/sight/cooldown "
         "upgrades and their research, Dropship/Shuttle/Overlord transport and the Nydus Canal");
    return 0;
}
