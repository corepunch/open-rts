#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

static int fail(const char *message) { return rts_fail("z squad", message); }

int main(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/Z"};
    if (!model || !rts_game_model_load(model, &config)) return fail("load level");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    fixed3_t at = fixed3_from_fixed2(fixed2_cell_center((ivec2_t){20, 20}), 0);
    mobj_t *leader = P_SpawnMobj(at, MT_UNIT_GRUNT);
    mobj_t *late = P_SpawnMobj(at, MT_UNIT_GRUNT);
    mobj_t *first = P_SpawnMobj(at, MT_UNIT_GRUNT);
    mobj_t *enemy = P_SpawnMobj(at, MT_UNIT_GRUNT);
    if (!leader || !late || !first || !enemy) return fail("spawn robots");
    Z_SetTeam(enemy, 1);
    leader->z.squad = late->z.squad = first->z.squad = enemy->z.squad = 255;
    leader->z.squad_slot = enemy->z.squad_slot = UINT8_MAX;
    late->z.squad_slot = 4;
    first->z.squad_slot = 2;
    fixed2_t goal = fixed2_cell_center((ivec2_t){25, 20});
    if (!P_MoveUnitTo(&level, leader, goal)) return fail("leader move");
    leader->z.route = (zroute_t){.active = TC_MOVE, .count = 1, .owner = leader->owner,
                                  .steps = {{.order = TC_MOVE, .position = at}}};
    first->z.board = enemy->id;
    first->z.service = enemy->id;
    P_MobjSetSelected(leader, true);
    Z_SquadLeave(leader);
    if (leader->z.squad || first->z.squad_slot != UINT8_MAX || late->z.squad_slot != 4 ||
        enemy->z.squad_slot != UINT8_MAX) return fail("first follower promoted within its team");
    if (first->z.route.count != 1 || first->z.route.active != TC_MOVE ||
        first->z.board || first->z.service || !P_HasMoveOrder(first) ||
        !fixed2_equal(first->movement.goal, goal) || !P_MobjIsSelected(first))
        return fail("promotion transfers route and replans goal");
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    uint32_t first_id = first->id, late_id = late->id, leader_id = leader->id, enemy_id = enemy->id;
    uint32_t hash = G_Consistency();
    if (!G_SaveGameBlob(&save, &size, "Squad promotion", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save promoted squad");
    free(save);
    first = P_MobjById(first_id); late = P_MobjById(late_id);
    leader = P_MobjById(leader_id); enemy = P_MobjById(enemy_id);
    if (!first || !late || !leader || !enemy || G_Consistency() != hash ||
        first->z.squad_slot != UINT8_MAX || late->z.squad_slot != 4 || first->z.route.count != 1)
        return fail("load preserves squad slots and route");
    leader->core.position = late->core.position = enemy->core.position =
        fixed3_from_fixed2(fixed2_cell_center((ivec2_t){18, 18}), 0);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker) ((mobj_t *)th)->traits &= ~MF_ATTACK;
    fixed2_t start = fixed3_xy(first->core.position);
    for (int i = 0; i < 40; ++i) if (!rts_tick(model, NULL)) return fail("tick promoted leader");
    if (fixed2_equal(start, fixed3_xy(first->core.position))) return fail("promoted leader travels after load");
    Z_SquadLeave(late);
    if (late->z.squad || first->z.squad_slot != UINT8_MAX) return fail("follower departure retains leader");
    mobj_t *crew[] = {leader, late};
    leader->z.squad = late->z.squad = 255;
    Z_SquadEjected(crew, 2);
    if (leader->z.squad_slot != 1 || late->z.squad_slot != 2 || first->z.squad_slot != UINT8_MAX)
        return fail("ejected crew join existing leader without slot collisions");
    Z_SquadLeave(first);
    if (leader->z.squad_slot != UINT8_MAX) return fail("ejected follower can be promoted");
    Z_SetTeam(leader, 1);
    if (leader->z.squad || late->z.squad_slot != UINT8_MAX) return fail("team change promotes old team follower");
    first->z.squad = late->z.squad;
    first->z.squad_slot = 1;
    P_ClearMove(late);
    if (!P_MoveUnitTo(&level, first, goal)) return fail("follower move");
    P_DamageMobj(late, NULL, late->hp);
    if (first->z.squad_slot != UINT8_MAX || late->z.squad || P_HasMoveOrder(first))
        return fail("lethal damage promotes follower and replaces its old motion");
    mobj_t *blocked_leader = P_SpawnMobj(at, MT_UNIT_GRUNT);
    mobj_t *blocked_follower = P_SpawnMobj(at, MT_UNIT_GRUNT);
    if (!blocked_leader || !blocked_follower) return fail("spawn blocked promotion fixture");
    blocked_leader->z.squad = blocked_follower->z.squad = 254;
    blocked_leader->z.squad_slot = UINT8_MAX;
    blocked_follower->z.squad_slot = 1;
    P_NavBeginTick();
    if (!P_MoveUnitTo(&level, blocked_leader, goal) || !P_MoveUnitTo(&level, blocked_follower, goal))
        return fail("plan blocked promotion fixture");
    blocked_follower->core.position = fixed3_from_fixed2((fixed2_t){-FIXED_ONE, -FIXED_ONE}, 0);
    P_NavBeginTick();
    Z_SquadLeave(blocked_leader);
    if (blocked_follower->z.squad_slot != UINT8_MAX || P_HasMoveOrder(blocked_follower) ||
        blocked_follower->movement.path.count) return fail("failed replan leaves no copied or stale path");
    rts_game_model_destroy(model);
    return 0;
}
