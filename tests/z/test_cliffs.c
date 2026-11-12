#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"
#include <stdio.h>
#include <stdlib.h>

static int fail(const char *message) { return rts_fail("z cliffs", message); }

static mobj_t *cliff_at(ivec2_t cell) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->type_id == MT_CLIFF && !mo->remove &&
            ivec2_equal(fixed2_cell(fixed3_xy(mo->core.position)), cell)) return mo;
    }
    return NULL;
}

int main(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/Z"};
    if (!model || !rts_game_model_load(model, &config)) return fail("load level one");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    zmap_t *map = level.native_data;
    int tops = 0, faces = 0;
    for (int y = 0; y < level.height; ++y)
        for (int x = 0; x < level.width; ++x) {
            int terrain = map->terrain[y * Z_MAP_STRIDE + x] & 127;
            if ((terrain >= 1 && terrain <= 6) || terrain == 18) {
                if (L_IsWalkable(&level, x, y)) return fail("every cliff top and face blocks movement");
                if (terrain == 18) faces++; else tops++;
            }
        }
    if (!tops || !faces) return fail("retail cliffs exist");
    /* A boundary cell with open ground on its left, suitable for an attack. */
    mobj_t *cliff = NULL;
    ivec2_t at = {0};
    for (int y = 1; y < level.height && !cliff; ++y)
        for (int x = 2; x < level.width && !cliff; ++x)
            if (L_IsWalkable(&level, x - 1, y) && (cliff = cliff_at((ivec2_t){x,y}))) at = (ivec2_t){x,y};
    if (!cliff || cliff->hp != 6) return fail("level one cliff durability five plus terminal threshold");
    mobj_t *grunt = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center((ivec2_t){at.x-1,at.y}),0), MT_UNIT_GRUNT);
    if (!grunt) return fail("spawn robot");
    Z_SetTeam(grunt, 0);
    if (P_TryMove(grunt, cliff->core.position)) return fail("robot cannot enter cliff");
    ticcmd_t cmd = {.order = TC_ORDER, .position = cliff->core.position, .count = 1};
    cmd.units[0] = grunt->id;
    G_RunTiccmd(0, &cmd);
    if (grunt->attack.target == cliff) return fail("bullets cannot target cliffs");
    grunt->z.grenades = 2;
    G_RunTiccmd(0, &cmd);
    if (grunt->attack.target != cliff || !P_CanTarget(grunt, cliff)) return fail("grenade order targets cliff");
    P_DamageMobj(cliff, grunt, 5);
    if (cliff->hp != 1 || cliff->remove) return fail("equal durability survives as native signed zero");
    if (!P_Attack(grunt) || grunt->z.grenades != 1) return fail("attack throws and spends a grenade");
    mobj_t *grenade = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker && ((mobj_t *)th)->type_id == MT_GRENADE) grenade = (mobj_t *)th;
    if (!grenade) return fail("grenade exists");
    grenade->core.position = cliff->core.position;
    P_ExplodeMissile(grenade);
    if (!cliff->remove) return fail("grenade blast destroys cliff");
    if (!L_IsWalkable(&level, at.x, at.y) || (map->terrain[at.y * Z_MAP_STRIDE + at.x] & 127) != 22)
        return fail("destruction leaves walkable rubble");
    if (!P_TryMove(grunt, fixed3_from_fixed2(fixed2_cell_center(at),0))) return fail("robot crosses destroyed cliff");
    /* A saved game must keep both the tile change and the live mask. */
    app_t app = {0};
    hudtext_t hud = {0};
    void *save = NULL;
    size_t size = 0;
    if (!G_SaveGameBlob(&save, &size, "Cliffs", &app, rts_game_model_ai(model), &hud)) return fail("save rubble");
    ivec2_t rubble_at = at;
    uint16_t rubble_tile = level.tile_ids[L_Index(&level, at.x, at.y)];
    /* A tank fires through the ordinary attack path at another cliff. */
    cliff = NULL;
    for (int y = 1; y < level.height && !cliff; ++y)
        for (int x = 2; x < level.width && !cliff; ++x)
            if (L_IsWalkable(&level, x - 1, y) && (cliff = cliff_at((ivec2_t){x,y}))) at = (ivec2_t){x,y};
    if (!cliff) return fail("another cliff");
    mobj_t *tank = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center((ivec2_t){at.x-1,at.y}),0), MT_UNIT_LIGHT_TANK);
    if (!tank) return fail("spawn tank");
    Z_SetTeam(tank, 0);
    cmd.position = cliff->core.position;
    cmd.units[0] = tank->id;
    G_RunTiccmd(0, &cmd);
    if (tank->attack.target != cliff || !P_Attack(tank)) return fail("tank fires at cliff");
    for (int i = 0; i < RTS_TICRATE && !cliff->remove; ++i) {
        RtsRenderSnapshot snap;
        if (!rts_tick(model, &snap)) return fail("tick tank shell");
    }
    if (!cliff->remove) return fail("tank shell destroys cliff");
    if (!G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("load rubble");
    free(save);
    if (cliff_at(rubble_at) || !cliff_at(at) || !L_IsWalkable(&level, rubble_at.x, rubble_at.y) ||
        level.tile_ids[L_Index(&level, rubble_at.x, rubble_at.y)] != rubble_tile || L_IsWalkable(&level, at.x, at.y))
        return fail("save restores destroyed and intact cliffs independently");
    printf("PASS: z %d cliff tops and %d faces block movement; grenades and shells leave traversable rubble\n", tops, faces);
    rts_game_model_destroy(model);
    return 0;
}
