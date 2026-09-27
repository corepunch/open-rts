#include "mobj_test.h"
#include "info.h"
#include "d_ticcmd.h"
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

static mobj_t *spawn(int type, float x, float y, int owner) {
    mobj_t *actor = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){x,y},0), type);
    assert(actor);
    actor->owner = actor->team = owner;
    actor->allegiance = owner ? ALLEGIANCE_ENEMY : ALLEGIANCE_PLAYER;
    actor->traits &= ~MF_ATTACK;
    return actor;
}

static mobj_t *projectile(void) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *actor = (mobj_t *)th;
        if (!actor->remove && (actor->traits & MF_MISSILE)) return actor;
    }
    return NULL;
}

int main(void) {
    gameinfo = &game_info;
    level = (level_t){0};
    P_FreeThinkers();
    const int launchers[] = {MT_THUNDERBOLT, MT_ATRIL};
    const int shots[] = {MT_CANNONBALL, MT_PUS_BOMB};
    for (int i = 0; i < 2; ++i) {
        mobj_t *source = spawn(launchers[i],10,10,0);
        mobj_t *target = spawn(MT_TROOPER,14,10,1);
        mobj_t *blocker = spawn(MT_TROOPER,11,10,1);
        mobj_t *ally = spawn(MT_TROOPER,14,11,0);
        mobj_t *air = spawn(MT_SCOUT,14,10,1);
        int hp = target->hp, ally_hp = ally->hp;
        mobj_t *shot = P_SpawnMissile(source,target,shots[i]);
        CHECK(shot && shot->target == source);
        CHECK(shot->core.sprite_id == (i ? SPR_ATRIL : SPR_BARR));
        CHECK(shot->core.frame == (i ? 321 : 282));
        CHECK(shot->missile.duration == 17); /* floor(4*256/60). */
        for (int tic = 0; tic < 18; ++tic) P_Ticker();
        CHECK(target->hp == hp && blocker->hp == 800 && shot->core.position.z > 0);
        CHECK(shot->core.position.z == 17 * 640 * 4);
        for (int tic = 0; tic < 20 && (shot->traits & MF_MISSILE); ++tic) P_Ticker();
        CHECK(!(shot->traits & MF_MISSILE));
        CHECK(target->hp == hp - 187); /* Lands in cell 13: 75% of 250. */
        CHECK(ally->hp == ally_hp - 31); /* 50% at quarter friendly damage. */
        CHECK(blocker->hp == 675 && air->hp == air->max_hp);
        CHECK(shot->core.position.z == 0 && shot->core.tics > 0);
        for (int tic = 0; tic < 120; ++tic) P_Ticker();
        CHECK(target->hp == hp - 187);
        P_FreeThinkers();
    }
    mobj_t *source = spawn(MT_MOBILE_TOWER,10,10,0);
    mobj_t *target = spawn(MT_TROOPER,14,10,1);
    mobj_t *blocker = spawn(MT_TROOPER,12,10,1);
    CHECK(P_SpawnMissile(source,target,MT_TOWER_ROCKET));
    for (int tic = 0; tic < 45; ++tic) P_Ticker();
    CHECK(blocker->hp == 700 && target->hp == 800);
    CHECK(P_SpawnMissile(source,target,MT_XENO_BOLT));
    P_RemoveMobj(target); P_RemoveMobj(blocker); P_RemoveMobj(source);
    for (int tic = 0; tic < 100; ++tic) P_Ticker();
    CHECK(!projectile());
    P_FreeThinkers();

    source = spawn(MT_TROOPER,10,10,0);
    target = spawn(MT_TROOPER,20,10,1);
    source->traits |= MF_ATTACK;
    source->attack.target = target;
    CHECK(!P_Attack(source) && target->hp == target->max_hp);
    P_FreeThinkers();

    const int mobile[] = {MT_TURRET_CARRIER,MT_XENOWORT,MT_SENTINEL,MT_SLOM};
    const int deployed[] = {MT_MOBILE_TOWER,MT_XENO_TOWER,MT_HUMAN_MINE,MT_ALIEN_MINE};
    for (int i = 0; i < 4; ++i) {
        mobj_t *unit = spawn(mobile[i],10,10,0);
        uint32_t id = unit->id;
        unit->hp = 700;
        unit->traits |= MF_SELECTED;
        ticcmd_t command = {.order=TC_DEPLOY,.count=1,.units={id}};
        G_RunTiccmd(0,&command);
        CHECK(!(unit->traits & MF_MOBILE));
        CHECK(!P_Deploy(unit));
        for (int tic = 0; tic < 140; ++tic) P_Ticker();
        CHECK(unit->type_id == deployed[i] && unit->id == id);
        CHECK(unit->hp == 700 && (unit->traits & MF_SELECTED));
        CHECK(unit->traits & (i < 2 ? MF_TURRET : MF_LANDMINE));
        CHECK(!P_MoveUnitTo(&level,unit,(fvec2_t){12,10}));
        if (i < 2) {
            target = spawn(MT_TROOPER,12,10,1);
            for (int tic = 0; tic < 25; ++tic) P_Ticker();
            CHECK(target->hp < target->max_hp);
        }
        P_FreeThinkers();
    }
    mobj_t *mine = spawn(MT_HUMAN_MINE,10,10,0);
    mine->traits |= MF_ATTACK;
    spawn(MT_TROOPER,10.75f,10,0);
    spawn(MT_SCOUT,10.75f,10,1);
    for (int tic = 0; tic < 20; ++tic) P_Ticker();
    CHECK(mine->hp == 800 && !projectile());
    for (int charge = 0; charge < 3; ++charge) {
        target = spawn(MT_TROOPER,10.75f,10,1);
        mine->attack.cooldown_left_ms = 0;
        for (int tic = 0; tic < 8; ++tic) P_Ticker();
        CHECK(target->hp == 0);
        if (charge < 2) CHECK(mine->hp == 494 - charge * 306);
    }
    bool live_mine = false;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th=th->next) {
        mobj_t *unit=(mobj_t *)th;
        if (!unit->remove && unit->type_id == MT_HUMAN_MINE) live_mine=true;
    }
    CHECK(!live_mine);
    P_FreeThinkers();
    puts("PASS: native artillery, rocket collision, turret deployment, and three-charge mines");
    return 0;
}
