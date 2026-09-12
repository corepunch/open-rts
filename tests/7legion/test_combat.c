#include "game.h"
#include "info.h"
#include "../rts_test.h"

int main(void) {
    for (int i = 0; i < num_actor_types; ++i) {
        const mobjtype_t *type = &actor_types[i];
        if (!(type->traits & MF_ATTACK)) continue;
        P_InitThinkers();
        level.width = level.height = 64;
        level.blocked = calloc(64 * 64, 1);
        mobj_t *attacker = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){16, 16}, 0), type->id);
        mobj_t *target = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){48, 48}, 0), MT_MOBILE_BASE);
        RTS_CHECK(attacker && target, "7legion combat", "spawn combat pair");
        target->owner = target->team = 1;
        target->allegiance = ALLEGIANCE_ENEMY;
        int hp = target->hp;
        for (int t = 0; t < 60; ++t) P_Ticker();
        RTS_CHECK(target->hp == hp, "7legion combat", "out-of-range units cannot fire");
        target->core.position = fixed3_from_fvec2((fvec2_t){18, 16}, 0);
        for (int t = 0; t < 3; ++t) P_Ticker();
        RTS_CHECK(target->hp == hp - type->attack.damage, "7legion combat", "state action fires without direct P_Attack");
        hp = target->hp;
        int cooldown = attacker->attack.cooldown_left_ms;
        while (attacker->attack.cooldown_left_ms > 33) P_Ticker();
        RTS_CHECK(target->hp == hp && cooldown > 0, "7legion combat", "cooldown gates repeated damage");
        for (int t = 0; t < 4; ++t) P_Ticker();
        RTS_CHECK(target->hp == hp - type->attack.damage, "7legion combat", "attack repeats after cooldown");
        P_FreeLevel(&level);
    }
    puts("PASS: every 7legion combat type acquires targets and deals damage on state entry");
    return 0;
}
