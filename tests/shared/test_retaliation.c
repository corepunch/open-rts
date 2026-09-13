#include "game.h"
#include "info.h"
#include "p_local.h"
#include "rts_test.h"

#define CHECK(c) RTS_CHECK(c, "shared retaliation", #c)

int main(void) {
    G_InitGame();
    P_InitThinkers();
    level = (level_t){.width = 32, .height = 32};
    level.blocked = calloc(32 * 32, 1);
    CHECK(level.blocked);
    const mobjtype_t *type = NULL;
    for (int i = 0; i < num_actor_types; ++i) {
        const mobjtype_t *candidate = &actor_types[i];
        if ((candidate->traits & (MF_MOBILE | MF_ATTACK)) == (MF_MOBILE | MF_ATTACK) &&
            mobjinfo[candidate->id].missilestate && mobjinfo[candidate->id].seestate) {
            type = candidate;
            break;
        }
    }
    CHECK(type);
    mobj_t *source = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){8, 8}, 0), type->id);
    mobj_t *victim = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){12, 8}, 0), type->id);
    CHECK(source && victim);
    source->traits &= ~MF_ATTACK;
    mobjtype_t attacker = *type;
    attacker.damage_action = NULL;
    source->info = &attacker;
    source->hp = source->max_hp = 10000;
    victim->owner = victim->team = 1;
    victim->allegiance = ALLEGIANCE_ENEMY;
    mobjtype_t defender = *type;
    defender.damage_action = NULL;
    defender.attack.range = 2;
    defender.attack.damage = 1;
    defender.attack.projectile_type = 0;
    victim->info = &defender;
    victim->speed = 4;
    fixed3_t start = victim->core.position;
    P_DamageMobj(victim, source, 1);
    CHECK(victim->attack.target == source);
    for (int tic = 0; tic < 300 && source->hp == source->max_hp; ++tic) P_Ticker();
    CHECK(fvec2_distance_squared(fixed3_xy_to_fvec2(victim->core.position),
                                 fixed3_xy_to_fvec2(start)) > 1);
    CHECK(source->hp < source->max_hp);
    /* Follow an attacker that leaves range, then release the stable reference
     * when it is removed. No base, mission script, or per-game AI is involved. */
    source->core.position = fixed3_from_fvec2((fvec2_t){5, 8}, 0);
    int hp = source->hp;
    for (int tic = 0; tic < 300 && source->hp == hp; ++tic) P_Ticker();
    CHECK(source->hp < hp);
    P_RemoveMobj(source);
    CHECK(!victim->attack.target);
    P_Ticker();
    P_DamageMobj(victim, victim, 1);
    CHECK(!victim->attack.target);
    mobj_t *ally = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){12, 12}, 0), type->id);
    CHECK(ally);
    ally->owner = ally->team = 1;
    ally->allegiance = ALLEGIANCE_ENEMY;
    P_DamageMobj(victim, ally, 1);
    CHECK(!victim->attack.target);
    P_FreeLevel(&level);
    printf("PASS: %s shared retaliation, pursuit, return fire and target cleanup\n", g_game_id);
    return 0;
}
