#include "t_local.h"
#include "dark-reign.h"
#include "../../games/dark-reign/info.h"
#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The game setup screen's choices reach the loaded level: closed and
 * available slots start empty, a chosen side swaps the authored units,
 * teams ally their members and the credits field replaces SetCredit. */
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static const char *MAP = "scenario/MULTI/4CROSS/4CROSS.SCN";

static RtsGameModel *load(RtsRenderSnapshot *snap) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/REIGN/dark", .map_path = MAP};
    CHECK(model && rts_game_model_load(model, &config) && rts_game_model_snapshot(model, snap));
    return model;
}

/* 4CROSS gives each team a rig and three infantry of its side. */
static const char *infantry_sprite(const RtsRenderSnapshot *snap, int owner) {
    for (int i = 0; i < snap->unit_count; ++i)
        if (snap->units[i].owner == owner && strcmp(snap->units[i].sprite_name, "ucfcnst0.spr"))
            return snap->units[i].sprite_name;
    return NULL;
}

static int owned(const RtsRenderSnapshot *snap, int owner) {
    int n = 0;
    for (int i = 0; i < snap->unit_count; ++i) n += snap->units[i].owner == owner;
    return n;
}

int main(void) {
    RtsRenderSnapshot plain, edited;
    RtsGameModel *model = load(&plain);
    char fg[32], imperium[32];
    CHECK(infantry_sprite(&plain, 0) && infantry_sprite(&plain, 1));
    snprintf(fg, sizeof(fg), "%s", infantry_sprite(&plain, 0));       /* team 0: SetTeamSide(0) */
    snprintf(imperium, sizeof(imperium), "%s", infantry_sprite(&plain, 1)); /* team 1: SetTeamSide(1) */
    CHECK(strcmp(fg, imperium) && owned(&plain, 2) > 0 && owned(&plain, 3) > 0);
    CHECK(!DR_LevelSkirmish());
    rts_game_model_destroy(model);

    dr_skirmish_t setup = {.count = 4, .credits = 7500};
    setup.slots[0] = (dr_slot_t){.type = DR_SLOT_HUMAN, .side = DR_SIDE_IMPERIUM, .team = 1};
    setup.slots[1] = (dr_slot_t){.type = DR_SLOT_MEDIUM, .side = DR_SIDE_FG, .team = 2};
    setup.slots[2] = (dr_slot_t){.type = DR_SLOT_CLOSED};
    setup.slots[3] = (dr_slot_t){.type = DR_SLOT_HARD, .team = 1};
    DR_RequestSkirmish(MAP, &setup);
    model = load(&edited);
    CHECK(DR_LevelSkirmish() && DR_LevelSkirmish()->count == 4);
    CHECK(!strcmp(infantry_sprite(&edited, 0), imperium));
    CHECK(!strcmp(infantry_sprite(&edited, 1), fg));
    CHECK(owned(&edited, 2) == 0 && owned(&edited, 3) == owned(&plain, 3));
    CHECK(edited.player_resources[0][0] == 7500 && edited.player_resources[3][0] == 7500);
    CHECK(level.player_teams);
    CHECK(level.sight.allies[0] & (UINT32_C(0x40000000) >> 3));
    CHECK(level.sight.allies[3] & (UINT32_C(0x40000000) >> 0));
    CHECK(!(level.sight.allies[0] & (UINT32_C(0x40000000) >> 1)));
    rts_game_model_destroy(model);

    /* A request names one map; another level plays as authored. */
    DR_RequestSkirmish("scenario/MULTI/2NIC/2NIC.SCN", &setup);
    model = load(&edited);
    CHECK(!DR_LevelSkirmish() && owned(&edited, 2) == owned(&plain, 2));
    rts_game_model_destroy(model);
    puts("PASS: dark-reign game setup applies to the level");
    return 0;
}
