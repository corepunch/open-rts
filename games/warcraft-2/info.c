#include "engine.h"
#include "info.h"
#include "w2_local.h"

#include <string.h>

const char *sprnames[W2_SPRITE_COUNT];
state_t states[W2_STATE_COUNT];
mobjinfo_t mobjinfo[W2_MOBJ_COUNT];
gameinfo_t game_info;

static int stand_state(int pud) { return 1 + pud * 2; }

void w2_limit_walk(int pud, int phases) {
    if (pud < 0 || pud >= W2_TYPE_COUNT) return;
    int stand = stand_state(pud);
    int walk = stand + 1;
    if (phases < 2) {
        mobjinfo[pud + 1].seestate = stand;
        return;
    }
    int count = phases - 1;
    if (count > 4) count = 4;
    states[walk].count = count;
}

void w2_build_info(void) {
    memset(sprnames, 0, sizeof(sprnames));
    memset(states, 0, sizeof(states));
    memset(mobjinfo, 0, sizeof(mobjinfo));
    states[0] = (state_t){
        .tics = -1, .nextstate = 0,
    };
    for (int pud = 0; pud < W2_TYPE_COUNT; ++pud) {
        const w2_unit_t *unit = &w2_units[pud];
        bool mobile = (unit->flags & W2_MOBILE) != 0;
        int stand = stand_state(pud);
        int walk = stand + 1;
        sprnames[pud] = unit->name;
        states[stand] = (state_t){
            .sprite = pud, .frame = 0, .count = 1, .tics = -1,
            .nextstate = stand, .group = 0,
        };
        states[walk] = (state_t){
            .sprite = pud, .frame = 1, .count = 4, .tics = W2_WALK_TICS,
            .action = mobile ? A_Chase : NULL,
            .nextstate = walk, .group = 2,
        };
        int hp = unit->hp > 0 ? unit->hp : 1;
        mobjinfo[pud + 1] = (mobjinfo_t){
            .doomednum = pud + 1,
            .spawnstate = stand,
            .spawnhealth = hp,
            .seestate = mobile ? walk : stand,
            .missilestate = 0,
            .deathstate = 0,
            .speed = (int)(unit->speed / W2_SPEED_DIVISOR),
            .radius = 16,
            .damage = unit->damage,
        };
    }
    static const int chop_frames[] = { 5, 6, 7, 8, 9, 5, 5 };
    static const int chop_tics[] = { 3, 3, 3, 5, 3, 7, 1 };
    for (int pud = 2; pud <= 3; ++pud) {
        int start = W2_WORK_STATE(pud);
        for (int i = 0; i < 7; ++i)
            states[start + i] = (state_t){
                .sprite = pud, .frame = chop_frames[i], .count = 1,
                .tics = chop_tics[i], .nextstate = i == 6 ? stand_state(pud) : start + i + 1,
                .group = 5,
            };
        states[W2_WAIT_STATE(pud)] = (state_t){
            .sprite = pud, .count = 1, .tics = 150,
            .nextstate = stand_state(pud), .group = 5,
        };
    }
    static const char *const carriers[] = { "peasant-gold", "peasant-lumber", "peon-gold", "peon-lumber" };
    for (int i = 0; i < 4; ++i) {
        int stand = W2_CARRY_STATE(i);
        sprnames[W2_TYPE_COUNT + i] = carriers[i];
        states[stand] = (state_t){ .sprite = W2_TYPE_COUNT + i, .count = 1,
            .tics = -1, .nextstate = stand };
        states[stand + 1] = (state_t){ .sprite = W2_TYPE_COUNT + i, .frame = 1,
            .count = 4, .tics = W2_WALK_TICS, .nextstate = stand + 1, .group = 2 };
    }
    game_info = (gameinfo_t){
        .sprnames = (const char *const *)sprnames,
        .sprite_count = W2_SPRITE_COUNT,
        .states = states,
        .state_count = W2_STATE_COUNT,
        .mobjinfo = mobjinfo,
        .mobj_type_count = W2_MOBJ_COUNT,
        .null_state = 0,
        .state_coord_mode = RTS_STATE_COORDS_GROUND_OFFSET,
        .selection_marker = { .style = SELECTION_STYLE_DEFAULT },
        .right_click_orders = true,
        .select_any = true,
        .f10_menu = true,
    };
}
