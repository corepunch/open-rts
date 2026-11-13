#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c,"StarCraft combat",#c)
/* The engine weapon model on retail weapons.dat rows: ground and air
 * weapons, damage types against sizes, armor, shields, hits per attack,
 * splash, glaive bounces, cloaking with detection, energy and speeds. */
enum { ULTRALISK = 40, SIEGE_MODE = 31, DARK_TEMPLAR = 75 };

static mobj_t *spawn(int type, fixed2_t at, int owner) {
    mobj_t *u = P_SpawnMobj(fixed3_from_fixed2(at, 0), (uint16_t)type);
    if (u) { u->owner = u->team = (uint8_t)owner; u->allegiance = owner == consoleplayer ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY; }
    return u;
}
static bool shoot(mobj_t *attacker, mobj_t *target) {
    ((sc_mission_t *)level.mission)->rng=2;
    attacker->attack.target = target;
    attacker->attack.cooldown_left_ms = 0;
    const weapondef_t *weapon=P_MobjWeapon(attacker,target);
    uint32_t previous=level.next_mobj_id;
    if(!P_Attack(attacker))return false;
    if(weapon&&sc_projectile_weapon(weapon->native_id)) {
        mobj_t *shot=P_MobjById(previous+1);
        if(!shot||shot->type_id!=MT_SC_PROJECTILE)return false;
        uint32_t end=level.next_mobj_id;
        for(int t=0;t<RTS_TICRATE*10;t++) {
            bool active=false; ++leveltime;
            for(uint32_t id=previous+1;id<=end;id++) {
                shot=P_MobjById(id);
                if(shot&&shot->type_id==MT_SC_PROJECTILE){active=true;gameinfo->mobj_ticker(shot);}
            }
            if(!active)return true;
        }
        for(uint32_t id=previous+1;id<=end;id++)if(P_MobjById(id))return false;
    }
    return true;
}
static void reset(void) {
    P_FreeThinkers();
    P_InitThinkers();
    memset(level.upgrades, 0, sizeof(level.upgrades));
}
static int armor(int type) { return sc_units[type - 1].armor; }

static int weapons(void) {
    const mobjtype_t *wraith = &actor_types[MT_WRAITH - 1], *goliath = &actor_types[MT_GOLIATH - 1];
    CHECK(!strcmp(sc_weapons[wraith->attack.native_id].name, "Burst Lasers"));
    CHECK(!strcmp(sc_weapons[wraith->air_attack.native_id].name, "Gemini Missiles"));
    CHECK(wraith->attack.damage == 8 && wraith->air_attack.damage == 15);
    CHECK(wraith->attack.targets == MOBJ_TARGET_GROUND && wraith->air_attack.targets == MOBJ_TARGET_AIR);
    /* The Goliath fires its turret's twin autocannons and Hellfire pack (two hits). */
    CHECK(!strcmp(sc_weapons[goliath->attack.native_id].name, "Twin Autocannons"));
    CHECK(!strcmp(sc_weapons[goliath->air_attack.native_id].name, "Hellfire Missile Pack"));
    CHECK(goliath->air_attack.hits == 2 && goliath->attack.hits == 1 && goliath->air_attack.range == FIXED_FROM_INT(5));
    /* One weapons.dat row for both: the Marine keeps a single slot. */
    CHECK(actor_types[MT_MARINE - 1].attack.targets == (MOBJ_TARGET_GROUND | MOBJ_TARGET_AIR) &&
          !actor_types[MT_MARINE - 1].air_attack.damage);
    CHECK(actor_types[MT_ZEALOT - 1].attack.hits == 2 && actor_types[MT_FIREBAT - 1].attack.hits == 2);
    CHECK(actor_types[71 - 1].air_attack.hits == 2); /* Scout: Anti-matter Missiles */
    CHECK(actor_types[SIEGE_MODE - 1].attack.splash == SPLASH_RADIAL);
    CHECK(actor_types[MT_FIREBAT - 1].attack.splash == SPLASH_ENEMY);
    CHECK(actor_types[69 - 1].attack.splash == SPLASH_ENEMY); /* Archon: Psionic Shockwave */
    CHECK(actor_types[MT_MUTALISK - 1].attack.bounces == 2);
    /* Psionic Storm is data until High Templar can cast. */
    CHECK(!strcmp(sc_weapons[84].name, "tPsionic Storm") && sc_weapons[84].splash[0] == 48);
    mobj_t *w = spawn(MT_WRAITH, FIXED2_LIT(5.5, 5.5), 0);
    mobj_t *marine = spawn(MT_MARINE, FIXED2_LIT(7.5, 5.5), 1), *bc = spawn(13, FIXED2_LIT(5.5, 7.5), 1);
    CHECK(w && marine && bc);
    CHECK(P_MobjWeapon(w, marine) == &wraith->attack && P_MobjWeapon(w, bc) == &wraith->air_attack);
    /* Lasers against the ground (normal), missiles against air (explosive on large). */
    int hp = marine->hp;
    CHECK(shoot(w, marine) && hp - marine->hp == 8 - armor(MT_MARINE));
    hp = bc->hp;
    CHECK(shoot(w, bc) && hp - bc->hp == 15 - armor(13));
    /* The AI weighs each slot: Goliath anti-air is the Hellfire pack. */
    AiUnitInfo goliath_ai, wraith_ai;
    P_AiUnitInfo(NULL, MT_GOLIATH, &goliath_ai);
    P_AiUnitInfo(NULL, MT_WRAITH, &wraith_ai);
    CHECK((goliath_ai.roles & AI_ROLE_HITS_AIR) && goliath_ai.air_strength > goliath_ai.ground_strength);
    CHECK((wraith_ai.roles & AI_ROLE_HITS_GROUND) && wraith_ai.air_strength > wraith_ai.ground_strength);
    reset();
    return 0;
}

