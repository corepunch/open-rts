#include "mobj_test.h"
#include "engine.h"
#include "info.h"

#include <stdio.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

int main(void) {
    gameinfo = &game_info;
    level = (level_t){0};
    P_FreeThinkers();

    mobj_t *barrager = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){ 10.0f, 10.0f }, 0),
                                   MT_THUNDERBOLT);
    mobj_t *target = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){ 13.0f, 10.0f }, 0),
                                 MT_TROOPER);
    CHECK(barrager && target);

    barrager->owner = 0;
    barrager->allegiance = ALLEGIANCE_PLAYER;
    barrager->core.angle = P_PointToAngle(1.0f, 0.0f);
    barrager->attack.target = target;
    target->owner = 1;
    target->allegiance = ALLEGIANCE_ENEMY;
    target->traits &= ~MF_ATTACK;
    int starting_hp = target->hp;

    CHECK(P_SetMobjState(barrager, S_BARR_ATK1));
    bool launched = false;
    mobj_t *projectile = NULL;
    for (int tic = 0; tic < 8; ++tic) {
        P_Ticker();
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *mobj = (mobj_t *)th;
            if (!mobj->remove && (mobj->traits & MF_MISSILE)) {
                launched = true;
                projectile = mobj;
                break;
            }
        }
        if (launched) break;
    }
    CHECK(launched);
    CHECK(target->hp == starting_hp);
    CHECK(projectile->target == barrager);
    CHECK(game_info.mobjinfo[MT_CANNONBALL].speed == 8);
    CHECK(game_info.mobjinfo[MT_CANNONBALL].damage == 180);
    CHECK(projectile->core.position.x > barrager->core.position.x);
    CHECK(projectile->core.position.y == barrager->core.position.y);

    bool impacted = false;
    for (int tic = 0; tic < 40 && !impacted; ++tic) {
        P_Ticker();
        impacted = target->hp < starting_hp;
    }
    CHECK(impacted);
    CHECK(target->hp == starting_hp - 180);
    CHECK(projectile->remove);

    P_FreeThinkers();
    barrager = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){ 10.0f, 10.0f }, 0),
                           MT_THUNDERBOLT);
    mobj_t *far_target = P_SpawnMobj(
        fixed3_from_fvec2((fvec2_t){ 14.0f, 10.0f }, 0), MT_TROOPER);
    mobj_t *interceptor = P_SpawnMobj(
        fixed3_from_fvec2((fvec2_t){ 12.5f, 10.0f }, 0), MT_TROOPER);
    CHECK(barrager && far_target && interceptor);
    barrager->traits &= ~MF_ATTACK;
    far_target->traits &= ~MF_ATTACK;
    interceptor->traits &= ~MF_ATTACK;
    barrager->allegiance = ALLEGIANCE_PLAYER;
    far_target->allegiance = ALLEGIANCE_ENEMY;
    interceptor->allegiance = ALLEGIANCE_ENEMY;
    int far_target_hp = far_target->hp;
    int interceptor_hp = interceptor->hp;
    mobj_t *missile = P_SpawnMissile(barrager, far_target, MT_CANNONBALL);
    CHECK(missile && missile->target == barrager);
    for (int tic = 0; tic < 40; ++tic) {
        bool missile_alive = false;
        P_Ticker();
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *mobj = (mobj_t *)th;
            if (!mobj->remove && (mobj->traits & MF_MISSILE)) {
                missile_alive = true;
                break;
            }
        }
        if (!missile_alive) break;
    }
    CHECK(interceptor->hp == interceptor_hp - 180);
    CHECK(far_target->hp == far_target_hp);

    P_FreeThinkers();
    puts("PASS: Doom-style cannonball launch, movement, and collision");
    return 0;
}
