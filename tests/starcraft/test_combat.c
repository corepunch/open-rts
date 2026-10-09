#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c,"StarCraft combat",#c)
/* The engine weapon model on retail weapons.dat rows: ground and air
 * weapons, damage types against sizes, armor, shields, hits per attack,
 * splash, glaive bounces, cloaking with detection, energy and speeds. */
enum { ULTRALISK = 40, SIEGE_MODE = 31, DARK_TEMPLAR = 75 };

static mobj_t *spawn(int type, fvec2_t at, int owner) {
    mobj_t *u = P_SpawnMobj(fixed3_from_fvec2(at, 0), (uint16_t)type);
    if (u) { u->owner = u->team = (uint8_t)owner; u->allegiance = owner == consoleplayer ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY; }
    return u;
}
static bool shoot(mobj_t *attacker, mobj_t *target) {
    attacker->attack.target = target;
    attacker->attack.cooldown_left_ms = 0;
    return P_Attack(attacker);
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
    CHECK(goliath->air_attack.hits == 2 && goliath->attack.hits == 1 && goliath->air_attack.range == 5);
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
    mobj_t *w = spawn(MT_WRAITH, (fvec2_t){5.5f, 5.5f}, 0);
    mobj_t *marine = spawn(MT_MARINE, (fvec2_t){7.5f, 5.5f}, 1), *bc = spawn(13, (fvec2_t){5.5f, 7.5f}, 1);
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
    mobj_t *vulture = spawn(MT_VULTURE, (fvec2_t){5.5f, 5.5f}, 0), *tank = spawn(MT_SIEGE_TANK, (fvec2_t){5.5f, 9.5f}, 0);
    mobj_t *ultra = spawn(ULTRALISK, (fvec2_t){8.5f, 5.5f}, 1), *ling = spawn(MT_ZERGLING, (fvec2_t){5.5f, 7.5f}, 1);
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
    mobj_t *marine = spawn(MT_MARINE, (fvec2_t){5.5f, 5.5f}, 1), *zealot = spawn(MT_ZEALOT, (fvec2_t){6.5f, 5.5f}, 0);
    mobj_t *ling = spawn(MT_ZERGLING, (fvec2_t){5.5f, 6.5f}, 1);
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
    /* A Shield Battery trades its energy for nearby shields. */
    mobj_t *battery = spawn(MT_SHIELD_BATTERY, (fvec2_t){8.5f, 5.5f}, 0);
    CHECK(battery && sc_energy(battery) == 50);
    zealot->sc.shields = 0;
    for (int t = 0; t < RTS_TICRATE; t++) { ++leveltime; gameinfo->mobj_ticker(battery); }
    CHECK(sc_shields(zealot) >= 48 && sc_energy(battery) < 50);
    reset();
    return 0;
}

static int splash_and_bounce(void) {
    mobj_t *tank = spawn(SIEGE_MODE, (fvec2_t){4.5f, 10.5f}, 0);
    mobj_t *a = spawn(ULTRALISK, (fvec2_t){10.5f, 10.5f}, 1), *b = spawn(ULTRALISK, (fvec2_t){11.4f, 10.5f}, 1);
    mobj_t *c = spawn(ULTRALISK, (fvec2_t){10.5f, 12.0f}, 1), *far = spawn(ULTRALISK, (fvec2_t){10.5f, 14.0f}, 1);
    mobj_t *own = spawn(MT_MARINE, (fvec2_t){10.5f, 9.6f}, 0), *flyer = spawn(MT_MUTALISK, (fvec2_t){10.5f, 10.6f}, 1);
    CHECK(tank && a && b && c && far && own && flyer);
    int hp = a->max_hp, ar = armor(ULTRALISK);
    /* Arclite Shock Cannon: 70 explosive; 50% then 25% beyond the inner radius. */
    CHECK(shoot(tank, a));
    CHECK(hp - a->hp == 70 - ar && hp - b->hp == 35 - ar && hp - c->hp == 17 - ar && far->hp == hp);
    CHECK(own->hp < own->max_hp);          /* radial splash spares nobody on the ground */
    CHECK(flyer->hp == flyer->max_hp);     /* but stays on the target's layer */
    reset();
    /* The Firebat's enemy splash spares its own side. */
    mobj_t *firebat = spawn(MT_FIREBAT, (fvec2_t){5.5f, 5.5f}, 0);
    mobj_t *ling = spawn(MT_ZERGLING, (fvec2_t){6.4f, 5.5f}, 1), *ling2 = spawn(MT_ZERGLING, (fvec2_t){6.4f, 6.0f}, 1);
    mobj_t *marine = spawn(MT_MARINE, (fvec2_t){6.4f, 5.0f}, 0);
    CHECK(firebat && ling && ling2 && marine);
    CHECK(shoot(firebat, ling) && ling->hp == ling->max_hp - 2 * 8 && ling2->hp < ling2->max_hp);
    CHECK(marine->hp == marine->max_hp);
    reset();
    /* The Mutalisk's glaive: 9, then 3, then 1 on the next nearest enemies. */
    mobj_t *muta = spawn(MT_MUTALISK, (fvec2_t){5.5f, 5.5f}, 0);
    mobj_t *m1 = spawn(MT_MARINE, (fvec2_t){7.5f, 5.5f}, 1), *m2 = spawn(MT_MARINE, (fvec2_t){8.5f, 5.5f}, 1);
    mobj_t *m3 = spawn(MT_MARINE, (fvec2_t){10.0f, 5.5f}, 1), *m4 = spawn(MT_MARINE, (fvec2_t){15.0f, 5.5f}, 1);
    CHECK(muta && m1 && m2 && m3 && m4);
    CHECK(shoot(muta, m1));
    CHECK(m1->max_hp - m1->hp == 9 && m2->max_hp - m2->hp == 3 && m3->max_hp - m3->hp == 1 && m4->hp == m4->max_hp);
    reset();
    return 0;
}

