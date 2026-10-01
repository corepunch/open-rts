#include "t_local.h"
#include "engine.h"
#include "info.h"
#include "dark-colony.h"

#include <stdio.h>

static int native_routes_move_units(void) {
    const char *tag = "native_route_movement";
    gameinfo = NULL;
    level = (level_t){ .width = 16, .height = 16 };
    P_FreeThinkers();
    mobj_t *units[2];
    for (int i = 0; i < 2; ++i) units[i] = spawn_mobj_fixture((mobj_t){0});

    for (int i = 0; i < 2; ++i) {
        units[i]->core.position = fixed3_from_fvec2(
            (fvec2_t){ 2.5f, 3.5f + (float)i }, fixed_from_float(2.0f));
        units[i]->speed = 4.0f;
        units[i]->hp = 1;
        units[i]->radius = 0.25f;
        units[i]->traits = MF_MOBILE;
        units[i]->attack.target = NULL;
        units[i]->harvest.target = -1;
        RTS_CHECK(P_MoveUnitTo(&level, units[i], (fvec2_t){ 10.5f, 8.5f }), tag,
                  "native route created");
    }

    RTS_CHECK(units[0]->route.count && units[1]->route.count && !level.flow_fields, tag,
              "units own native cell routes without cached flow fields");
    RTS_CHECK(ivec2_equal(fvec2_cell(units[0]->movement.goal),
                           fvec2_cell(units[1]->movement.goal)), tag,
              "units receive the shared destination without formation slots");

    int unit_count = 2;
    for (int tic = 0; tic < 300 &&
         (!units[0]->movement.order_arrived || !units[1]->movement.order_arrived); ++tic) {
        P_Ticker();
    }

    RTS_CHECK(unit_count == 2, tag, "movement preserves both units");
    RTS_CHECK(units[0]->movement.order_arrived && units[1]->movement.order_arrived,
              tag,
              "both units arrive through native routes");
    RTS_CHECK(units[0]->core.position.z == fixed_from_float(2.0f) &&
              units[1]->core.position.z == fixed_from_float(2.0f),
              tag,
              "planar movement preserves z");

    P_FreeLevel(&level);
    return 0;
}

static int unit_turns_before_moving(int type, const char *stem) {
    const char *tag = stem;
    spritesheet_t sprite;
    RTS_CHECK(load_dark_colony_sprite(M_va("data/DCOLONY/ANIMATE/%s.FIN", stem), &sprite, NULL),
              tag, "load actual turning and travel poses");
    level = (level_t){ .width = 16, .height = 16 };
    P_FreeThinkers();
    gameinfo = &game_info;
    mobj_t *unit = spawn_mobj_fixture((mobj_t){
        .type_id = type, .traits = MF_MOBILE,
        .core = { .angle = ANG90 },
        .attack.target = NULL, .harvest.target = -1,
    });
    unit->core.position = fixed3_from_fvec2((fvec2_t){ 2.5f, 3.5f }, 0);
    P_InitMobj(&game_info, unit);
    fixed3_t start = unit->core.position;
    RTS_CHECK(P_MoveUnitTo(&level, unit, (fvec2_t){ 10.5f, 3.5f }), tag,
              "create eastbound movement order");
    bool saw_intermediate_pose = false, moved = false;
    for (int tic = 0; tic < 120; ++tic) {
        P_Ticker();
        const spriteframe_t *frame = &sprite.spritedef.spriteframes[unit->core.frame];
        if (memcmp(&unit->core.position, &start, sizeof(start)) != 0) {
            RTS_CHECK(states[unit->core.state_id].group == 2, tag,
                      "translation selects the travel cycle");
            RTS_CHECK(frame->rotations == 8, tag, "travel renders eight animated facings");
            moved = true;
            break;
        }
        RTS_CHECK(unit->core.state_id == mobjinfo[type].spawnstate, tag,
                  "turning stays in the standing state without translating");
        RTS_CHECK(frame->rotations == 16, tag, "turning renders all sixteen poses");
        int rotation = angle_to_direction(unit->core.angle, frame->rotations, ANG90, false);
        if (rotation & 1) {
            static const int lumps[8] = { 7, 5, 3, 1, 1, 3, 5, 7 };
            RTS_CHECK(frame->directions[rotation].layers[0].lump == lumps[rotation / 2],
                      tag, "intermediate angle selects the authored stationary body");
            saw_intermediate_pose = true;
        }
    }
    P_FreeLevel(&level);
    R_FreeSprite(&sprite);
    V_FreeScreen();
    RTS_CHECK(saw_intermediate_pose && moved, tag,
              "turn passes through intermediate stationary facings before travel");
    return 0;
}

int main(void) {
    RTS_RUN(native_routes_move_units());
    RTS_RUN(unit_turns_before_moving(MT_EXPLOITER, "EXPL"));
    RTS_RUN(unit_turns_before_moving(MT_THUNDERBOLT, "BARR"));
    puts("All native route movement tests passed.");
    return 0;
}