static int damage_types(void) {
    mobj_t *vulture = spawn(MT_VULTURE, FIXED2_LIT(5.5, 5.5), 0), *tank = spawn(MT_SIEGE_TANK, FIXED2_LIT(5.5, 9.5), 0);
    mobj_t *ultra = spawn(ULTRALISK, FIXED2_LIT(8.5, 5.5), 1), *ling = spawn(MT_ZERGLING, FIXED2_LIT(5.5, 7.5), 1);
    CHECK(vulture && tank && ultra && ling);
    /* Concussive grenades: full on small, a quarter on large after armor. */
    int hp = ling->hp;
    CHECK(shoot(vulture, ling) && hp - ling->hp == 20);
    hp = ultra->hp;
    CHECK(shoot(vulture, ultra) && hp - ultra->hp == (20 - armor(ULTRALISK)) / 4);
    /* Quarter points carry: (20 - 1) / 4 is 4.75, so four volleys take 19. */
    for (int i = 0; i < 3; i++) CHECK(shoot(vulture, ultra));
    CHECK(hp - ultra->hp == (20 - armor(ULTRALISK)));
    /* Explosive shells: half on small. */
    hp = ultra->hp;
    CHECK(shoot(tank, ultra) && hp - ultra->hp == 30 - armor(ULTRALISK));
    ling->hp = hp = 35;
    CHECK(shoot(tank, ling) && hp - ling->hp == 15);
    /* Infantry weapon upgrades add the weapon's bonus before the type scales it. */
    level.upgrades[sc_weapons[tank->info->attack.native_id].upgrade][0].weapon = 2;
    hp = ling->hp;
    CHECK(shoot(tank, ling) && hp - ling->hp == (30 + 2 * 3) / 2);
    reset();
    return 0;
}

