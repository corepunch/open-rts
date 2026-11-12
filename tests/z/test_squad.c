#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

static int fail(const char *message) { return rts_fail("z squad", message); }

static void ground_click(app_t *app, mobj_t *unit, fixed2_t point, SDL_Keymod mods) {
    float sx, sy;
    R_MapToScreen(app, &level, fixed_to_float(point.x), fixed_to_float(point.y), &sx, &sy);
    SDL_Event event = {.button = {.type = SDL_MOUSEBUTTONDOWN, .button = SDL_BUTTON_LEFT,
                                  .x = (int)sx, .y = (int)sy}};
    mobj_t *units[] = {unit};
    SDL_SetModState(mods);
    G_Responder(app, &level, units, 1, NULL, NULL, gameinfo, &event);
    event.button.type = SDL_MOUSEBUTTONUP;
    G_Responder(app, &level, units, 1, NULL, NULL, gameinfo, &event);
    SDL_SetModState(KMOD_NONE);
}

int main(void) {
    blob_t trig = {0};
    if (!Z_ReadPac("data/Z/WARDATA.PAC", "SINCOS.DAT", &trig) || trig.size < 1154)
        return fail("load native formation trig");
    for (int d = 0; d < 8; ++d) {
        int n = d * 128;
        int s = (int16_t)((trig.bytes[n] << 8) | trig.bytes[n + 1]);
        int c = (int16_t)((trig.bytes[n + 256] << 8) | trig.bytes[n + 257]);
        s >>= 5; c >>= 5;
        ivec2_t expected = {((-16 * c) >> 10) + ((16 * s) >> 10),
                            ((16 * c) >> 10) - ((-16 * s) >> 10)};
        if (!ivec2_equal(Z_FormationOffset(0, 1, d), expected)) return fail("native eight-way formation rotation");
    }
    W_FreeFile(&trig);
    blob_t exe = {0};
    enum { GRID_FILE = 0x11bdd4, GRID_SIZE = 225 };
    if (!W_ReadFile("data/Z/ZED.EXE", &exe) || exe.size < GRID_FILE + 5 * GRID_SIZE)
        return fail("load native formation grids");
    for (int g = 0; g < 5; ++g) {
        ivec2_t slots[20] = {{0}};
        for (int p = 0; p < GRID_SIZE; ++p) {
            int slot = exe.bytes[GRID_FILE + g * GRID_SIZE + p];
            if (slot > 0 && slot <= 20) slots[slot - 1] = (ivec2_t){(p % 15 - 7) * 16, (p / 15 - 7) * 16};
        }
        for (int i = 0; i < 20; ++i) {
            ivec2_t expected = {(slots[i].x * 1023) >> 10, (slots[i].y * 1023) >> 10};
            if (!ivec2_equal(Z_FormationOffset(g, i + 1, 0), expected)) return fail("authored native formation slot");
        }
    }
    W_FreeFile(&exe);
    if (!ivec2_equal(Z_FormationOffset(2, 19, 0), (ivec2_t){0, 0}) ||
        !ivec2_equal(Z_FormationOffset(3, 1, 0), (ivec2_t){-16, 0}) ||
        !ivec2_equal(Z_FormationOffset(5, 1, 0), (ivec2_t){0, 0}))
        return fail("native grid slots and bounds");
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
    leader->z.detour.phase = Z_DETOUR_TRAVEL;
    leader->z.detour.resume = goal;
    leader->movement.goal = fixed2_cell_center((ivec2_t){22, 22});
    leader->z.route = (zroute_t){.active = TC_MOVE, .count = 1, .owner = leader->owner,
                                  .steps = {{.order = TC_MOVE, .position = at}}};
    first->z.board = enemy->id;
    first->z.service = enemy->id;
    P_MobjSetSelected(leader, true);
    Z_SquadLeave(leader);
    leader->z.detour = (zdetour_t){0};
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
    mobj_t *moving = P_SpawnMobj(at, MT_UNIT_GRUNT);
    mobj_t *follower = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center((ivec2_t){20, 25}), 0), MT_UNIT_GRUNT);
    if (!moving || !follower) return fail("spawn formation fixture");
    moving->z.squad = follower->z.squad = 253;
    moving->z.squad_slot = UINT8_MAX; follower->z.squad_slot = 1;
    moving->z.formation = 3;
    moving->traits &= ~MF_ATTACK; follower->traits &= ~MF_ATTACK;
    ticcmd_t group = {.order = TC_MOVE, .count = 1, .units = {follower->id},
                      .position = fixed3_from_fixed2(goal, 0)};
    G_RunTiccmd(0, &group);
    if (!moving->z.formation_active || !P_HasMoveOrder(moving) || follower->z.route.active ||
        !moving->move_only || !follower->move_only)
        return fail("follower order installs one leader route");
    group.order = TC_ORDER; group.queued = true; group.count = 2;
    group.units[0] = follower->id; group.units[1] = moving->id;
    group.position = fixed3_from_fixed2(fixed2_cell_center((ivec2_t){26, 21}), 0);
    G_RunTiccmd(0, &group);
    if (moving->z.route.count != 1 || follower->z.route.count || follower->z.route.active)
        return fail("selected group deduplicates a single queued leader order");
    fixed2_t follower_start = fixed3_xy(follower->core.position);
    for (int i = 0; i < 35; ++i) if (!rts_tick(model, NULL)) return fail("tick formation movement");
    if (fixed2_equal(follower_start, fixed3_xy(follower->core.position)))
        return fail("follower moves toward rotated native slot");
    uint32_t moving_id = moving->id, follower_id = follower->id;
    hash = G_Consistency(); save = NULL; size = 0;
    if (!G_SaveGameBlob(&save, &size, "Moving formation", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save moving formation");
    free(save);
    moving = P_MobjById(moving_id); follower = P_MobjById(follower_id);
    if (!moving || !follower || G_Consistency() != hash || moving->z.formation != 3 ||
        !moving->z.formation_active || moving->z.route.count != 1 || follower->z.squad_slot != 1)
        return fail("load preserves leader formation and single queued route");
    fixed2_t here = fixed3_xy(moving->core.position);
    moving->movement.path = (navpath_t){.count = 1, .points = {fixed2_add(here, (fixed2_t){0, -FIXED_ONE})}};
    moving->movement.order_arrived = false;
    Z_SquadTick(moving);
    if (moving->z.formation_direction != 0) return fail("leader turn rotates formation north");
    moving->movement.path.points[0] = fixed2_add(here, (fixed2_t){FIXED_ONE, 0});
    Z_SquadTick(moving);
    if (moving->z.formation_direction != 6) return fail("leader turn rotates formation east");
    P_ClearMove(follower);
    follower->core.position = fixed3_from_fixed2(follower_start, 0);
    follower->z.detour.phase = Z_DETOUR_CHEER;
    Z_SquadTick(follower);
    if (P_HasMoveOrder(follower)) return fail("formation does not interrupt diversion");
    follower->z.detour.phase = 0;
    Z_SquadTick(follower);
    if (!P_HasMoveOrder(follower)) return fail("idle follower rejoins moving leader");
    P_ClearMove(moving); P_ClearMove(follower);
    follower->core.position = fixed3_from_fixed2(follower_start, 0);
    follower->z.formation_rejoin = true;
    hash = G_Consistency(); save = NULL; size = 0;
    if (!G_SaveGameBlob(&save, &size, "Pending rejoin", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save pending rejoin");
    free(save);
    moving = P_MobjById(moving_id); follower = P_MobjById(follower_id);
    if (!moving || !follower || G_Consistency() != hash || !follower->z.formation_rejoin)
        return fail("load preserves pending formation rejoin");
    Z_SquadTick(follower);
    if (!P_HasMoveOrder(follower) || follower->z.formation_rejoin)
        return fail("follower rejoins stopped leader once");
    moving->movement.order_arrived = true;
    Z_RouteTick(moving);
    if (moving->move_only || follower->move_only || moving->z.route.count)
        return fail("queued attack-enabled movement replaces force-move for whole squad");
    P_ClearMove(follower);
    follower->z.formation_rejoin = true;
    group.order = TC_STOP; group.queued = false; group.count = 1; group.units[0] = follower->id;
    G_RunTiccmd(0, &group);
    if (moving->z.formation_active || follower->z.formation_rejoin || P_HasMoveOrder(moving) || P_HasMoveOrder(follower))
        return fail("follower stop cancels group movement");
    mobj_t *soldier = P_SpawnMobj(at, MT_UNIT_GRUNT);
    mobj_t *opponent = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center((ivec2_t){21, 20}), 0), MT_UNIT_GRUNT);
    if (!soldier || !opponent) return fail("spawn combat modifier fixture");
    Z_SetTeam(opponent, 1);
    opponent->traits &= ~MF_ATTACK;
    P_MobjSetSelected(soldier, true);
    app_t input = {.win = {2048, 2048}, .cell = {32, 32}};
    P_UpdateSight();
    ground_click(&input, soldier, goal, KMOD_ALT | KMOD_CTRL);
    if (!soldier->move_only || soldier->z.route.active != TC_MOVE || !P_HasMoveOrder(soldier))
        return fail("Alt wins over Ctrl on actual ground click");
    int health = opponent->hp;
    for (int i = 0; i < 5; ++i) if (!rts_tick(model, NULL)) return fail("tick force move");
    if (opponent->hp != health || soldier->attack.target) return fail("force move suppresses automatic combat");
    P_ProvokeMobj(soldier, opponent);
    if (soldier->attack.target != opponent) return fail("force move retaliation fixture");
    for (int i = 0; i < 5; ++i) if (!rts_tick(model, NULL)) return fail("tick provoked force move");
    if (opponent->hp != health) return fail("force move suppresses a provoked shot until movement ends");
    ground_click(&input, soldier, fixed2_cell_center((ivec2_t){26, 21}), KMOD_CTRL | KMOD_SHIFT);
    if (soldier->z.route.count != 1 || soldier->z.route.steps[0].order != TC_ORDER)
        return fail("Ctrl Shift click queues attack-enabled movement");
    P_ClearMove(soldier); soldier->movement.order_arrived = true;
    Z_RouteTick(soldier);
    if (soldier->move_only || soldier->z.route.active != TC_ORDER) return fail("queued Ctrl move enables combat");
    for (int i = 0; i < 20 && opponent->hp == health && !soldier->attack.target; ++i)
        if (!rts_tick(model, NULL)) return fail("tick attack move");
    if (opponent->hp == health && !soldier->attack.target) return fail("attack-enabled move engages enemy");
    rts_game_model_destroy(model);
    return 0;
}
