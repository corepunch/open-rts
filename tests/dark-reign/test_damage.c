#include "t_local.h"
#include "engine.h"
#include "dark-reign.h"
#include "info.h"
#include <string.h>

#define CHECK(c) RTS_CHECK(c, "Dark Reign damage", #c)

/* Offense and class indices follow DAMAGE.TXT's declaration order. */
enum { S1 = 22, C1 = 3, E3 = 6, G2 = 11, K1 = 14 };
enum { TOUGH_HUMAN = 0, TOUGH_HUMAN_WET = 1, TANK_PLATING_WET = 6, BUILDING_ARMOUR = 7, SUPER_ARMOUR = 8 };

/* dkreign.exe 0x474b40 with the rolls pinned: a variation roll of V gives no
 * spread, a crit roll of 99 never crits. */
static int formula(void) {
    bool crit;
    /* Sniper rifle (S1 150): full against humans, 10% against tank plating. */
    CHECK(DR_VulnerableDamage(S1, TOUGH_HUMAN_WET, 100, 150, 100, 99, 10, &crit) == 150 && !crit);
    CHECK(DR_VulnerableDamage(S1, TANK_PLATING_WET, 100, 150, 100, 99, 10, &crit) == 15);
    CHECK(DR_VulnerableDamage(S1, BUILDING_ARMOUR, 100, 150, 100, 99, 10, &crit) == 15);
    /* Variation is uniform -V..V-1 percentage points. */
    CHECK(DR_VulnerableDamage(S1, TOUGH_HUMAN_WET, 100, 150, 100, 99, 0, &crit) == 135);
    CHECK(DR_VulnerableDamage(S1, TOUGH_HUMAN_WET, 100, 150, 100, 99, 19, &crit) == 163);
    /* Chaff does nothing to ground armour; SuperArmour takes nothing. */
    CHECK(DR_VulnerableDamage(C1, TANK_PLATING_WET, 100, 8, 100, 99, 0, &crit) == 0);
    CHECK(DR_VulnerableDamage(E3, SUPER_ARMOUR, 100, 180, 100, 99, 0, &crit) == 0);
    /* Buildings default to half (E3 40%). */
    CHECK(DR_VulnerableDamage(E3, BUILDING_ARMOUR, 100, 180, 100, 99, 10, &crit) == 72);
    /* The percentage truncates before it multiplies: 40% x strength 75 = 30%,
     * and 10% x 75 = 7% (7.5 truncated): 7 of 100, not 8. */
    CHECK(DR_VulnerableDamage(E3, BUILDING_ARMOUR, 75, 100, 100, 99, 10, &crit) == 30);
    CHECK(DR_VulnerableDamage(S1, BUILDING_ARMOUR, 75, 100, 100, 99, 10, &crit) == 7);
    /* Critical hits (G2 3%, K1 2% on humans) report a kill, not extra damage. */
    CHECK(DR_VulnerableDamage(G2, TOUGH_HUMAN, 100, 50, 100, 2, 10, &crit) == 50 && crit);
    CHECK(DR_VulnerableDamage(G2, TOUGH_HUMAN, 100, 50, 100, 3, 10, &crit) == 40 && !crit);
    CHECK(DR_VulnerableDamage(K1, TOUGH_HUMAN, 100, 50, 100, 1, 5, &crit) == 50 && crit);
    /* Unknown categories leave the weapon's damage alone. */
    CHECK(DR_VulnerableDamage(-1, TOUGH_HUMAN, 100, 50, 100, 0, 0, &crit) == 50 && !crit);
    printf("PASS: DAMAGE.TXT vulnerability formula\n");
    return 0;
}

/* Every actor has a SetVulnerability row, every armed one an offense. */
static int coverage(void) {
    int armed = 0;
    for (int i = 0; i < num_actor_types; ++i) {
        const mobjtype_t *type = &actor_types[i];
        mobj_t probe = {0};
        probe.type_id = type->id;
        if (DR_DefenseOf(&probe) < 0) {
            fprintf(stderr, "no defense: %s (%d)\n", type->name, mobjinfo[type->id].doomednum);
            CHECK(0);
        }
        if ((type->traits & MF_ATTACK) && type->attack.damage > 0) {
            if (DR_OffenseOf(&probe) < 0) {
                fprintf(stderr, "no offense: %s\n", type->name);
                CHECK(0);
            }
            ++armed;
        }
    }
    CHECK(armed > 30);
    printf("PASS: %d armed actors carry a retail offense\n", armed);
    return 0;
}