static int shields_and_hits(void) {
    mobj_t *marine = spawn(MT_MARINE, FIXED2_LIT(5.5, 5.5), 1), *zealot = spawn(MT_ZEALOT, FIXED2_LIT(6.5, 5.5), 0);
    mobj_t *ling = spawn(MT_ZERGLING, FIXED2_LIT(5.5, 6.5), 1);
    CHECK(marine && zealot && ling && sc_shields(zealot) == 80);
    /* Shields take the whole hit before hit points. */
    CHECK(shoot(marine, zealot) && zealot->hp == zealot->max_hp && sc_shields(zealot) == 74);
    CHECK(zealot->attack.target == marine); /* a shielded hit still provokes */
    /* Plasma Shields is the shields' armor; damage types do not reduce shields. */
    level.upgrades[15][0].weapon = 1;
    CHECK(shoot(marine, zealot) && sc_shields(zealot) == 69);
    zealot->sc.shields = 3 << 8;
    CHECK(shoot(marine, zealot) && sc_shields(zealot) == 0 && zealot->hp == zealot->max_hp - (6 - 1 - 3 - armor(MT_ZEALOT)));
    /* Shields regenerate 7/256 a frame: 24 frames in 30 tics. */
    int before = zealot->sc.shields;
    for (int t = 0; t < RTS_TICRATE; t++) { ++leveltime; gameinfo->mobj_ticker(zealot); }
    CHECK(zealot->sc.shields - before == 7 * 24);
    /* Two psi blades a swing. */
    int hp = ling->hp;
    CHECK(shoot(zealot, ling) && hp - ling->hp == 2 * (8 - armor(MT_ZERGLING)));
    reset();
    mobj_t *battery = spawn(MT_SHIELD_BATTERY, FIXED2_LIT(8.5, 5.5), 0);
    zealot = spawn(MT_ZEALOT, FIXED2_LIT(8.5, 5.5), 0);
    CHECK(battery && zealot);
    sc_start(zealot);
    zealot->sc.shields = 0;
    ticcmd_t right_click = {.order = TC_ORDER, .count = 1, .units = {zealot->id}, .target = battery->id};
    G_RunTiccmd(0, &right_click); /* Battery receives its first command before any ticker initializes energy. */
    CHECK(zealot->sc.order.kind == SC_ORDER_FOLLOW && sc_energy(battery) == 50);

    reset();
    /* Right-clicking a battery starts recharge; mere proximity does not. */
    battery = spawn(MT_SHIELD_BATTERY, FIXED2_LIT(8.5, 5.5), 0);
    zealot = spawn(MT_ZEALOT, FIXED2_LIT(8.5, 5.5), 0);
    CHECK(battery && zealot && sc_energy(battery) == 50);
    zealot->traits &= ~MF_ATTACK;
    sc_start(zealot);
    zealot->sc.shields = 0;
    for (int t = 0; t < RTS_TICRATE; t++) P_Ticker();
    CHECK(sc_shields(zealot) < 2 && sc_energy(battery) >= 50);
    right_click = (ticcmd_t){.order = TC_ORDER, .count = 1, .units = {zealot->id}, .target = battery->id};
    G_RunTiccmd(0, &right_click);
    CHECK(zealot->sc.order.kind == SC_ORDER_FOLLOW && zealot->sc.order.target == battery->id);
    ticcmd_t queued_move = {.order = TC_MOVE, .count = 1, .units = {zealot->id},
        .position = fixed3_from_fixed2(FIXED2_LIT(18.5, 5.5), 0), .queued = true};
    G_RunTiccmd(0, &queued_move);
    CHECK(zealot->sc.queue.count == 1);
    for (int t = 0; t < RTS_TICRATE; t++) P_Ticker();
    CHECK(sc_shields(zealot) == 80 && sc_energy(battery) <= 11);
    CHECK(zealot->sc.order.kind == SC_ORDER_NONE && zealot->sc.queue.active == TC_MOVE &&
          zealot->move_only && P_HasMoveOrder(zealot)); /* Full shields resume the queued Shift-Move. */

    reset();
    battery = spawn(MT_SHIELD_BATTERY, FIXED2_LIT(8.5, 5.5), 0);
    mobj_t *nexus = spawn(MT_NEXUS, FIXED2_LIT(8.5, 5.5), 0);
    CHECK(battery && nexus);
    sc_start(nexus);
    nexus->sc.shields = 0;
    ticcmd_t follow = {.order = TC_FOLLOW, .count = 1, .units = {nexus->id}, .target = battery->id};
    G_RunTiccmd(0, &follow);
    for (int t = 0; t < RTS_TICRATE; t++) P_Ticker();
    CHECK(sc_shields(nexus) == 0 && sc_energy(battery) == 50); /* Grounded buildings are ineligible. */

    reset();
    battery = spawn(MT_SHIELD_BATTERY, FIXED2_LIT(8.5, 5.5), 0);
    mobj_t *carrier = spawn(MT_CARRIER,
        fixed2_add(FIXED2_LIT(8.5, 5.5), (fixed2_t){SC_PIXELS(140), 0}), 0);
    CHECK(battery && carrier && sc_unit_distance(carrier, battery) < 128);
    carrier->traits &= ~MF_ATTACK;
    carrier->production = calloc(1, sizeof(*carrier->production));
    CHECK(carrier->production);
    carrier->production->product_class = RTS_PRODUCT_UNIT;
    carrier->production->product_type = MT_INTERCEPTOR;
    carrier->production->queue_count = 1; /* Completed Carrier while Interceptors are in production. */
    sc_start(carrier);
    carrier->sc.shields = 0;
    right_click = (ticcmd_t){.order = TC_ORDER, .count = 1, .units = {carrier->id}, .target = battery->id};
    G_RunTiccmd(0, &right_click);
    CHECK(carrier->sc.order.kind == SC_ORDER_FOLLOW);
    for (int t = 0; t < RTS_TICRATE / 2; t++) P_Ticker();
    CHECK(sc_shields(carrier) > 0); /* One shared bbox radius governs approach and charging despite 140px centers. */

    reset();
    battery = spawn(MT_SHIELD_BATTERY, FIXED2_LIT(8.5, 5.5), 0);
    zealot = spawn(MT_ZEALOT, FIXED2_LIT(8.5, 5.5), 0);
    CHECK(battery && zealot);
    sc_start(battery); battery->sc.energy = 1 << 8;
    sc_start(zealot); zealot->sc.shields = 0;
    right_click = (ticcmd_t){.order = TC_ORDER, .count = 1, .units = {zealot->id}, .target = battery->id};
    G_RunTiccmd(0, &right_click);
    queued_move.units[0] = zealot->id;
    G_RunTiccmd(0, &queued_move);
    CHECK(zealot->sc.queue.count == 1);
    for (int t = 0; t < 3; t++) P_Ticker();
    CHECK(zealot->sc.order.kind == SC_ORDER_NONE && zealot->sc.queue.active == TC_MOVE &&
          zealot->move_only && P_HasMoveOrder(zealot)); /* Exhausting the battery also completes recharge. */

    reset();
    battery = spawn(MT_SHIELD_BATTERY, FIXED2_LIT(8.5, 5.5), 0);
    zealot = spawn(MT_ZEALOT, FIXED2_LIT(8.5, 5.5), 0);
    CHECK(battery && zealot);
    sc_start(zealot); zealot->sc.shields = 0;
    right_click = (ticcmd_t){.order = TC_ORDER, .count = 1, .units = {zealot->id}, .target = battery->id};
    G_RunTiccmd(0, &right_click);
    queued_move.units[0] = zealot->id;
    G_RunTiccmd(0, &queued_move);
    CHECK(zealot->sc.queue.count == 1);
    P_RemoveMobj(battery);
    for (int t = 0; t < 3; t++) P_Ticker();
    CHECK(zealot->sc.order.kind == SC_ORDER_NONE && zealot->sc.queue.active == TC_MOVE &&
          zealot->move_only && P_HasMoveOrder(zealot)); /* A destroyed battery relinquishes its follower. */
    reset();
    return 0;
}

