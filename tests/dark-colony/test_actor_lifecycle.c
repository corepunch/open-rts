#include "mobj_test.h"
#include "game.h"
#include "engine.h"
#include "info.h"
#include "rts_model_test.h"
#include "dc_facing.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

int main(void) {
    level = (level_t){0};
    P_FreeThinkers();
    mobj_t *units[2];
    for (int i = 0; i < 2; ++i) units[i] = spawn_mobj_fixture((mobj_t){0});

    gameinfo = &game_info;
    P_ApplyActorTypeDefaults(units[0], actor_type_by_id(MT_TROOPER));
    P_ApplyActorTypeDefaults(units[1], actor_type_by_id(MT_TROOPER));
    units[1]->allegiance = ALLEGIANCE_ENEMY;
    P_InitMobj(&game_info, units[0]);
    int hp = units[1]->hp;
    CHECK(P_SetMobjState(units[0], S_TRSC_STND));
    CHECK(units[0]->core.state_id == S_TRSC_ATK1 && units[1]->hp == hp);
    /* Two windup frames, then exactly one damage action on the third frame. */
    for (int tic = 1; tic <= 12; ++tic) {
        CHECK(P_TickMobjState(units[0]));
        CHECK(units[1]->hp == hp - (tic >= 4 ? units[0]->info->attack.damage : 0));
    }
    CHECK(units[0]->core.state_id == S_TRSC_STND);

    static const struct { int type, corpse; } deaths[] = {
        { MT_TROOPER, S_TRSC_CORPSE }, { MT_GREY, S_GRAY_CORPSE },
        { MT_EXPLOITER, S_EXPL_CORPSE }, { MT_REAPER, S_REAP_DIEA2_CORPSE },
        { MT_THUNDERBOLT, S_BARR_CORPSE }, { MT_CYBORG, S_SARG_CORPSE },
        { MT_SCOUT, S_SCGM_CORPSE }, { MT_ORTU, S_ORTU_CORPSE },
        { MT_SLUG, S_SLUG_CORPSE },
    };
    for (size_t i = 0; i < sizeof(deaths) / sizeof(deaths[0]); ++i) {
        copy_mobj_fixture(units[1], &(mobj_t){0});
        P_ApplyActorTypeDefaults(units[1], actor_type_by_id(deaths[i].type));
        units[1]->allegiance = ALLEGIANCE_ENEMY;
        units[1]->hp = 1;
        units[1]->traits |= MF_SELECTED;
        units[1]->movement.order_arrived = true;
        units[1]->attack.target = units[0];
        units[1]->attack.cooldown_left_ms = 100;
        units[1]->harvest.target = 0;
        units[1]->harvest.timer_ms = 100;
        units[1]->harvest.phase = 1;
        units[1]->harvest.cargo = 1;
        units[1]->core.position = fixed3_from_fvec2((fvec2_t){ 0.25f, 0.75f }, FIXED_ONE);
        fixed3_t position = units[1]->core.position;
        units[1]->core.momentum = position;
        units[1]->core.angle = dc_direction_to_angle(0);
        units[0]->attack.target = units[1];
        CHECK(P_Attack(units[0]));
        CHECK(units[1]->hp == 0 && units[1]->traits == MF_RENDERABLE);
        CHECK(!units[1]->movement.flow_field && !units[1]->movement.order_arrived);
        CHECK(units[1]->attack.target == NULL && units[1]->attack.cooldown_left_ms == 0);
        CHECK(units[1]->harvest.target == -1 && units[1]->harvest.timer_ms == 0);
        CHECK(units[1]->harvest.phase == 0 && units[1]->harvest.cargo == 0);
        CHECK(memcmp(&units[1]->core.momentum, &(fixed3_t){0}, sizeof(fixed3_t)) == 0);
        for (int tic = 0; tic < 1000 && units[1]->core.tics != -1; ++tic)
            P_TickMobjState(units[1]);
        CHECK(!units[1]->remove && units[1]->core.state_id == deaths[i].corpse);
        CHECK(units[1]->core.tics == -1);
        CHECK(level.decoration_count == 0);
        CHECK(memcmp(&units[1]->core.position, &position, sizeof(position)) == 0);
        P_Ticker();
        CHECK(!units[1]->remove && units[1]->core.tics == -1);
    }
    P_FreeLevel(&level);
    puts("PASS: direct Trooper attack, death cleanup, and persistent corpse objects");
    return 0;
}