static mobj_t *spawn(uint16_t type, int owner, fixed2_t position) {
    mobj_t *unit = P_SpawnMobj(fixed3_from_fixed2(position, 0), type);
    if (unit) {
        unit->owner = unit->team = (uint8_t)owner;
        unit->allegiance = owner ? ALLEGIANCE_ENEMY : ALLEGIANCE_PLAYER;
    }
    return unit;
}

/* Through the engine hook: snipers barely scratch tanks, kill raiders fast. */
static int in_game(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {0};
    CHECK(model && rts_game_model_load(model, &config));
    P_FreeThinkers();
    P_InitThinkers();
    CHECK(gameinfo->hit_damage != NULL);
    mobj_t *sniper = spawn(MT_FG_SNIPER, 0, FIXED2_LIT(20, 20));
    mobj_t *tank = spawn(MT_FG_TANK_HUNTER, 1, FIXED2_LIT(22, 20));
    mobj_t *raider = spawn(MT_FG_RAIDER, 1, FIXED2_LIT(20, 22));
    CHECK(sniper && tank && raider);
    const weapondef_t *rifle = &sniper->info->attack;
    int to_tank = gameinfo->hit_damage(sniper, rifle, tank, rifle->damage, 1);
    int to_raider = gameinfo->hit_damage(sniper, rifle, raider, rifle->damage, 1);
    CHECK(to_tank <= 28); /* 0..19% of 150 */
    CHECK(to_raider >= 135);
    /* Chaff never touches ground units. */
    mobj_t *flak = spawn(MT_FG_MAD, 0, FIXED2_LIT(24, 24));
    CHECK(flak && gameinfo->hit_damage(flak, &flak->info->attack, tank, 8, 1) == 0);
    rts_game_model_destroy(model);
    printf("PASS: hits use the attacker's offense and the target's armour\n");
    return 0;
}

static RtsGameModel *fresh(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {0};
    if (!model || !rts_game_model_load(model, &config)) return NULL;
    P_FreeThinkers();
    P_InitThinkers();
    memset(level.dr_effects, 0, sizeof(level.dr_effects));
    return model;
}

static int active_effects(void) {
    int n = 0;
    for (int i = 0; i < DR_MAX_EFFECTS; ++i) n += level.dr_effects[i].remaining != 0;
    return n;
}

/* F = (R^2 - d^2) * 100 / R^2; zero at and past R; a point blast only at d 0. */
static int falloff(void) {
    CHECK(DR_Falloff(100, 0) == 100);
    CHECK(DR_Falloff(100, 50) == 75);
    CHECK(DR_Falloff(100, 43) == 81);
    CHECK(DR_Falloff(100, 100) == 0 && DR_Falloff(100, 150) == 0);
    CHECK(DR_Falloff(100, -5) == 100);
    CHECK(DR_Falloff(0, 0) == 100 && DR_Falloff(0, 1) == 0);
    printf("PASS: quadratic area falloff\n");
    return 0;
}

/* An area shell hurts what stands near the target, not what stands clear. */
static int splash(void) {
    RtsGameModel *model = fresh();
    CHECK(model && gameinfo->attack);
    mobj_t *spa = spawn(MT_FG_SPA, 0, FIXED2_LIT(10, 10));
    mobj_t *target = spawn(MT_FG_TANK_HUNTER, 1, FIXED2_LIT(30, 30));
    mobj_t *near = spawn(MT_FG_TANK_HUNTER, 1, FIXED2_LIT(31, 30));
    mobj_t *far = spawn(MT_FG_TANK_HUNTER, 1, FIXED2_LIT(36, 30));
    CHECK(spa && target && near && far);
    CHECK(DR_Attack(spa, &spa->info->attack, target));
    CHECK(target->hp < target->max_hp && near->hp < near->max_hp);
    CHECK(far->hp == far->max_hp);
    CHECK(spa->hp == spa->max_hp);
    /* Heals stay on the engine's support path. */
    mobj_t *medic = spawn(MT_FG_MEDIC, 0, FIXED2_LIT(12, 12));
    CHECK(medic && !DR_Attack(medic, &medic->info->attack, spa));
    rts_game_model_destroy(model);
    printf("PASS: area impacts reach neighbours inside the blast only\n");
    return 0;
}

