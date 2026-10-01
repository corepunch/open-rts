#include "engine.h"
#include "info.h"
#include "dark-colony.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/DCOLONY",
        .map_path = "SCENARIO/MPLAYER/D8PLAY01.MAP"};
    assert(model && rts_game_model_load(model, &config));
    int rate = level.resource_vents[0].rate;
    int amount = level.resource_vents[0].amount;
    assert(amount > 0);
    dc_skirmish_t setup = {.quantity = 20, .flow = 2, .rank = 3};
    for (int i = 0; i < 8; ++i)
        setup.players[i] = (dc_skirmish_player_t){.type = DC_PLAYER_NONE, .team = i};
    setup.players[0] = (dc_skirmish_player_t){.type = DC_PLAYER_HUMAN, .race = 1, .color = 6, .team = 2};
    setup.players[1] = (dc_skirmish_player_t){.type = DC_PLAYER_AI_PLUS, .race = 0, .color = 4, .team = 3};
    setup.players[2] = (dc_skirmish_player_t){.type = DC_PLAYER_AI, .race = 1, .color = 0, .team = 2};
    setup.players[7] = (dc_skirmish_player_t){.type = DC_PLAYER_AI, .race = 0, .color = 2, .team = 7};
    DC_RequestSkirmish(config.map_path, &setup);
    assert(rts_game_model_load(model, &config));
    assert(level.player_teams && DC_LevelSkirmish(&level));
    assert(level.player_colors[0] == 6 && level.player_colors[1] == 4);
    assert(level.resource_vents[0].rate == rate / 2);
    assert(level.resource_vents[0].amount == amount * 5);
    mobj_t *commanders[8] = {0};
    int bases[8] = {0};
    mobjlist_t objects = P_ListMobjs();
    for (int i = 0; i < objects.count; ++i) {
        mobj_t *actor = objects.items[i];
        if (actor->type_id == MT_VENT || actor->owner >= 8) continue;
        assert(actor->owner == actor->team);
        assert(setup.players[actor->owner].type != DC_PLAYER_NONE);
        if (actor->type_id == MT_EXCOPOD || actor->type_id == MT_ALIEN_MINDHIVE) {
            ++bases[actor->owner];
            assert(actor->type_id == (setup.players[actor->owner].race ? MT_ALIEN_MINDHIVE : MT_EXCOPOD));
        }
        if (actor->native_type_id >= 69 && actor->native_type_id <= 76) {
            commanders[actor->owner] = actor;
            assert(actor->native_type_id == (setup.players[actor->owner].race ? 76 : 72));
            assert(actor->type_id == (setup.players[actor->owner].race ? MT_GREY : MT_TROOPER));
        }
    }
    assert(commanders[0] && commanders[1] && commanders[2] && commanders[7]);
    assert(bases[0] == 1 && bases[1] == 1 && bases[2] == 1 && bases[7] == 1);
    assert(P_IsAlly(commanders[0], commanders[2]));
    assert(!P_IsAlly(commanders[0], commanders[1]));
    assert(!P_IsAlly(commanders[1], commanders[7]));
    P_FreeMobjList(&objects);
    /* A two-start map must retain a player in lobby slot seven. Seed zero
     * gives 5758 % 2 == 0, so the native shuffle keeps start zero first. */
    config.map_path = "SCENARIO/MPLAYER/D2PLAY01.MAP";
    setup.players[1].type = setup.players[2].type = DC_PLAYER_NONE;
    DC_RequestSkirmish(config.map_path, &setup);
    assert(rts_game_model_load(model, &config));
    int ortu = 0, atril = 0, last_player = 0;
    objects = P_ListMobjs();
    for (int i = 0; i < objects.count; ++i) {
        mobj_t *actor = objects.items[i];
        if (actor->owner == 0) {
            ortu += actor->type_id == MT_ORTU;
            atril += actor->type_id == MT_ATRIL;
        }
        last_player += actor->owner == 7;
    }
    assert(ortu == 1 && atril == 1 && last_player > 0);
    P_FreeMobjList(&objects);
    config.map_path = "SCENARIO/HUMAN/HUMAN01.MAP";
    assert(rts_game_model_load(model, &config));
    assert(!level.player_teams && !DC_LevelSkirmish(&level));
    rts_game_model_destroy(model);
    puts("PASS: skirmish ownership, races, colors, alliances, commander rank, resources and campaign isolation");
    return 0;
}