static int regeneration(void) {
    mobj_t *ling = spawn(MT_ZERGLING, FIXED2_LIT(2, 2), 0);
    mobj_t *hatch = spawn(MT_HATCHERY, FIXED2_LIT(4, 4), 0);
    mobj_t *marine = spawn(MT_MARINE, FIXED2_LIT(6, 6), 0);
    mobj_t *morph = spawn(MT_CREEP_COLONY, FIXED2_LIT(8, 8), 0);
    mobj_t *dead = spawn(MT_ZERGLING, FIXED2_LIT(10, 10), 0);
    CHECK(ling && hatch && marine && morph && dead);
    ling->hp -= 2; hatch->hp -= 2; marine->hp -= 2; morph->hp -= 2;
    ling->sc.flags |= SC_INVINCIBLE | SC_LOADED;
    ling->sc.timers[SC_TIMER_STASIS] = 500;
    morph->production = calloc(1, sizeof(*morph->production));
    CHECK(morph->production);
    morph->production->product_class = RTS_PRODUCT_BUILDING;
    dead->hp = 0;
    int ling_hp = ling->hp, hatch_hp = hatch->hp, marine_hp = marine->hp, morph_hp = morph->hp;
    leveltime = 0;
    for (int t = 0; t < RTS_TICRATE; ++t) {
        ++leveltime;
        gameinfo->mobj_ticker(ling); gameinfo->mobj_ticker(hatch);
        gameinfo->mobj_ticker(marine); gameinfo->mobj_ticker(morph); gameinfo->mobj_ticker(dead);
    }
    CHECK(ling->hp == ling_hp + 1 && ling->sc.wound == 160);
    CHECK(hatch->hp == hatch_hp + 1 && hatch->sc.wound == 160);
    CHECK(marine->hp == marine_hp && !marine->sc.wound && morph->hp == morph_hp && !morph->sc.wound);
    CHECK(dead->hp == 0 && !dead->sc.wound);
    /* Fractional damage is healed first; reaching full health is clamped. */
    hatch->hp = hatch->max_hp; hatch->sc.wound = 3;
    for (int t = 0; t < 2; ++t) { ++leveltime; gameinfo->mobj_ticker(hatch); }
    CHECK(hatch->hp == hatch->max_hp && hatch->sc.wound == 0);
    CHECK(sc_hit(1, NULL, hatch, 1, 2) == 0 && hatch->sc.wound == 128);
    for (int t = 0; t < RTS_TICRATE; ++t) { ++leveltime; gameinfo->mobj_ticker(hatch); }
    CHECK(hatch->hp == hatch->max_hp && hatch->sc.wound == 32);
    reset();
    return 0;
}