/* A Martyr's SuicideNuke (E3 180, area 100) kills its target and itself. */
static int martyr(void) {
    RtsGameModel *model = fresh();
    CHECK(model);
    mobj_t *martyr = spawn(MT_FG_MARTYR, 0, FIXED2_LIT(20, 20));
    mobj_t *raider = spawn(MT_FG_RAIDER, 1, FIXED2_LIT(22, 20));
    CHECK(martyr && raider);
    CHECK(DR_Attack(martyr, &martyr->info->attack, raider));
    CHECK(raider->hp <= 0 && martyr->hp <= 0);
    rts_game_model_destroy(model);
    printf("PASS: a Martyr dies in its own blast\n");
    return 0;
}

/* The SCARAB shell's PersistentDamage(5 10 E3 2 90) strikes five more
 * times, ten cycles apart, where it landed; it is saved and hashed. */
static int lingering(void) {
    RtsGameModel *model = fresh();
    CHECK(model);
    mobj_t *scarab = spawn(MT_IMP_SCARAB, 0, FIXED2_LIT(10, 40));
    mobj_t *raider = spawn(MT_FG_RAIDER, 1, FIXED2_LIT(40, 40));
    CHECK(scarab && raider);
    uint32_t before = G_ConsistencyExtra(0);
    CHECK(DR_Attack(scarab, &scarab->info->attack, raider));
    CHECK(active_effects() == 1);
    CHECK(G_ConsistencyExtra(0) != before);
    int hp = raider->hp;
    CHECK(hp > 0 && hp < raider->max_hp);
    for (int tic = 0; tic < 9; ++tic) DR_TickEffects();
    CHECK(raider->hp == hp);
    DR_TickEffects();
    CHECK(raider->hp < hp);
    /* Save and reload mid-blast. */
    CHECK(G_SaveExtraSize() == sizeof(level.dr_effects));
    uint8_t saved[sizeof(level.dr_effects)];
    G_SaveExtra(saved);
    memset(level.dr_effects, 0, sizeof(level.dr_effects));
    CHECK(G_LoadExtra(saved, sizeof(saved)) && active_effects() == 1);
    CHECK(!G_LoadExtra(saved, sizeof(saved) - 1));
    /* It keeps burning if the shooter dies, and ends after five strikes. */
    P_DamageMobj(scarab, NULL, scarab->hp);
    for (int tic = 0; tic < 40; ++tic) DR_TickEffects();
    CHECK(active_effects() == 0);
    CHECK(raider->hp <= hp - 4);
    /* Chaff lingers among flyers and never touches the ground. */
    mobj_t *flak = spawn(MT_FG_MAD, 0, FIXED2_LIT(10, 10));
    mobj_t *bike = spawn(MT_FG_SKY_BIKE, 1, FIXED2_LIT(14, 10));
    mobj_t *ground = spawn(MT_FG_RAIDER, 1, FIXED2_LIT(14, 10));
    CHECK(flak && bike && ground && (bike->traits & MF_FLY));
    CHECK(DR_Attack(flak, &flak->info->attack, bike));
    int bike_hp = bike->hp;
    for (int tic = 0; tic < 50; ++tic) DR_TickEffects();
    CHECK(bike->hp < bike_hp && ground->hp == ground->max_hp);
    rts_game_model_destroy(model);
    printf("PASS: PersistentDamage re-strikes on its cadence, survives save/load and keeps its layer\n");
    return 0;
}

/* QSELFDESTRUCT: an order, the unit dies, the blast scales with its health. */
static int self_destruct(void) {
    RtsGameModel *model = fresh();
    CHECK(model);
    mobj_t *tank = spawn(MT_IMP_TACHYON_TANK, 0, FIXED2_LIT(50, 50));
    mobj_t *victim = spawn(MT_IMP_TACHYON_TANK, 1, FIXED2_LIT(51, 50));
    mobj_t *raider = spawn(MT_FG_RAIDER, 0, FIXED2_LIT(52, 52));
    CHECK(tank && victim && raider);
    CHECK(DR_CanSelfDestruct(tank) && !DR_CanSelfDestruct(raider));
    ticcmd_t cmd = {.order = TC_SPELL, .product = DR_SPELL_SELF_DESTRUCT};
    mobj_t *units[] = {tank, raider};
    CHECK(G_GameUnitCommand(0, &cmd, units, 2, NULL));
    CHECK(tank->hp <= 0);
    CHECK(raider->hp > 0);
    CHECK(victim->hp < victim->max_hp);
    /* A wrecked tank (under 1% health) blasts for nothing. */
    mobj_t *wreck = spawn(MT_IMP_TACHYON_TANK, 0, FIXED2_LIT(20, 50));
    mobj_t *other = spawn(MT_IMP_TACHYON_TANK, 1, FIXED2_LIT(21, 50));
    CHECK(wreck && other);
    wreck->hp = 1;
    CHECK(DR_SelfDestruct(wreck) && wreck->hp <= 0 && other->hp == other->max_hp);
    CHECK(!DR_SelfDestruct(wreck));
    /* Other orders pass through. */
    ticcmd_t stop = {.order = TC_STOP};
    CHECK(!G_GameUnitCommand(0, &stop, units, 1, NULL));
    rts_game_model_destroy(model);
    printf("PASS: self-destruct order kills the unit and scales its blast by health\n");
    return 0;
}

