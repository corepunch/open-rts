#include "t_local.h"
#include "engine.h"
#include "dark-reign.h"
#include "info.h"

#define CHECK(c) RTS_CHECK(c, "Dark Reign damage", #c)

/* Offense and class indices follow DAMAGE.TXT's declaration order. */
enum { S1 = 22, C1 = 3, E3 = 6, G2 = 11, K1 = 14 };
enum { TOUGH_HUMAN = 0, TOUGH_HUMAN_WET = 1, TANK_PLATING_WET = 6, BUILDING_ARMOUR = 7, SUPER_ARMOUR = 8 };

/* dkreign.exe 0x474b40 with the rolls pinned: a variation roll of V gives no
 * spread, a crit roll of 99 never crits. */
static int formula(void) {
    bool crit;
    /* Sniper rifle (S1 150): full against humans, 10% against tank plating. */
    CHECK(DR_VulnerableDamage(S1, TOUGH_HUMAN_WET, 100, 150, 99, 10, &crit) == 150 && !crit);
    CHECK(DR_VulnerableDamage(S1, TANK_PLATING_WET, 100, 150, 99, 10, &crit) == 15);
    CHECK(DR_VulnerableDamage(S1, BUILDING_ARMOUR, 100, 150, 99, 10, &crit) == 15);
    /* Variation is uniform -V..V-1 percentage points. */
    CHECK(DR_VulnerableDamage(S1, TOUGH_HUMAN_WET, 100, 150, 99, 0, &crit) == 135);
    CHECK(DR_VulnerableDamage(S1, TOUGH_HUMAN_WET, 100, 150, 99, 19, &crit) == 163);
    /* Chaff does nothing to ground armour; SuperArmour takes nothing. */
    CHECK(DR_VulnerableDamage(C1, TANK_PLATING_WET, 100, 8, 99, 0, &crit) == 0);
    CHECK(DR_VulnerableDamage(E3, SUPER_ARMOUR, 100, 180, 99, 0, &crit) == 0);
    /* Buildings default to half (E3 40%). */
    CHECK(DR_VulnerableDamage(E3, BUILDING_ARMOUR, 100, 180, 99, 10, &crit) == 72);
    /* The percentage truncates before it multiplies: 40% x strength 75 = 30%,
     * and 10% x 75 = 7% (7.5 truncated): 7 of 100, not 8. */
    CHECK(DR_VulnerableDamage(E3, BUILDING_ARMOUR, 75, 100, 99, 10, &crit) == 30);
    CHECK(DR_VulnerableDamage(S1, BUILDING_ARMOUR, 75, 100, 99, 10, &crit) == 7);
    /* Critical hits (G2 3%, K1 2% on humans) report a kill, not extra damage. */
    CHECK(DR_VulnerableDamage(G2, TOUGH_HUMAN, 100, 50, 2, 10, &crit) == 50 && crit);
    CHECK(DR_VulnerableDamage(G2, TOUGH_HUMAN, 100, 50, 3, 10, &crit) == 40 && !crit);
    CHECK(DR_VulnerableDamage(K1, TOUGH_HUMAN, 100, 50, 1, 5, &crit) == 50 && crit);
    /* Unknown categories leave the weapon's damage alone. */
    CHECK(DR_VulnerableDamage(-1, TOUGH_HUMAN, 100, 50, 0, 0, &crit) == 50 && !crit);
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

int main(void) {
    RTS_RUN(formula());
    RTS_RUN(coverage());
    RTS_RUN(in_game());
    return 0;
}