static int burning(void) {
    mobj_t *low = spawn(MT_BARRACKS, FIXED2_LIT(2, 2), 0);
    mobj_t *safe = spawn(MT_BARRACKS, FIXED2_LIT(4, 4), 0);
    mobj_t *flying = spawn(MT_BARRACKS, FIXED2_LIT(6, 6), 0);
    mobj_t *protected = spawn(MT_BARRACKS, FIXED2_LIT(8, 8), 0);
    mobj_t *zerg = spawn(MT_HATCHERY, FIXED2_LIT(10, 10), 0);
    mobj_t *rounded = spawn(MT_FACTORY, FIXED2_LIT(12, 12), 0);
    mobj_t *above = spawn(MT_FACTORY, FIXED2_LIT(14, 14), 0);
    CHECK(low && safe && flying && protected && zerg && rounded && above);
    low->max_hp = safe->max_hp = flying->max_hp = protected->max_hp = zerg->max_hp = 100;
    low->hp = flying->hp = protected->hp = zerg->hp = 33; safe->hp = 34;
    flying->traits |= MF_FLY;
    protected->sc.flags |= SC_INVINCIBLE;
    rounded->max_hp = above->max_hp = 1000;
    rounded->hp = 339; above->hp = 340;
    CHECK(sc_hit(1, NULL, protected, 10, 1) == 0);
    leveltime = 0;
    for (int t = 0; t < RTS_TICRATE; ++t) {
        ++leveltime;
        gameinfo->mobj_ticker(low); gameinfo->mobj_ticker(safe); gameinfo->mobj_ticker(flying);
        gameinfo->mobj_ticker(protected); gameinfo->mobj_ticker(zerg);
        gameinfo->mobj_ticker(rounded); gameinfo->mobj_ticker(above);
    }
    CHECK(low->hp == 32 && low->sc.wound == 224);
    CHECK(flying->hp == 32 && flying->sc.wound == 224);
    CHECK(protected->hp == 32 && protected->sc.wound == 224);
    CHECK(safe->hp == 34 && !safe->sc.wound && zerg->hp >= 33);
    CHECK(rounded->hp == 338 && rounded->sc.wound == 224 && above->hp == 340 && !above->sc.wound);
    /* Repair above the integer-percent threshold ends burning. */
    low->hp = 34; low->sc.wound = 0;
    for (int t = 0; t < RTS_TICRATE; ++t) { ++leveltime; gameinfo->mobj_ticker(low); }
    CHECK(low->hp == 34 && !low->sc.wound);
    protected->hp = 1; protected->sc.wound = 0;
    for (int t = 0; t < RTS_TICRATE && protected->hp > 0; ++t) { ++leveltime; gameinfo->mobj_ticker(protected); }
    CHECK(protected->hp == 0);
    reset();
    /* Every real Terran DAT building, including add-ons, shares the rule. */
    for (int type = 1; type <= SC_TYPES; ++type) {
        const sc_unit_t *u = &sc_units[type - 1];
        if (!(u->race & 2) || !(u->flags & SC_UNIT_BUILDING) || u->hp <= 0) continue;
        mobj_t *building = spawn(type, FIXED2_LIT(4, 4), 0);
        CHECK(building);
        building->hp = building->max_hp / 4;
        leveltime = 2; /* one 24 Hz frame */
        gameinfo->mobj_ticker(building);
        CHECK(building->sc.wound == 20);
        reset();
    }
    return 0;
}

static int splash_and_bounce(void) {
    mobj_t *tank = spawn(SIEGE_MODE, FIXED2_LIT(4.5, 10.5), 0);
    mobj_t *a = spawn(ULTRALISK, FIXED2_LIT(10.5, 10.5), 1), *b = spawn(ULTRALISK, FIXED2_LIT(11.4, 10.5), 1);
    mobj_t *c = spawn(ULTRALISK, FIXED2_LIT(10.5, 12.0), 1), *far = spawn(ULTRALISK, FIXED2_LIT(10.5, 14.0), 1);
    mobj_t *own = spawn(MT_MARINE, FIXED2_LIT(10.5, 9.6), 0), *flyer = spawn(MT_MUTALISK, FIXED2_LIT(10.5, 10.6), 1);
    CHECK(tank && a && b && c && far && own && flyer);
    int hp = a->max_hp, ar = armor(ULTRALISK);
    /* The source-facing impact puts b in the middle radius and c in the outer radius. */
    CHECK(shoot(tank, a));
    CHECK(hp - a->hp == 70 - ar && hp - b->hp == 35 - ar && hp - c->hp == 17 - ar && far->hp == hp);
    CHECK(own->hp < own->max_hp);          /* radial splash spares nobody on the ground */
    CHECK(flyer->hp == flyer->max_hp);     /* but stays on the target's layer */
    reset();
    /* The Firebat's enemy splash spares its own side. */
    mobj_t *firebat = spawn(MT_FIREBAT, FIXED2_LIT(5.5, 5.5), 0);
    mobj_t *ling = spawn(MT_ZERGLING, FIXED2_LIT(6.4, 5.5), 1), *ling2 = spawn(MT_ZERGLING, FIXED2_LIT(6.4, 6.0), 1);
    mobj_t *marine = spawn(MT_MARINE, FIXED2_LIT(6.4, 5.0), 0);
    CHECK(firebat && ling && ling2 && marine);
    /* Flames appear 24, 52 and 80 pixels ahead (no scripts here, so all at once): the first is
     * inside the target's box (8), the second 16 pixels off its east edge (medium, 4). */
    CHECK(shoot(firebat, ling) && ling->hp == ling->max_hp - (8 + 4) && ling2->hp < ling2->max_hp);
    CHECK(marine->hp == marine->max_hp);
    reset();
    /* The Mutalisk's glaive: 9, then 3, then 1 on the next nearest enemies. */
    mobj_t *muta = spawn(MT_MUTALISK, FIXED2_LIT(5.5, 5.5), 0);
    mobj_t *m1 = spawn(MT_MARINE, FIXED2_LIT(7.5, 5.5), 1), *m2 = spawn(MT_MARINE, FIXED2_LIT(8.5, 5.5), 1);
    mobj_t *m3 = spawn(MT_MARINE, FIXED2_LIT(10.0, 5.5), 1), *m4 = spawn(MT_MARINE, FIXED2_LIT(15.0, 5.5), 1);
    CHECK(muta && m1 && m2 && m3 && m4);
    CHECK(shoot(muta, m1));
    CHECK(m1->max_hp - m1->hp == 9 && m2->max_hp - m2->hp == 3 && m3->max_hp - m3->hp == 1 && m4->hp == m4->max_hp);
    reset();
    return 0;
}