/* AmperAmp SetBooster(200 200 0.5 1): hit points jump to the cap, hold 200
 * cycles, fade one point every two, and the deadly boost ends in death. A
 * Medic's CanCancelBoosting hit ends the boost and keeps the hit points. */
static int amper(void) {
    RtsGameModel *model = fresh();
    CHECK(model);
    mobj_t *amper = spawn(MT_IMP_AMPER, 0, FIXED2_LIT(20, 20));
    mobj_t *guard = spawn(MT_IMP_GUARDIAN, 0, FIXED2_LIT(22, 20));
    mobj_t *tank = spawn(MT_IMP_TACHYON_TANK, 0, FIXED2_LIT(20, 22));
    mobj_t *foe = spawn(MT_FG_RAIDER, 1, FIXED2_LIT(18, 20));
    CHECK(amper && guard && tank && foe && (amper->traits & MF_HEAL));
    guard->hp = guard->max_hp / 2;
    tank->hp = tank->max_hp / 2;
    foe->hp = foe->max_hp / 2;
    /* Only wounded allied humans qualify. */
    CHECK(P_CanTarget(amper, guard) && !P_CanTarget(amper, tank) && !P_CanTarget(amper, foe));
    CHECK(DR_Attack(amper, &amper->info->attack, guard));
    CHECK(guard->hp == guard->max_hp && guard->dr.boost == 1);
    guard->hp -= 10;
    CHECK(!P_CanTarget(amper, guard)); /* no second boost */
    int hp = guard->hp;
    for (int c = 0; c < 200; ++c) DR_MobjTicker(guard);
    CHECK(guard->hp == hp && guard->dr.boost == 2);
    for (int c = 0; c < 20; ++c) DR_MobjTicker(guard);
    CHECK(guard->hp == hp - 10);
    for (int c = 0; c < 4000 && guard->hp > 0; ++c) DR_MobjTicker(guard);
    CHECK(guard->hp <= 0 && guard->dr.boost == 0);
    /* Medic cancel. */
    mobj_t *raider = spawn(MT_FG_RAIDER, 0, FIXED2_LIT(24, 24));
    mobj_t *medic = spawn(MT_FG_MEDIC, 0, FIXED2_LIT(25, 24));
    CHECK(raider && medic);
    raider->hp = 40;
    CHECK(DR_Boost(raider, 200, 200, 128, true) && raider->hp == raider->max_hp);
    raider->hp -= 30;
    hp = raider->hp;
    CHECK(DR_Attack(medic, &medic->info->attack, raider));
    CHECK(raider->dr.boost == 0 && raider->hp == hp);
    /* Without a boost the Medic heals through the engine as before. */
    CHECK(!DR_Attack(medic, &medic->info->attack, raider));
    /* A non-deadly boost fades back to the old hit points. */
    raider->hp = 50;
    CHECK(DR_Boost(raider, 200, 2, 256, false));
    for (int c = 0; c < 200; ++c) DR_MobjTicker(raider);
    CHECK(raider->hp == 50 && raider->dr.boost == 0);
    rts_game_model_destroy(model);
    printf("PASS: Amper boost holds, fades, kills, and a Medic cancels it\n");
    return 0;
}

int main(void) {
    RTS_RUN(formula());
    RTS_RUN(falloff());
    RTS_RUN(coverage());
    RTS_RUN(in_game());
    RTS_RUN(splash());
    RTS_RUN(martyr());
    RTS_RUN(lingering());
    RTS_RUN(self_destruct());
    RTS_RUN(amper());
    return 0;
}
