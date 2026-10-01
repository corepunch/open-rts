#include "t_local.h"
#include "info.h"
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); return 1; } } while (0)

static mobj_t *spawn(int type, fvec2_t position, int owner) {
    mobj_t *actor = P_SpawnMobj(fixed3_from_fvec2(position,0),type);
    assert(actor);
    actor->owner = actor->team = owner;
    actor->allegiance = owner ? ALLEGIANCE_ENEMY : ALLEGIANCE_PLAYER;
    return actor;
}

int main(void) {
    gameinfo = &game_info;
    level = (level_t){0};
    const int bombers[] = {MT_SCOUT, MT_ORTU};
    for (unsigned i = 0; i < sizeof(bombers)/sizeof(bombers[0]); ++i) {
        P_FreeThinkers();
        mobj_t *bomber = spawn(bombers[i],(fvec2_t){10.5f,10.5f},0);
        mobj_t *enemy = spawn(MT_TROOPER,(fvec2_t){11.5f,10.5f},1);
        mobj_t *neighbor = spawn(MT_TROOPER,(fvec2_t){11.5f,11.5f},1);
        enemy->traits &= ~MF_ATTACK;
        neighbor->traits &= ~MF_ATTACK;
        CHECK(bomber->hp == 800 && bomber->info->armor_class == 2);
        CHECK(bomber->info->attack.range == 2);
        for (int shot = 0; shot < 3; ++shot) {
            bomber->attack.cooldown_left_ms = 0;
            bomber->attack.target = enemy;
            CHECK(P_Attack(bomber));
            CHECK(bomber->attack.shots == (shot + 1) % 3);
            CHECK(bomber->attack.cooldown_left_ms == (shot == 2 ? 1980 : 660));
        }
        CHECK(enemy->hp == 800); /* Flight delays damage. */
        bomber->traits &= ~MF_ATTACK;
        for (int tic = 0; tic < 40; ++tic) P_Ticker();
        CHECK(enemy->hp == 782 && neighbor->hp == 800);
        bomber->traits |= MF_ATTACK;
        bomber->attack.cooldown_left_ms = 0;
        for (int tic = 0; tic < 70; ++tic) P_Ticker();
        CHECK(enemy->hp < 782); /* Ordinary thinker/state path also fires. */
    }
    const int ground[] = {MT_THUNDERBOLT, MT_ATRIL, MT_HUMAN_MINE};
    for (unsigned i = 0; i < sizeof(ground)/sizeof(ground[0]); ++i) {
        P_FreeThinkers();
        mobj_t *actor = spawn(ground[i],(fvec2_t){10.5f,10.5f},0);
        mobj_t *air = spawn(MT_SCOUT,(fvec2_t){11,10.5f},1);
        air->traits &= ~MF_ATTACK;
        actor->attack.target = air;
        CHECK(!P_Attack(actor));
        for (int tic = 0; tic < 40; ++tic) P_Ticker();
        CHECK(!actor->attack.target && actor->attack.cooldown_left_ms == 0);
        CHECK(air->hp == 800);
    }
    P_FreeThinkers();
    mobj_t *medic = spawn(MT_MEDI_CRAFT,(fvec2_t){10,10},0);
    mobj_t *zisp = spawn(MT_ZISP,(fvec2_t){11,10},0);
    CHECK(medic->info->armor_class == 2 && zisp->info->armor_class == 2);
    CHECK(medic->info->sight.day == 5 && medic->info->sight.night == 3);
    P_FreeThinkers();
    puts("PASS: bomber flight, burst/reload, impact-cell damage, air eligibility and aircraft stats");
    return 0;
}