static int cloaking(void) {
    CHECK(P_InitSight());
    mobj_t *marine = spawn(MT_MARINE, FIXED2_LIT(5.5, 5.5), 0), *observer = spawn(MT_OBSERVER, FIXED2_LIT(8.5, 5.5), 1);
    mobj_t *dt = spawn(DARK_TEMPLAR, FIXED2_LIT(5.5, 8.5), 1);
    CHECK(marine && observer && dt && (observer->traits & MF_CLOAKED) && (dt->traits & MF_CLOAKED));
    P_UpdateSight();
    /* Unseen and unhit without a detector. */
    CHECK(!P_VisibleTo(marine, observer) && !P_VisibleTo(marine, dt) && !shoot(marine, dt));
    CHECK(P_VisibleTo(dt, marine) && P_VisibleTo(observer, dt)); /* its own side sees it */
    AiUnitInfo info;
    P_AiUnitInfo(NULL, DARK_TEMPLAR, &info);
    CHECK(info.roles & AI_ROLE_CLOAKED);
    /* A Missile Turret's sight detects. */
    mobj_t *turret = spawn(MT_MISSILE_TURRET, FIXED2_LIT(6.5, 7.5), 0);
    CHECK(turret && (turret->traits & MF_DETECTOR));
    P_UpdateSight();
    CHECK((P_Detectors(dt) & UINT32_C(0x40000000)) && P_VisibleTo(marine, dt));
    int shields = sc_shields(dt);
    CHECK(shoot(marine, dt) && sc_shields(dt) < shields);
    P_RemoveMobj(turret);
    P_UpdateSight();
    CHECK(!P_VisibleTo(marine, dt));
    /* A Ghost cloaks for 25 energy, then drains 13/256 a frame and decloaks. */
    mobj_t *ghost = spawn(MT_GHOST, FIXED2_LIT(28.5, 25.5), 0), *wraith = spawn(MT_WRAITH, FIXED2_LIT(28.5, 28.5), 0);
    mobj_t *ling = spawn(MT_ZERGLING, FIXED2_LIT(30.5, 25.5), 1); /* far from the Observer */
    CHECK(ghost && wraith && ling && sc_energy(ghost) == 50);
    CHECK(!sc_cast(ghost, SC_TECH_CLOAKING_FIELD, NULL, (fixed2_t){0}));
    /* Neither cloak works before its research. */
    CHECK(!sc_cast(ghost, SC_TECH_PERSONNEL_CLOAKING, NULL, (fixed2_t){0}) && !sc_cast(wraith, SC_TECH_CLOAKING_FIELD, NULL, (fixed2_t){0}));
    level.upgrades[SC_UPGRADES + SC_TECH_PERSONNEL_CLOAKING][0].weapon = 1;
    level.upgrades[SC_UPGRADES + SC_TECH_CLOAKING_FIELD][0].weapon = 1;
    ticcmd_t cmd = {.order = TC_SPELL, .count = 1, .units = {ghost->id}, .product = SC_TECH_PERSONNEL_CLOAKING};
    G_RunTiccmd(0, &cmd);
    CHECK((ghost->traits & MF_CLOAKED) && sc_energy(ghost) == 25);
    P_UpdateSight();
    CHECK(!P_VisibleTo(ling, ghost) && P_VisibleTo(marine, ghost));
    int energy = ghost->sc.energy;
    for (int t = 0; t < RTS_TICRATE; t++) { ++leveltime; gameinfo->mobj_ticker(ghost); }
    CHECK(energy - ghost->sc.energy == 13 * 24);
    while (ghost->traits & MF_CLOAKED) { ++leveltime; gameinfo->mobj_ticker(ghost); }
    CHECK(ghost->sc.energy == 0);
    /* Casting again switches it off; the Wraith uses Cloaking Field. */
    CHECK(sc_cast(wraith, SC_TECH_CLOAKING_FIELD, NULL, (fixed2_t){0}) && (wraith->traits & MF_CLOAKED));
    CHECK(sc_cast(wraith, SC_TECH_CLOAKING_FIELD, NULL, (fixed2_t){0}) && !(wraith->traits & MF_CLOAKED));
    /* Uncloaked casters regain 8/256 a frame; the AI adds half their energy. */
    energy = wraith->sc.energy;
    for (int t = 0; t < RTS_TICRATE; t++) { ++leveltime; gameinfo->mobj_ticker(wraith); }
    CHECK(wraith->sc.energy - energy == 8 * 24);
    AiUnitInfo plain, live;
    P_AiUnitInfo(NULL, MT_WRAITH, &plain);
    live = plain;
    G_AiInterface()->describe_unit(wraith, &live);
    CHECK(live.ground_strength == plain.ground_strength + sc_energy(wraith) / 2);
    reset();
    free(level.sight.cells);
    level.sight.cells = NULL;
    return 0;
}