static int cloaking(void) {
    CHECK(P_InitSight());
    mobj_t *marine = spawn(MT_MARINE, (fvec2_t){5.5f, 5.5f}, 0), *observer = spawn(MT_OBSERVER, (fvec2_t){8.5f, 5.5f}, 1);
    mobj_t *dt = spawn(DARK_TEMPLAR, (fvec2_t){5.5f, 8.5f}, 1);
    CHECK(marine && observer && dt && (observer->traits & MF_CLOAKED) && (dt->traits & MF_CLOAKED));
    P_UpdateSight();
    /* Unseen and unhit without a detector. */
    CHECK(!P_VisibleTo(marine, observer) && !P_VisibleTo(marine, dt) && !shoot(marine, dt));
    CHECK(P_VisibleTo(dt, marine) && P_VisibleTo(observer, dt)); /* its own side sees it */
    AiUnitInfo info;
    P_AiUnitInfo(NULL, DARK_TEMPLAR, &info);
    CHECK(info.roles & AI_ROLE_CLOAKED);
    /* A Missile Turret's sight detects. */
    mobj_t *turret = spawn(MT_MISSILE_TURRET, (fvec2_t){6.5f, 7.5f}, 0);
    CHECK(turret && (turret->traits & MF_DETECTOR));
    P_UpdateSight();
    CHECK((P_Detectors(dt) & UINT32_C(0x40000000)) && P_VisibleTo(marine, dt));
    int shields = sc_shields(dt);
    CHECK(shoot(marine, dt) && sc_shields(dt) < shields);
    P_RemoveMobj(turret);
    P_UpdateSight();
    CHECK(!P_VisibleTo(marine, dt));
    /* A Ghost cloaks for 25 energy, then drains 13/256 a frame and decloaks. */
    mobj_t *ghost = spawn(MT_GHOST, (fvec2_t){28.5f, 25.5f}, 0), *wraith = spawn(MT_WRAITH, (fvec2_t){28.5f, 28.5f}, 0);
    mobj_t *ling = spawn(MT_ZERGLING, (fvec2_t){30.5f, 25.5f}, 1); /* far from the Observer */
    CHECK(ghost && wraith && ling && sc_energy(ghost) == 50);
    CHECK(!sc_cast(ghost, SC_TECH_CLOAKING_FIELD, NULL, (fvec2_t){0}));
    /* Neither cloak works before its research. */
    CHECK(!sc_cast(ghost, SC_TECH_PERSONNEL_CLOAKING, NULL, (fvec2_t){0}) && !sc_cast(wraith, SC_TECH_CLOAKING_FIELD, NULL, (fvec2_t){0}));
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
    CHECK(sc_cast(wraith, SC_TECH_CLOAKING_FIELD, NULL, (fvec2_t){0}) && (wraith->traits & MF_CLOAKED));
    CHECK(sc_cast(wraith, SC_TECH_CLOAKING_FIELD, NULL, (fvec2_t){0}) && !(wraith->traits & MF_CLOAKED));
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

static int speeds(void) {
    /* flingy.dat top speeds, or the walking script's for iscript movers. */
    CHECK(actor_types[MT_MARINE - 1].speed == 3.0f);
    CHECK(actor_types[MT_VULTURE - 1].speed > 4.9f && actor_types[MT_VULTURE - 1].speed < 5.1f);
    CHECK(actor_types[MT_ZERGLING - 1].speed > 4.1f && actor_types[MT_ZERGLING - 1].speed < 4.2f);
    CHECK(actor_types[ULTRALISK - 1].speed > actor_types[MT_MARINE - 1].speed);
    CHECK(actor_types[43 - 1].speed < 1.0f); /* Overlord */
    for (int i = 0; i < SC_TYPES; i++)
        if ((actor_types[i].traits & MF_MOBILE) && sc_units[i].hp > 0 && sc_units[i].name[0] && actor_types[i].speed <= 0)
            return rts_fail("StarCraft combat", sc_units[i].name);
    mobj_t *ling = spawn(MT_ZERGLING, (fvec2_t){2.5f, 2.5f}, 0), *marine = spawn(MT_MARINE, (fvec2_t){2.5f, 4.5f}, 0);
    CHECK(ling && marine);
    ticcmd_t cmd = {.order = TC_MOVE, .count = 2, .units = {ling->id, marine->id},
                    .position = fixed3_from_fvec2((fvec2_t){28.5f, 3.5f}, 0)};
    G_RunTiccmd(0, &cmd);
    for (int t = 0; t < RTS_TICRATE * 2; t++) P_Ticker();
    CHECK(fixed_to_float(ling->core.position.x) > fixed_to_float(marine->core.position.x) + 1.5f);
    reset();
    return 0;
}

int main(void) {
    G_InitGame();
    P_InitThinkers();
    consoleplayer = 0;
    level.width = level.height = 32;
    level.blocked = calloc(1024, 1);
    level.cell_solid = calloc(1024, 1);
    CHECK(level.blocked && level.cell_solid);
    CHECK(!weapons());
    CHECK(!damage_types());
    CHECK(!shields_and_hits());
    CHECK(!splash_and_bounce());
    CHECK(!cloaking());
    CHECK(!speeds());
    P_FreeLevel(&level);
    puts("PASS: ground and air weapons, damage types, shields and Shield Battery, hits, splash, glaive bounces, "
         "cloaking and detection, energy, flingy speeds");
    return 0;
}
