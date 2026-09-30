#include "mobj_test.h"
#include "info.h"
#include "p_path.h"
#include "p_special.h"
#include "p_weapon.h"
#include "m_random.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); return 1; } } while (0)

static mobj_t *spawn(int type, int x, int y, int owner) {
    mobj_t *actor = P_SpawnMobj((fixed3_t){x*FIXED_ONE+FIXED_ONE/2,y*FIXED_ONE+FIXED_ONE/2,0},type);
    assert(actor);
    actor->owner = actor->team = owner;
    actor->allegiance = owner ? ALLEGIANCE_ENEMY : ALLEGIANCE_PLAYER;
    actor->traits &= ~MF_ATTACK;
    return actor;
}

int main(void) {
    gameinfo = &game_info;
    level = (level_t){.width=40,.height=40};
    P_InitThinkers();
    const int healers[] = {MT_MEDI_CRAFT,MT_ZISP};
    for (int i = 0; i < 2; ++i) {
        mobj_t *healer = spawn(healers[i],10,10,0);
        mobj_t *air = spawn(MT_SCOUT,11,10,0);
        mobj_t *ground = spawn(MT_TROOPER,11,10,0);
        mobj_t *enemy = spawn(MT_TROOPER,10,10,1);
        air->hp = ground->hp = enemy->hp = 700;
        CHECK(healer->ability_charge == 64);
        DC_TickSupport(1);
        CHECK(air->hp == 718 && ground->hp == 700 && enemy->hp == 700);
        CHECK(healer->ability_charge == 0 && healer->repair_wait == 50);
        for (int tick=2; tick<128; ++tick) DC_TickSupport(tick);
        CHECK(air->hp == 718 && healer->ability_charge == 3);
        DC_TickSupport(128);
        CHECK(air->hp == 736 && healer->ability_charge == 0);
        air->hp = air->max_hp;
        ground->hp = ground->max_hp-5;
        healer->repair_wait=0; healer->ability_charge=4;
        DC_TickSupport(129);
        CHECK(ground->hp == ground->max_hp && healer->ability_charge == 0);
        P_FreeThinkers();
    }
    CHECK(P_InitSight());
    mobj_t *trooper=spawn(MT_TROOPER,10,10,0);
    mobj_t *mine=spawn(MT_HUMAN_MINE,12,10,1);
    P_UpdateSight();
    CHECK(!P_VisibleTo(trooper,mine));
    mobj_t *detector=spawn(MT_SENTINEL,11,10,0);
    P_UpdateSight();
    CHECK(P_VisibleTo(trooper,mine));
    P_RemoveMobj(detector);
    P_UpdateSight();
    CHECK(!P_VisibleTo(trooper,mine));
    free(level.sight.cells); level.sight.cells=NULL;
    P_FreeThinkers();
    mobj_t *source=spawn(MT_THUNDERBOLT,10,10,0);
    mobj_t *target=spawn(MT_TROOPER,14,12,1);
    mobj_t *shot=P_SpawnMissile(source,target,MT_CANNONBALL);
    CHECK(shot && shot->core.momentum.x == 54*256 && shot->core.momentum.y == 25*256);
    CHECK(shot->missile.duration == 18 && shot->core.angle == (angle_t)18<<24);
    /* The native probability buckets leave six byte values at (+2,+2). */
    bool residual = false;
    for (int seed = 0; seed < 256; ++seed) {
        uint8_t index = seed;
        if ((M_DC_Random(&index) & 255) < 250) continue;
        mobj_t expected = *shot;
        DC_AimMissile(&expected, fixed3_add(target->core.position,
                     (fixed3_t){2*FIXED_ONE,2*FIXED_ONE,0}));
        level.random_index = seed;
        DC_ScatterMissile(shot,target->core.position);
        CHECK(shot->core.angle == expected.core.angle && shot->missile.duration == expected.missile.duration);
        CHECK(shot->core.momentum.x == expected.core.momentum.x && shot->core.momentum.y == expected.core.momentum.y);
        residual = true;
    }
    CHECK(residual);
    /* The next cell is reserved while its occupant is still drawn elsewhere. */
    target->route=(dc_route_t){.cells={{15,12}},.count=1,.traveling=true};
    CHECK(DC_Occupant((ivec2_t){15,12},false,false)==target);
    CHECK(!DC_Occupant((ivec2_t){14,12},false,false));
    shot->core.position=(fixed3_t){15*FIXED_ONE,12*FIXED_ONE,0};
    A_Explode(shot);
    CHECK(target->hp==550);
    P_FreeThinkers();
    CHECK(DC_LoadWeapons(&level,"data/DCOLONY"));
    source=spawn(MT_THUNDERBOLT,10,10,0);
    target=spawn(MT_TROOPER,10,8,1);
    source->core.angle=ANG270;
    shot=DC_FireMissiles(source,target,MT_CANNONBALL);
    CHECK(shot && shot->missile.wait==8);
    CHECK(shot->core.position.x==source->core.position.x);
    CHECK(shot->core.position.y==source->core.position.y+19*2048);
    fixed3_t origin=shot->core.position;
    for (int tick=0; tick<16; ++tick) P_Ticker();
    CHECK(shot->missile.age==0 && shot->core.position.x==origin.x && shot->core.position.y==origin.y);
    for (int tick=0; tick<2; ++tick) P_Ticker();
    CHECK(shot->missile.age==1);
    P_FreeThinkers();
    DC_FreeWeapons(&level);
    const int artillery[]={MT_THUNDERBOLT,MT_ATRIL};
    for (int i=0; i<2; ++i) {
        source=spawn(artillery[i],10,10,0);
        target=spawn(MT_TROOPER,25,10,1);
        source->traits |= MF_ATTACK;
        source->attack.target=target;
        CHECK(!P_Attack(source));
        level.upgrades[source->native_type_id][0].weapon=2;
        CHECK(P_Attack(source));
        CHECK(source->attack.cooldown_left_ms==(i ? 150 : 75)*66);
        P_FreeThinkers();
    }
    source=spawn(MT_SCOUT,10,10,0);
    target=spawn(MT_TROOPER,11,10,1);
    level.upgrades[5][0].weapon=2;
    shot=P_SpawnMissile(source,target,MT_SCOUT_BOMB);
    CHECK(shot->missile.damage==150 && shot->core.state_id==S_SPIKE_BULLET1);
    level.upgrades[0][1].armor=1;
    CHECK(DC_DefendedDamage(target,187)==149);
    level.upgrades[0][1].armor=2;
    CHECK(DC_DefendedDamage(target,187)==124);
    mobj_t *infantry = spawn(MT_TROOPER,11,11,0);
    infantry->traits |= MF_ATTACK;
    infantry->attack.target = target;
    int hp = target->hp;
    CHECK(P_Attack(infantry));
    CHECK(target->hp == hp - infantry->info->attack.damage * 170 / 256);
    /* A projectile cannot reserve the ground layer or hide its occupant. */
    mobj_t *empty_shot = P_SpawnMobj((fixed3_t){12*FIXED_ONE,12*FIXED_ONE,0},MT_SCOUT_BOMB);
    CHECK(empty_shot && !DC_Occupant((ivec2_t){12,12},false,false));
    P_FreeThinkers();
    puts("PASS: healer priority/recharge/caps, detector concealment, quantized launch and reserved blast cells");
    return 0;
}
