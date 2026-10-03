#include "engine.h"
#include "info.h"
#include "w2_local.h"

#include <string.h>

static const char *w2_sprnames[W2_TYPE_COUNT];
static state_t w2_states[W2_STATE_COUNT];
static mobjinfo_t w2_mobjinfo[W2_MOBJ_COUNT];
gameinfo_t game_info;

static int stand_state(int pud) { return 1 + pud * 2; }

void w2_limit_walk(int pud, int phases) {
    if (pud < 0 || pud >= W2_TYPE_COUNT) return;
    int stand = stand_state(pud);
    int walk = stand + 1;
    if (phases < 2) {
        w2_mobjinfo[pud + 1].seestate = stand;
        return;
    }
    int count = phases - 1;
    if (count > 4) count = 4;
    w2_states[walk].count = count;
}

void w2_build_info(void) {
    memset(w2_sprnames, 0, sizeof(w2_sprnames));
    memset(w2_states, 0, sizeof(w2_states));
    memset(w2_mobjinfo, 0, sizeof(w2_mobjinfo));
    w2_states[0] = (state_t){
        .tics = -1, .nextstate = 0,
    };
    for (int pud = 0; pud < W2_TYPE_COUNT; ++pud) {
        const w2_unit_t *unit = &w2_units[pud];
        bool mobile = (unit->flags & W2_MOBILE) != 0;
        int stand = stand_state(pud);
        int walk = stand + 1;
        w2_sprnames[pud] = unit->name;
        w2_states[stand] = (state_t){
            .sprite = pud, .frame = 0, .count = 1, .tics = -1,
            .nextstate = stand, .group = 0,
        };
        w2_states[walk] = (state_t){
            .sprite = pud, .frame = 1, .count = 4, .tics = W2_WALK_TICS,
            .action = mobile ? A_Chase : NULL,
            .nextstate = walk, .group = 2,
        };
        int hp = unit->hp > 0 ? unit->hp : 1;
        w2_mobjinfo[pud + 1] = (mobjinfo_t){
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
    game_info = (gameinfo_t){
        .sprnames = (const char *const *)w2_sprnames,
        .sprite_count = W2_TYPE_COUNT,
        .states = w2_states,
        .state_count = W2_STATE_COUNT,
        .mobjinfo = w2_mobjinfo,
        .mobj_type_count = W2_MOBJ_COUNT,
        .null_state = 0,
        .state_coord_mode = RTS_STATE_COORDS_GROUND_OFFSET,
        .selection_marker = { .style = SELECTION_STYLE_DEFAULT },
        .right_click_orders = false,
    };
}