/* A Goliath facing north turns its turret, not its hull, to a Marine on
 * its right, fires only once the turret is on it, and turns the turret
 * back once the Marine is gone. */
static int turrets(void) {
    const mobjtype_t *goliath = &actor_types[MT_GOLIATH - 1];
    CHECK(goliath->turret.sprite_name && goliath->turret.turn_step > 0 && goliath->turret.fire_frame < 0);
    /* Both Siege Tank modes have one; a Missile Turret is its own picture. */
    CHECK(actor_types[MT_SIEGE_TANK - 1].turret.sprite_name && actor_types[SIEGE_MODE - 1].turret.sprite_name);
    CHECK(!actor_types[MT_MISSILE_TURRET - 1].turret.sprite_name && !actor_types[MT_MARINE - 1].turret.sprite_name);
    CHECK(P_InitSight());
    mobj_t *g = spawn(MT_GOLIATH, FIXED2_LIT(5.5, 5.5), 0), *marine = spawn(MT_MARINE, FIXED2_LIT(8.5, 5.5), 1);
    CHECK(g && marine);
    marine->traits &= ~MF_ATTACK;
    angle_t north = P_PointToAngle(0, -FIXED_ONE), east = P_PointToAngle(FIXED_ONE, 0);
    g->core.angle = north;
    int fired_at = -1;
    for (int t = 0; t < 5 * RTS_TICRATE && fired_at < 0; t++) {
        P_Ticker();
        if (gameinfo->states[g->core.state_id].group == 3 || marine->hp < marine->max_hp) fired_at = t;
        else if (t == 0 && goliath->turret.turn_step + ANG45 / 8u < ANG90)
            CHECK(angle_distance(g->core.angle + g->attack.aim, east) >= ANG45 / 8u); /* still turning */
    }
    CHECK(fired_at >= 0);
    CHECK(g->core.angle == north);
    CHECK(angle_distance(g->core.angle + g->attack.aim, east) < ANG45 / 8u);
    P_RemoveMobj(marine);
    for (int t = 0; t < RTS_TICRATE; t++) P_Ticker();
    CHECK(g->attack.aim == 0);
    reset();
    free(level.sight.cells);
    level.sight.cells = NULL;
    return 0;
}

static int speeds(void) {
    /* flingy.dat top speeds, or the walking script's for iscript movers. */
    CHECK(actor_types[MT_MARINE - 1].speed == FIXED_FROM_INT(3));
    CHECK(actor_types[MT_VULTURE - 1].speed > FIXED_LIT(4.9) && actor_types[MT_VULTURE - 1].speed < FIXED_LIT(5.1));
    CHECK(actor_types[MT_ZERGLING - 1].speed > FIXED_LIT(4.1) && actor_types[MT_ZERGLING - 1].speed < FIXED_LIT(4.2));
    CHECK(actor_types[ULTRALISK - 1].speed > actor_types[MT_MARINE - 1].speed);
    CHECK(actor_types[43 - 1].speed < FIXED_ONE); /* Overlord */
    for (int i = 0; i < SC_TYPES; i++)
        if ((actor_types[i].traits & MF_MOBILE) && sc_units[i].hp > 0 && sc_units[i].name[0] && actor_types[i].speed <= 0)
            return rts_fail("StarCraft combat", sc_units[i].name);
    mobj_t *ling = spawn(MT_ZERGLING, FIXED2_LIT(2.5, 2.5), 0), *marine = spawn(MT_MARINE, FIXED2_LIT(2.5, 4.5), 0);
    CHECK(ling && marine);
    ticcmd_t cmd = {.order = TC_MOVE, .count = 2, .units = {ling->id, marine->id},
                    .position = fixed3_from_fixed2(FIXED2_LIT(28.5, 3.5), 0)};
    G_RunTiccmd(0, &cmd);
    for (int t = 0; t < RTS_TICRATE * 2; t++) P_Ticker();
    CHECK(ling->core.position.x > marine->core.position.x + FIXED_LIT(1.5));
    reset();
    return 0;
}

