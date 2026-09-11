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
            if (!mobj->remove && (mobj->traits & MF_PROJECTILE)) {
                launched = true;
                projectile = mobj;
                break;
            }
        }
        if (launched) break;
    }
    CHECK(launched);
    CHECK(target->hp == starting_hp);
    CHECK(projectile->projectile.target == target);
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
    puts("PASS: Barrager launches a moving cannonball and damages on impact");
    return 0;
}