static int immediate_misses(void) {
    sc_mission_t *mission=level.mission;
    mobj_t *marine=spawn(MT_MARINE,FIXED2_LIT(5,5),0),*target=spawn(MT_MARINE,FIXED2_LIT(8,5),1);
    CHECK(marine&&target);marine->attack.target=target;marine->core.angle=0;
    mission->rng=0;CHECK(P_Attack(marine));
    CHECK(target->hp==target->max_hp&&!target->attack.target); /* roll zero misses without provocation */
    weapondef_t twin=marine->info->attack;twin.hits=2;mission->rng=0;
    CHECK(sc_projectile_fire(marine,&twin,target));
    CHECK(target->hp==target->max_hp-6); /* first roll misses; the second hits */
    target->hp=target->max_hp;target->attack.target=NULL;
    mission->terrain=calloc((size_t)level.width*level.height,sizeof(*mission->terrain));CHECK(mission->terrain);
    sc_terrain_t *tile=&mission->terrain[L_Index(&level,8,5)];
    tile->cv5=SC_TERRAIN_COVER;mission->rng=1;marine->attack.cooldown_left_ms=0;
    CHECK(P_Attack(marine)&&target->hp==target->max_hp);
    tile->cv5=0;tile->vf4[0]=SC_TERRAIN_HIGH;mission->rng=1;marine->attack.cooldown_left_ms=0;
    CHECK(P_Attack(marine)&&target->hp==target->max_hp);
    tile->vf4[0]=0;mission->rng=2;marine->attack.cooldown_left_ms=0;
    CHECK(P_Attack(marine)&&target->hp==target->max_hp-6&&target->attack.target==marine);
    free(mission->terrain);mission->terrain=NULL;reset();

    mobj_t *tank=spawn(SIEGE_MODE,FIXED2_LIT(4,8),0);
    target=spawn(MT_MARINE,FIXED2_LIT(10,8),1);
    mobj_t *near=spawn(MT_MARINE,(fixed2_t){FIXED_FROM_INT(10)-SC_PIXELS(30),FIXED_FROM_INT(8)},1);
    CHECK(tank&&target&&near);tank->core.angle=0;tank->attack.target=target;mission->rng=0;
    CHECK(P_Attack(tank));
    CHECK(near->hp==5&&target->hp==32); /* explosive damage on small units, centered on miss */
    reset();

    tank=spawn(SIEGE_MODE,FIXED2_LIT(4,8),0);
    target=spawn(MT_MARINE,FIXED2_LIT(10,8),1);
    near=spawn(MT_MARINE,(fixed2_t){FIXED_FROM_INT(10)-SC_PIXELS(52),FIXED_FROM_INT(8)},1);
    CHECK(tank&&target&&near);tank->core.angle=0;tank->attack.target=target;mission->rng=2;
    CHECK(P_Attack(tank)&&near->hp==32); /* target's source-facing inner edge, not its centre */
    reset();

    mobj_t *firebat=spawn(MT_FIREBAT,FIXED2_LIT(5,5),0);
    target=spawn(MT_ZERGLING,FIXED2_LIT(5.8,5),1);
    mobj_t *cloud=spawn(MT_DARK_SWARM,FIXED2_LIT(5.8,5),1);
    CHECK(firebat&&target&&cloud);cloud->sc.order.tech=SC_TECH_DARK_SWARM;cloud->sc.timers[SC_TIMER_LIFE]=100;
    CHECK(shoot(firebat,target)&&target->hp<target->max_hp); /* source-appearing flame bypasses miss roll */
    reset();return 0;
}

int main(void) {
    G_InitGame();
    P_InitThinkers();
    consoleplayer = 0;
    level.width = level.height = 32;
    level.blocked = calloc(1024, 1);
    level.cell_solid = calloc(1024, 1);
    level.mission = calloc(1, sizeof(sc_mission_t));
    CHECK(level.blocked && level.cell_solid && level.mission);
    CHECK(!weapons());
    CHECK(!damage_types());
    CHECK(!shields_and_hits());
    CHECK(!regeneration());
    CHECK(!burning());
    CHECK(!splash_and_bounce());
    CHECK(!cloaking());
    CHECK(!turrets());
    CHECK(!speeds());
    CHECK(!immediate_misses());
    free(level.mission); level.mission=NULL;
    P_FreeLevel(&level);
    puts("PASS: ground and air weapons, damage types, shields and Shield Battery, hits, splash, glaive bounces, "
         "cloaking and detection, energy, turrets, flingy speeds");
    return 0;
}
