#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fail(const char *msg) { return rts_fail("z capture", msg); }

static RtsGameModel *load(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/Z" };
    if (model && rts_game_model_load(model, &config)) {
        P_AiSetFeatures(rts_game_model_ai(model), 0);
        return model;
    }
    rts_game_model_destroy(model);
    return NULL;
}

static bool alive(const mobj_t *target) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if ((const mobj_t *)th == target && th->function == P_MobjThinker) return true;
    return false;
}

/* The `n`th live thing of `type` (any team when team < 0). */
static mobj_t *nth(int type, int team, int n) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->type_id == type && mo->hp > 0 &&
            (team < 0 || mo->team == team) && n-- == 0) return mo;
    }
    return NULL;
}

static mobj_t *flag_of(int territory) {
    for (int i = 0; ; ++i) {
        mobj_t *flag = nth(MT_FLAG, -1, i);
        if (!flag || flag->z.territory == territory + 1) return flag;
    }
}

/* Nobody fights, so the tests watch only what they set up. */
static void disarm(void) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        ((mobj_t *)th)->traits &= ~MF_ATTACK;
}

static void place(mobj_t *mo, ivec2_t cell) {
    mo->core.position = fixed3_from_fixed2(fixed2_cell_center(cell), mo->core.position.z);
    P_ClearMove(mo);
    mo->movement.goal = fixed3_xy(mo->core.position);
    mo->movement.order_arrived = true;
}

static void place_crate(mobj_t *box, ivec2_t cell) {
    isize2_t size = {0};
    Z_SpriteSize(level.native_data, states[mobjinfo[box->type_id].spawnstate].frame, &size);
    ivec2_t centre = fixed2_cell(fixed2_scale(fixed3_xy(box->core.position), FIXED_FROM_INT(Z_TILE)));
    Z_RestoreObjectTerrain(box, ivec2_sub(centre, (ivec2_t){size.w / 2, size.h / 2}));
    place(box, cell);
    centre = fixed2_cell(fixed2_scale(fixed3_xy(box->core.position), FIXED_FROM_INT(Z_TILE)));
    Z_StampObjectTerrain(box, box->type_id - 1, ivec2_sub(centre, (ivec2_t){size.w / 2, size.h / 2}));
}

static bool tick(RtsGameModel *model, int tics) {
    RtsRenderSnapshot snap;
    for (int t = 0; t < tics; ++t)
        if (!rts_tick(model, &snap)) return false;
    return true;
}

/* Level 1 plants eight flags (CPUPLR01.DAT's first territory has none). A
 * Grunt on a neutral flag takes it and the building standing in its
 * territory, and cheers; the computer cannot take it back at once. */
static int test_flags(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load level 1");
    const zmap_t *map = level.native_data;
    int flags = 0;
    while (nth(MT_FLAG, -1, flags)) flags++;
    if (flags != 8) return fail("eight flags");
    int total;
    if (Z_TerritoriesHeld(0, &total) != 1 || total != 9) return fail("production counts all nine territories");
    if (flag_of(0) || !flag_of(1) || flag_of(1)->team != 1 || flag_of(7)->team != 0 || flag_of(3)->team != Z_NEUTRAL)
        return fail("flags carry their territories' teams");
    disarm();
    const zterritory_t *west = &map->territories[3];
    mobj_t *building = NULL;
    for (int i = 0; !building && nth(MT_ROBOT_FACTORY, -1, i); ++i) {
        mobj_t *mo = nth(MT_ROBOT_FACTORY, -1, i);
        ivec2_t cell = fixed2_cell(fixed3_xy(mo->core.position));
        if (cell.x >= west->min.x && cell.x < west->max.x && cell.y >= west->min.y && cell.y < west->max.y)
            building = mo;
    }
    if (!building || building->team != Z_NEUTRAL) return fail("a neutral factory in the west territory");
    mobj_t *grunt = nth(MT_UNIT_GRUNT, 0, 0), *theirs = nth(MT_UNIT_GRUNT, 1, 0);
    place(grunt, west->flag);
    if (!tick(model, 2)) return fail("tick");
    mobj_t *flag = flag_of(3);
    if (flag->team != 0 || flag->owner != 0) return fail("Grunt takes the flag");
    if (Z_TerritoriesHeld(0, &total) != 2 || total != 9) return fail("capture immediately changes production's territory share");
    if (building->team != 0 || building->owner != 0) return fail("the factory changes sides");
    if (grunt->core.state_id != S_GRUNT_CHEER) return fail("Grunt cheers");
    place(grunt, (ivec2_t){west->flag.x + 3, west->flag.y});
    place(theirs, west->flag);
    if (!tick(model, 5) || flag->team != 0) return fail("a fresh flag holds a moment");
    if (!tick(model, 20) || flag->team != 1 || building->team != 1) return fail("the computer takes it back");
    if (Z_TerritoriesHeld(0, &total) != 1 || total != 9) return fail("recapture restores production's territory share");
    puts("PASS: z flags change hands with their buildings");
    rts_game_model_destroy(model);
    return 0;
}

static int test_flag_origin_boundary(void) {
    const int types[] = {MT_UNIT_GRUNT, MT_UNIT_JEEP, MT_UNIT_FIRE_TRUCK};
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); ++i) {
        RtsGameModel *model = load();
        if (!model) return fail("load flag origin fixture");
        mobj_t *flag = flag_of(3);
        zmap_t *map = level.native_data;
        if (!flag) return fail("flag origin marker");
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            if (th->function != P_MobjThinker) continue;
            mobj_t *mo = (mobj_t *)th;
            if (Z_UnitType(mo) >= 0 || (mo->type_id == MT_FLAG && mo != flag)) P_RemoveMobj(mo);
        }
        P_RunThinkers();
        ivec2_t flag_cell = {25,25};
        map->territory_count = 1;
        map->territories[0] = (zterritory_t){.min = {0,0}, .max = {level.width,level.height}, .flag = flag_cell};
        flag->z.territory = 1;
        Z_StampFlagTerrain(flag_cell);
        mobj_t *unit = P_SpawnMobj((fixed3_t){0}, types[i]);
        if (!unit) return fail("spawn flag boundary unit");
        Z_SetTeam(unit, 0); Z_SetTeam(flag, 1);
        int left = Z_IsRobot(Z_UnitType(unit)) ? 1 : 2;
        ivec2_t origin = {(flag_cell.x - left) * Z_TILE + 7, flag_cell.y * Z_TILE};
        fixed2_t centre;
        if (!Z_UnitCentre(map, Z_UnitType(unit), origin, &centre)) return fail("flag unit origin");
        unit->core.position = fixed3_from_fixed2(centre, 0);
        map->territories[0].capture_hold = 0;
        Z_Ticker();
        if (flag->team != 1) return fail("native sample excludes seven-pixel boundary");
        origin.x++;
        if (!Z_UnitCentre(map, Z_UnitType(unit), origin, &centre)) return fail("flag unit inclusive boundary");
        unit->core.position = fixed3_from_fixed2(centre, 0);
        Z_Ticker();
        if (flag->team != 0) return fail("native sample includes eight-pixel boundary");
        if (!Z_IsRobot(Z_UnitType(unit))) {
            for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
                ivec2_t sample = ivec2_sub(flag_cell, (ivec2_t){x,y});
                origin = ivec2_sub(ivec2_scale(sample, Z_TILE), (ivec2_t){8,8});
                if (!Z_UnitCentre(map, Z_UnitType(unit), origin, &centre)) return fail("flag vehicle probe origin");
                unit->core.position = fixed3_from_fixed2(centre, 0);
                Z_SetTeam(flag, 1); map->territories[0].capture_hold = 0;
                Z_Ticker();
                if (flag->team != 0) return fail("vehicle checks all four native flag cells");
            }
        }
        rts_game_model_destroy(model);
    }
    return 0;
}

static int test_flag_terrain_and_territory(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load flag terrain fixture");
    mobj_t *west = flag_of(1), *east = flag_of(3);
    zmap_t *map = level.native_data;
    if (!west || !east) return fail("flag terrain markers");
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *mo = (mobj_t *)th;
        if (mo != west && mo != east) P_RemoveMobj(mo);
    }
    P_RunThinkers();
    memset(map->ground_attr, 0, sizeof(map->ground_attr));
    memset(map->cliffs, 0, sizeof(map->cliffs));
    Z_RebuildCliffs(&level);
    map->territory_count = 2;
    map->territories[0] = (zterritory_t){.min = {0,0}, .max = {25,level.height}, .flag = {10,10}};
    map->territories[1] = (zterritory_t){.min = {25,0}, .max = {level.width,level.height}, .flag = {25,20}};
    west->z.territory = 1; east->z.territory = 2;
    Z_SetTeam(west, 1); Z_SetTeam(east, 1);
    Z_StampFlagTerrain(map->territories[0].flag); Z_StampFlagTerrain(map->territories[1].flag);
    fixed2_t centre;
    if (!Z_UnitCentre(map, MT_UNIT_JEEP - MT_UNITS, (ivec2_t){24 * Z_TILE,20 * Z_TILE}, &centre))
        return fail("border Jeep origin");
    mobj_t *jeep = P_SpawnMobj(fixed3_from_fixed2(centre, 0), MT_UNIT_JEEP);
    if (!jeep) return fail("border Jeep");
    Z_SetTeam(jeep, 0);
    Z_Ticker();
    if (west->team != 0 || east->team != 1 || map->territories[0].team != 0 || map->territories[1].team != 1 ||
        !map->territories[0].capture_hold) return fail("neighbor marker captures territory of unit origin");
    int total = 0;
    if (Z_TerritoriesHeld(0, &total) != 1 || total != 2) return fail("captured territory contributes to production");
    mobj_t *crate = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center((ivec2_t){25,20}), 0), MT_GRENADES_2);
    mobj_t *robot = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center((ivec2_t){25,20}), 0), MT_UNIT_GRUNT);
    if (!crate || !robot) return fail("crate on flag fixture");
    Z_SetTeam(robot, 0); place_crate(crate, (ivec2_t){25,20}); place(robot, (ivec2_t){25,20});
    int index = 20 * Z_MAP_STRIDE + 25;
    if ((map->terrain[index] & 0x7f) != 0x24 || (crate->z.object_terrain & 0x7f) != 0x23)
        return fail("crate replaces and remembers flag terrain");
    uint32_t west_id = west->id, east_id = east->id, crate_id = crate->id, robot_id = robot->id, jeep_id = jeep->id;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!G_SaveGameBlob(&save, &size, "Flag and crate", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save flag and crate");
    free(save); map = level.native_data;
    map->territory_count = 2; /* The fixture's geometry is not part of a retail save. */
    map->territories[0].min = (ivec2_t){0,0}; map->territories[0].max = (ivec2_t){25,level.height};
    map->territories[0].flag = (ivec2_t){10,10};
    map->territories[1].min = (ivec2_t){25,0}; map->territories[1].max = (ivec2_t){level.width,level.height};
    map->territories[1].flag = (ivec2_t){25,20};
    west = P_MobjById(west_id); east = P_MobjById(east_id); crate = P_MobjById(crate_id); robot = P_MobjById(robot_id);
    if (!west || !east || !crate || !robot || map->territories[0].team != 0 ||
        !map->territories[0].capture_hold || (map->terrain[index] & 0x7f) != 0x24)
        return fail("save restores territory, hold and crate marker");
    Z_Ticker();
    if (east->team != 1 || !crate->remove || robot->z.grenades != 20 || (map->terrain[index] & 0x7f) != 0x23)
        return fail("crate suppresses flag until pickup restores its terrain");
    Z_Ticker();
    if (east->team != 0 || map->territories[1].team != 0) return fail("restored marker captures on next pass");
    P_RemoveMobj(west);
    map->territories[0].options |= 1; /* A fort territory may have no flag of its own. */
    map->territories[0].team = 1; map->territories[0].capture_hold = 0;
    Z_Ticker();
    if (map->territories[0].team != 0 || !map->territories[0].capture_hold || Z_TerritoriesHeld(0, &total) != 2)
        return fail("adjacent flag marker captures a flagless origin territory");
    save = NULL; size = 0;
    if (!G_SaveGameBlob(&save, &size, "Flagless capture", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save flagless capture");
    free(save); map = level.native_data;
    if (map->territories[0].team != 0 || !map->territories[0].capture_hold || !(map->territories[0].options & 1))
        return fail("save restores flagless owner, cooldown and options");
    map->territory_count = 2;
    map->territories[0].min = (ivec2_t){0,0}; map->territories[0].max = (ivec2_t){25,level.height};
    map->territories[1].min = (ivec2_t){25,0}; map->territories[1].max = (ivec2_t){level.width,level.height};
    jeep = P_MobjById(jeep_id);
    if (!jeep) return fail("restore border Jeep");
    Z_SetTeam(jeep, 1);
    int hold = map->territories[0].capture_hold;
    for (int i = 0; i < hold; ++i) {
        Z_Ticker();
        if (map->territories[0].team != 0) return fail("saved flagless cooldown remains active");
    }
    Z_Ticker();
    if (map->territories[0].team != 1) return fail("flagless territory recaptures after saved cooldown expires");
    rts_game_model_destroy(model);
    return 0;
}

static int test_flag_rebuild_over_crate(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load flag rebuild fixture");
    mobj_t *captured = flag_of(3), *covered = flag_of(1);
    zmap_t *map = level.native_data;
    if (!captured || !covered) return fail("flag rebuild markers");
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker && Z_UnitType((mobj_t *)th) >= 0) P_RemoveMobj((mobj_t *)th);
    P_RunThinkers();
    ivec2_t first = map->territories[captured->z.territory - 1].flag;
    ivec2_t second = map->territories[covered->z.territory - 1].flag;
    Z_SetTeam(captured, 1);
    Z_StampFlagTerrain(first); Z_StampFlagTerrain(second);
    mobj_t *crate = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center(second), 0), MT_GRENADES_2);
    mobj_t *robot = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center(first), 0), MT_UNIT_GRUNT);
    if (!crate || !robot) return fail("flag rebuild unit and crate");
    Z_SetTeam(robot, 0); place_crate(crate, second); place(robot, first);
    int index = second.y * Z_MAP_STRIDE + second.x;
    if ((map->terrain[index] & 0x7f) != 0x24 || (crate->z.object_terrain & 0x7f) != 0x23)
        return fail("crate initially masks flag marker");
    Z_Ticker();
    if (captured->team != 0 || crate->remove || (map->terrain[index] & 0x7f) != 0x23)
        return fail("capture rebuilds every active flag marker over existing crates");
    uint32_t crate_id = crate->id;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!G_SaveGameBlob(&save, &size, "Flag rebuild", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save flag rebuild");
    free(save); map = level.native_data; crate = P_MobjById(crate_id);
    if (!crate || crate->remove || (crate->z.object_terrain & 0x7f) != 0x23 || (map->terrain[index] & 0x7f) != 0x23)
        return fail("flag rebuild and covered crate survive save/load");
    rts_game_model_destroy(model);
    return 0;
}

static int test_flag_player_options(void) {
    static const uint8_t options[] = {0,4,8,3};
    for (int players = 2; players <= 4; ++players) {
        doomcom_t net = {.numplayers = players};
        doomcom_t *old_doomcom = doomcom;
        bool old_netgame = netgame;
        doomcom = &net; netgame = true;
        level_t loaded = {0};
        bool ok = Z_LoadLevel("data/Z/LEVEL01/LEVEL01.MAP", &loaded);
        doomcom = old_doomcom; netgame = old_netgame;
        if (!ok || ((zmap_t *)loaded.native_data)->flag_players != players)
            return fail("match player count initializes native flag rules");
        P_FreeLevel(&loaded);
        RtsGameModel *model = load();
        if (!model) return fail("load flag player options fixture");
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
            if (th->function == P_MobjThinker) P_RemoveMobj((mobj_t *)th);
        P_RunThinkers();
        zmap_t *map = level.native_data;
        memset(map->ground_attr, 0, sizeof(map->ground_attr));
        memset(map->cliffs, 0, sizeof(map->cliffs));
        memset(map->bridges, 0, sizeof(map->bridges));
        Z_RebuildCliffs(&level);
        map->flag_players = (uint8_t)players;
        zterritory_t hidden = {.options = 1};
        if (Z_FlagAvailable(map, &hidden)) return fail("option bit zero hides flags");
        hidden.options = 2;
        if (Z_FlagAvailable(map, &hidden)) return fail("option bit one hides flags");
        map->fort_count = map->placed_count = 0; map->groups[0] = 0xff;
        snprintf(map->dir, sizeof(map->dir), "/private/tmp");
        snprintf(map->objects, sizeof(map->objects), "ZF%ld.DAT", (long)getpid());
        snprintf(map->buildings, sizeof(map->buildings), "%s", map->objects);
        char path[128];
        snprintf(path, sizeof(path), "/private/tmp/%s", map->objects);
        FILE *empty = fopen(path, "wb");
        if (!empty || fclose(empty)) return fail("create empty flag fixture");
        map->territory_count = 4;
        for (int i = 0; i < 4; ++i) {
            map->territories[i] = (zterritory_t){.min = {i * 16,0}, .max = {(i + 1) * 16,level.height},
                                                 .team = 1, .flag = {i * 16 + 8,8}, .options = options[i]};
        }
        Z_LoadThings();
        remove(path);
        for (int i = 0; i < 4; ++i) {
            bool expected = i == 0 || (i == 1 && players == 2) || (i == 2 && players < 4);
            int index = 8 * Z_MAP_STRIDE + i * 16 + 8;
            if (!!flag_of(i) != expected || ((map->terrain[index] & 0x7f) == 0x23) != expected)
                return fail("startup flags and terrain follow player-count option gates");
        }
        mobj_t *robot = P_SpawnMobj((fixed3_t){0}, MT_UNIT_GRUNT);
        if (!robot) return fail("flag option capture unit");
        Z_SetTeam(robot, 0); place(robot, map->territories[0].flag);
        Z_Ticker();
        if (map->territories[0].team != 0) return fail("capture triggers flag terrain rebuild");
        for (int i = 0; i < 4; ++i) {
            bool expected = i == 0 || (i == 1 && players == 2) || (i == 2 && players < 4);
            int index = 8 * Z_MAP_STRIDE + i * 16 + 8;
            if (((map->terrain[index] & 0x7f) == 0x23) != expected)
                return fail("capture rebuild respects player-count option gates");
        }
        app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
        if (!G_SaveGameBlob(&save, &size, "Flag options", &app, rts_game_model_ai(model), &hud) ||
            !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save flag player options");
        free(save); map = level.native_data;
        if (map->flag_players != players) return fail("saved match player count survives reload");
        for (int i = 0; i < 4; ++i) {
            bool expected = i == 0 || (i == 1 && players == 2) || (i == 2 && players < 4);
            int index = 8 * Z_MAP_STRIDE + i * 16 + 8;
            if (!!flag_of(i) != expected || ((map->terrain[index] & 0x7f) == 0x23) != expected ||
                map->territories[i].options != options[i]) return fail("saved flag options and markers survive reload");
        }
        size_t extra_size = G_SaveExtraSize();
        uint8_t *extra = malloc(extra_size);
        if (!extra) return fail("allocate flag save validation fixture");
        G_SaveExtra(extra);
        uint32_t hash = G_Consistency();
        extra[0] ^= 1; /* A later validation must not partly overwrite cliffs. */
        extra[extra_size - 1] = 0;
        if (G_LoadExtra(extra, extra_size) || G_Consistency() != hash)
            return fail("zero player count rejects without mutating level");
        extra[extra_size - 1] = 5;
        if (G_LoadExtra(extra, extra_size) || G_Consistency() != hash)
            return fail("out-of-range player count rejects without mutating level");
        free(extra);
        rts_game_model_destroy(model);
    }
    return 0;
}

/* A Grunt ordered onto an empty Jeep walks over and drives it away. */
static int test_board(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for boarding");
    disarm();
    mobj_t *jeep = nth(MT_UNIT_JEEP, Z_NEUTRAL, 0), *grunt = nth(MT_UNIT_GRUNT, 0, 0);
    if (!jeep || !grunt || (jeep->traits & MF_MOBILE)) return fail("an empty Jeep and a Grunt");
    Z_SquadLeave(grunt);
    uint8_t squad = Z_AllocateSquad(0);
    grunt->z.squad = squad; grunt->z.squad_slot = UINT8_MAX;
    zmap_t *map = level.native_data;
    P_DamageMobj(jeep, NULL, jeep->hp / 4);
    if (!jeep->z.body_tracked) return fail("the empty Jeep has tracked body damage");
    ivec2_t at = fixed2_cell(fixed3_xy(jeep->core.position));
    place(grunt, (ivec2_t){at.x + 3, at.y});
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        P_MobjSetSelected((mobj_t *)th, (mobj_t *)th == grunt);
    RtsGameCommand order = { .kind = RTS_GAME_COMMAND_ATTACK_UNIT,
                             .data.attack_unit = { .target_id = jeep->id } };
    if (!rts_game_model_command(model, &order)) return fail("order the Grunt onto the Jeep");
    for (int t = 0; t < 10 * RTS_TICRATE && alive(grunt); ++t)
        if (!tick(model, 1)) return fail("tick");
    if (alive(grunt)) return fail("the Grunt gets in");
    if (jeep->team != 0 || jeep->owner != 0 || jeep->z.driver != MT_UNIT_GRUNT - MT_UNITS + 1)
        return fail("the Jeep is the player's, driven by the Grunt");
    if (!squad || jeep->z.squad != squad || map->squad_cooldown[squad - 1])
        return fail("boarding transfers the group without releasing its slot");
    if (jeep->z.body_accuracy != 60) return fail("boarding installs the new driver's accuracy");
    if ((jeep->traits & (MF_MOBILE | MF_SELECTABLE | MF_ATTACK)) != (MF_MOBILE | MF_SELECTABLE | MF_ATTACK) ||
        jeep->core.state_id != (int)mobjinfo[MT_UNIT_JEEP].spawnstate || !P_MobjIsSelected(jeep))
        return fail("the Jeep drives, fights and stays selected");
    if (jeep->core.angle != P_PointToAngle(-FIXED_ONE, 0) ||
        states[jeep->core.state_id].count < 2 || states[jeep->core.state_id].tics != 1)
        return fail("the Jeep faces west, as it stood empty, with its motor running");
    fvec2_t start = fixed3_xy_to_fvec2(jeep->core.position);
    RtsGameCommand move = { .kind = RTS_GAME_COMMAND_MOVE_SELECTED,
        .data.move_selected = { .target = fixed2_from_fvec2((fvec2_t){start.x, start.y + 4.0f}) } };
    if (!rts_game_model_command(model, &move) || !tick(model, 3 * RTS_TICRATE)) return fail("drive");
    if (fixed3_xy_to_fvec2(jeep->core.position).y < start.y + 1.0f) return fail("the Jeep moves");
    P_DamageMobj(jeep, NULL, jeep->hp + 1000);
    if (jeep->hp > 0 || map->squad_cooldown[squad - 1] != 200)
        return fail("destroying a long-lived occupied Jeep releases its group");
    puts("PASS: z a Grunt gets into an empty Jeep and drives it");
    rts_game_model_destroy(model);
    return 0;
}

/* Ordered boarding and opportunistic boarding share the native origin test. */
static int test_board_reach(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for boarding boundary");
    disarm();
    mobj_t *jeep = nth(MT_UNIT_JEEP, Z_NEUTRAL, 0), *robot = nth(MT_UNIT_GRUNT, 0, 0);
    const zmap_t *map = level.native_data;
    isize2_t size;
    if (!jeep || !robot || !Z_SpriteSize(map, states[jeep->core.state_id].frame, &size))
        return fail("boarding boundary fixture");
    ivec2_t centre = fixed2_cell(fixed2_scale(fixed3_xy(jeep->core.position), FIXED_FROM_INT(Z_TILE)));
    ivec2_t origin = ivec2_sub(centre, (ivec2_t){size.w / 2, size.h / 2});
    fixed2_t at;
    if (!Z_UnitCentre(map, Z_UnitType(robot), ivec2_add(origin, (ivec2_t){21, 0}), &at))
        return fail("boarding robot position");
    robot->core.position = fixed3_from_fixed2(at, 0);
    if (!Z_BoardOrder(&robot, 1, jeep)) return fail("order boarding at boundary");
    Z_Ticker();
    if (robot->remove || jeep->team != Z_NEUTRAL) return fail("ordered boarding rejects 21 pixels");
    if (!Z_UnitCentre(map, Z_UnitType(robot), ivec2_add(origin, (ivec2_t){-20, 0}), &at))
        return fail("boarding robot boundary position");
    robot->core.position = fixed3_from_fixed2(at, 0);
    Z_Ticker();
    if (!robot->remove || jeep->team != 0) return fail("ordered boarding accepts 20 pixels from object origin");
    rts_game_model_destroy(model);
    return 0;
}

/* A Grunt walking over a crate of 20 shares them with its squad of three;
 * then it may attack a building, which it throws them at. */
static int test_grenades(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for grenades");
    disarm();
    mobj_t *crate = nth(MT_GRENADES_2, -1, 0), *grunt = nth(MT_UNIT_GRUNT, 0, 0);
    mobj_t *factory = nth(MT_ROBOT_FACTORY, -1, 0);
    if (!crate || !grunt || !factory || !grunt->z.squad) return fail("a crate, a squad and a factory");
    Z_SetTeam(factory, 1);
    grunt->traits |= MF_ATTACK;
    if (P_CanTarget(grunt, factory)) return fail("bullets cannot hurt a building");
    place(grunt, fixed2_cell(fixed3_xy(crate->core.position)));
    if (!tick(model, 2)) return fail("tick");
    if (alive(crate)) return fail("the crate is picked up");
    if (nth(MT_BOX_BLAST, -1, 0)) return fail("picking up a crate does not trigger its explosion");
    int held = 0, members = 0;
    for (int i = 0; nth(MT_UNIT_GRUNT, 0, i); ++i) {
        const mobj_t *mo = nth(MT_UNIT_GRUNT, 0, i);
        if (mo->z.squad != grunt->z.squad) continue;
        held += mo->z.grenades;
        members++;
    }
    if (members != 3 || held != 20 || grunt->z.grenades < 6) return fail("20 grenades shared by three");
    if (!P_CanTarget(grunt, factory)) return fail("grenades reach buildings");
    ivec2_t at = fixed2_cell(fixed3_xy(factory->core.position));
    place(grunt, (ivec2_t){at.x, at.y + 5});
    int before = grunt->z.grenades, hp = factory->hp;
    bool threw = false, flew = false;
    for (int t = 0; t < 10 * RTS_TICRATE && factory->hp == hp; ++t) {
        grunt->attack.target = factory;
        if (!tick(model, 1)) return fail("tick");
        threw |= grunt->core.state_id == S_GRUNT_THROW || grunt->core.state_id == S_GRUNT_TOSS;
        flew |= nth(MT_GRENADE, -1, 0) != NULL;
    }
    if (!threw || !flew) return fail("the Grunt throws a grenade");
    if (factory->hp >= hp || grunt->z.grenades >= before) return fail("the grenade hurts the factory");
    puts("PASS: z Grunts pick up grenades and throw them at buildings");
    rts_game_model_destroy(model);
    return 0;
}

static int test_specialist_grenades(void) {
    const int types[] = {MT_UNIT_SNIPER, MT_UNIT_PSYCHO, MT_UNIT_TOUGH};
    for (int i = 0; i < 3; ++i) {
        RtsGameModel *model = load();
        if (!model) return fail("load specialist grenade fixture");
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            if (th->function != P_MobjThinker) continue;
            mobj_t *mo = (mobj_t *)th;
            if (Z_UnitType(mo) >= 0) P_RemoveMobj(mo);
        }
        P_RunThinkers();
        mobj_t *box = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center((ivec2_t){20,20}), 0), MT_GRENADES_2);
        mobj_t *robot = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center((ivec2_t){20,20}), 0), types[i]);
        if (!box || !robot) return fail("specialist grenade fixture");
        Z_SetTeam(robot, 0);
        place_crate(box, (ivec2_t){20,20});
        place(robot, (ivec2_t){20,20});
        if (Z_CrateEligible(robot, box) != (i < 2)) return fail("specialist crate order eligibility");
        if (i < 2) {
            ticcmd_t cmd = {.order = TC_ORDER, .position = box->core.position, .count = 1, .units = {robot->id}};
            G_RunTiccmd(0, &cmd);
            if (robot->z.route.target != box->id) return fail("specialist accepts explicit crate order");
        }
        Z_Ticker();
        if ((bool)box->remove != (i < 2) || robot->z.grenades != (i < 2 ? 20 : 0))
            return fail("native robot class controls crate pickup");
        if (i < 2) {
            mobj_t *tank = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center((ivec2_t){25,20}), 0), MT_UNIT_MED_TANK);
            if (!tank) return fail("specialist grenade target");
            Z_SetTeam(tank, 1);
            tank->traits &= ~(MF_ATTACK | MF_MOBILE);
            robot->attack.target = tank;
            bool threw = false;
            for (int tic = 0; tic < 6 * RTS_TICRATE && robot->z.grenades == 20; ++tic) {
                if (!tick(model, 1)) return fail("tick specialist grenade throw");
                if (nth(MT_GRENADE, -1, 0)) threw = true;
            }
            if (!threw || robot->z.grenades != 19) return fail("specialist throws picked-up grenade at a vehicle");
        } else {
            mobj_t *tank = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center((ivec2_t){25,20}), 0), MT_UNIT_MED_TANK);
            if (!tank) return fail("Tough grenade dispatch target");
            Z_SetTeam(tank, 1);
            robot->z.grenades = 1; /* A mixed group can distribute grenades to a Tough. */
            if (!Z_Attack(robot, &robot->info->attack, tank) || !nth(MT_SHELL, -1, 0) ||
                nth(MT_GRENADE, -1, 0) || robot->z.grenades != 1)
                return fail("Tough keeps its launcher against vehicles even when holding a grenade");
        }
        rts_game_model_destroy(model);
    }
    return 0;
}

static int test_tough_building_attack(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load Tough building attack");
    disarm();
    mobj_t *factory = nth(MT_ROBOT_FACTORY, -1, 0);
    mobj_t *tough = P_SpawnMobj((fixed3_t){0}, MT_UNIT_TOUGH);
    mobj_t *grunt = P_SpawnMobj((fixed3_t){0}, MT_UNIT_GRUNT);
    if (!factory || !tough || !grunt) return fail("Tough building fixture");
    Z_SetTeam(factory, 1); Z_SetTeam(tough, 0); Z_SetTeam(grunt, 0);
    if (!P_CanTarget(tough, factory) || P_CanTarget(grunt, factory) ||
        !Z_CliffWeapon(tough) || Z_CliffWeapon(grunt)) return fail("Tough launcher targets structures without grenades");
    ivec2_t at = fixed2_cell(fixed3_xy(factory->core.position));
    place(tough, (ivec2_t){at.x, at.y + 5});
    tough->attack.target = factory;
    int hp = factory->hp;
    bool shell = false;
    for (int tic = 0; tic < 10 * RTS_TICRATE && factory->hp == hp; ++tic) {
        if (!tick(model, 1)) return fail("tick Tough building attack");
        shell |= nth(MT_SHELL, 0, 0) != NULL;
    }
    if (!shell || factory->hp >= hp || tough->z.grenades)
        return fail("Tough launcher flies and damages an enemy factory");
    tough->z.grenades = 1; /* A mixed squad can allocate one to a Tough. */
    if (!Z_Attack(tough, &tough->info->attack, factory) || tough->z.grenades != 1 || nth(MT_GRENADE, 0, 0))
        return fail("Tough keeps using launcher against buildings when carrying a grenade");
    rts_game_model_destroy(model);
    return 0;
}

static int test_crate_slot_order(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load crate slot order");
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *mo = (mobj_t *)th;
        if (Z_IsRobot(Z_UnitType(mo))) P_RemoveMobj(mo);
    }
    fixed2_t at = fixed2_cell_center((ivec2_t){25, 25}), robot_at;
    mobj_t *twenty = P_SpawnMobj(fixed3_from_fixed2(at, 0), MT_GRENADES_2);
    mobj_t *ten = P_SpawnMobj(fixed3_from_fixed2(at, 0), MT_GRENADES);
    mobj_t *robot = P_SpawnMobj(fixed3_from_fixed2(at, 0), MT_UNIT_GRUNT);
    int slot = Z_FreeObjectSlot();
    if (!twenty || !ten || !robot || !slot || slot >= 200) return fail("crate slot order fixture");
    twenty->z.object_slot = (uint8_t)(slot + 1); ten->z.object_slot = (uint8_t)slot;
    zmap_t *map = level.native_data;
    int index = 25 * Z_MAP_STRIDE + 25;
    map->terrain[index] = map->ground_attr[index] = 0x96;
    place_crate(ten, (ivec2_t){25, 25}); place_crate(twenty, (ivec2_t){25, 25});
    if (ten->z.object_terrain != 0x96 || twenty->z.object_terrain != 0xa4 || map->terrain[index] != 0xa4)
        return fail("overlapping crates save preceding terrain in creation order");
    Z_SetTeam(robot, 0);
    ivec2_t centre = fixed2_cell(fixed2_scale(at, FIXED_FROM_INT(Z_TILE)));
    if (!Z_UnitCentre(level.native_data, Z_UnitType(robot), ivec2_sub(centre, (ivec2_t){8,8}), &robot_at))
        return fail("crate slot robot origin");
    robot->core.position = fixed3_from_fixed2(robot_at, 0);
    uint32_t robot_id = robot->id, ten_id = ten->id, twenty_id = twenty->id;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!G_SaveGameBlob(&save, &size, "Crate order", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save crate slot order");
    free(save);
    robot = P_MobjById(robot_id); ten = P_MobjById(ten_id); twenty = P_MobjById(twenty_id);
    if (!robot || !ten || !twenty) return fail("restore crate slot order");
    map = level.native_data;
    if (map->terrain[index] != 0xa4 || ten->z.object_terrain != 0x96 || twenty->z.object_terrain != 0xa4)
        return fail("save preserves marked terrain and each saved terrain byte");
    Z_Ticker();
    if (!ten->remove || twenty->remove || robot->z.grenades != 10 || map->terrain[index] != 0x96)
        return fail("pickup chooses the earlier effect slot rather than thinker order");
    robot->z.grenades = 0;
    Z_Ticker();
    if (twenty->remove || robot->z.grenades) return fail("restored terrain suppresses overlapping pickup");
    save = NULL; size = 0;
    if (!G_SaveGameBlob(&save, &size, "Restored terrain", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save restored terrain");
    free(save);
    map = level.native_data;
    if (map->terrain[index] != 0x96 || map->ground_attr[index] != 0x96)
        return fail("restored terrain survives reload and cliff reconstruction");
    rts_game_model_destroy(model);
    return 0;
}

static int test_object_terrain(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load object terrain");
    zmap_t *map = level.native_data;
    ivec2_t origin = {320, 320};
    int at = 20 * Z_MAP_STRIDE + 20, below = at + Z_MAP_STRIDE;
    map->terrain[at] = map->ground_attr[at] = 0x96;
    map->terrain[below] = map->ground_attr[below] = 0x96;
    mobj_t *house = P_SpawnMobj((fixed3_t){0}, MT_DESERT_HOUSE);
    mobj_t *cactus = P_SpawnMobj((fixed3_t){0}, MT_CACTUS_DES);
    if (!house || !cactus) return fail("object terrain fixture");
    Z_StampObjectTerrain(house, MT_DESERT_HOUSE - 1, origin);
    Z_StampObjectTerrain(cactus, MT_CACTUS_DES - 1, origin);
    if (map->terrain[at] != 0xa4 || house->z.object_terrain != 0x96 ||
        map->terrain[below] != 0xae || cactus->z.object_terrain != 0x96)
        return fail("object flags stamp pickup and offset obstacle terrain");
    ivec2_t other = {352, 320};
    int other_at = 20 * Z_MAP_STRIDE + 22, node = L_Index(&level, 22, 20);
    map->terrain[other_at] = map->ground_attr[other_at] = 0x89;
    level.blocked[node] = 1;
    const uint16_t *regions = P_NavRegions(&level, 0);
    if (!regions || regions[node]) return fail("blocked terrain absent from navigation region");
    mobj_t *box = P_SpawnMobj((fixed3_t){0}, MT_GRENADES);
    if (!box) return fail("navigation crate fixture");
    Z_StampObjectTerrain(box, MT_GRENADES - 1, other);
    regions = P_NavRegions(&level, 0);
    if (!regions || !regions[node]) return fail("navigation sees marker passability");
    Z_RestoreObjectTerrain(box, other);
    regions = P_NavRegions(&level, 0);
    if (!regions || regions[node]) return fail("navigation sees restored passability");
    rts_game_model_destroy(model);
    return 0;
}

static int test_crate_leader_remainder(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for grenade remainder");
    mobj_t *box = nth(MT_GRENADES_2, -1, 0);
    mobj_t *follower = P_SpawnMobj((fixed3_t){0}, MT_UNIT_GRUNT);
    mobj_t *other = P_SpawnMobj((fixed3_t){0}, MT_UNIT_GRUNT);
    mobj_t *leader = P_SpawnMobj((fixed3_t){0}, MT_UNIT_GRUNT);
    if (!box || !follower || !other || !leader) return fail("grenade remainder fixture");
    Z_SetTeam(follower, 0); Z_SetTeam(other, 0); Z_SetTeam(leader, 0);
    uint8_t squad = Z_AllocateSquad(0);
    follower->z.squad = other->z.squad = leader->z.squad = squad;
    follower->z.squad_slot = 2; other->z.squad_slot = 1; leader->z.squad_slot = UINT8_MAX;
    place_crate(box, (ivec2_t){20, 20}); place(follower, (ivec2_t){20, 20});
    place(other, (ivec2_t){1, 1}); place(leader, (ivec2_t){1, 1});
    Z_Ticker();
    if (!squad || !box->remove || leader->z.grenades != 8 || follower->z.grenades != 6 || other->z.grenades != 6)
        return fail("grenade remainder follows leader slot rather than spawn order");
    mobj_t *third = P_SpawnMobj((fixed3_t){0}, MT_UNIT_GRUNT);
    box = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center((ivec2_t){20, 20}), 0), MT_GRENADES_2);
    if (!third || !box) return fail("promoted grenade leader fixture");
    place_crate(box, (ivec2_t){20, 20});
    Z_SetTeam(third, 0);
    third->z.squad = squad; third->z.squad_slot = 3;
    follower->z.grenades = other->z.grenades = leader->z.grenades = 0;
    Z_SquadLeave(leader);
    if (other->z.squad_slot != UINT8_MAX) return fail("lowest surviving slot becomes leader");
    place(third, (ivec2_t){1, 1});
    Z_Ticker();
    if (!box->remove || other->z.grenades != 8 || follower->z.grenades != 6 || third->z.grenades != 6)
        return fail("promoted leader receives grenade remainder");
    rts_game_model_destroy(model);
    return 0;
}

static int test_crate_order(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for crate order");
    disarm();
    mobj_t *box = nth(MT_GRENADES_2, -1, 0);
    mobj_t *robot = P_SpawnMobj((fixed3_t){0}, MT_UNIT_GRUNT);
    if (!box || !robot) return fail("crate order fixture");
    Z_SetTeam(robot, 0);
    robot->core.position = box->core.position;
    ticcmd_t cmd = {.order = TC_ORDER, .position = box->core.position, .count = 1, .units = {robot->id}};
    mobj_t *fort = nth(MT_FORT, 0, 0);
    if (!fort) return fail("crate rally fixture");
    ticcmd_t rally = cmd; rally.units[0] = fort->id;
    G_RunTiccmd(0, &rally);
    if (fort->z.route.count != 1 || fort->z.route.steps[0].target ||
        !fixed2_equal(fixed3_xy(fort->z.route.steps[0].position), fixed3_xy(box->core.position)))
        return fail("factory rally over a crate remains positional");
    G_RunTiccmd(0, &cmd);
    if (robot->z.route.target != box->id || robot->z.route.active != TC_ORDER || robot->attack.target == box)
        return fail("clicking a crate records its target in the route");
    robot->z.grenades = 1;
    Z_RouteTick(robot);
    if (robot->z.route.target || robot->z.route.active)
        return fail("pickup route cancels when the squad gains grenades");
    robot->z.grenades = 0;
    G_RunTiccmd(0, &cmd);
    if (robot->z.route.target != box->id) return fail("empty squad can order another pickup");
    P_RemoveMobj(box);
    Z_RouteTick(robot);
    if (robot->z.route.target || robot->z.route.active || P_HasMoveOrder(robot))
        return fail("removed crate cancels the pickup route");
    rts_game_model_destroy(model);
    return 0;
}

static int test_crate_order_travel(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for travelling crate order");
    disarm();
    mobj_t *box = nth(MT_GRENADES_2, -1, 0);
    mobj_t *leader = P_SpawnMobj((fixed3_t){0}, MT_UNIT_GRUNT);
    mobj_t *follower = P_SpawnMobj((fixed3_t){0}, MT_UNIT_GRUNT);
    if (!box || !leader || !follower) return fail("travelling crate fixture");
    Z_SetTeam(leader, 0); Z_SetTeam(follower, 0);
    uint8_t squad = Z_AllocateSquad(0);
    leader->z.squad = follower->z.squad = squad;
    leader->z.squad_slot = UINT8_MAX; follower->z.squad_slot = 1;
    ivec2_t start = {0}, goal = {0};
    bool found = false;
    for (int y = 2; y < level.height - 2 && !found; ++y)
        for (int x = 2; x < level.width - 7; ++x)
            if (L_IsWalkable(&level, x, y) && L_IsWalkable(&level, x + 4, y)) {
                fixed2_t a = fixed2_cell_center((ivec2_t){x, y});
                fixed2_t b = fixed2_cell_center((ivec2_t){x + 4, y});
                int cls = P_MobjMoveClass(leader);
                if (!P_NavReachable(&level, cls, P_NavNodeAt(&level, cls, a), P_NavNodeAt(&level, cls, b))) continue;
                start = (ivec2_t){x, y}; goal = (ivec2_t){x + 4, y}; found = true; break;
            }
    if (!found || !squad) return fail("find reachable crate path");
    place(leader, start); place(follower, start); place_crate(box, goal);
    ticcmd_t pickup = {.order = TC_ORDER, .position = box->core.position,
                       .count = 2, .units = {leader->id, follower->id}};
    G_RunTiccmd(0, &pickup);
    if (leader->z.route.target != box->id || follower->z.route.target)
        return fail("a squad click gives its leader one pickup route");
    ticcmd_t next = {.order = TC_MOVE, .position = fixed3_from_fixed2(fixed2_cell_center(start), 0),
                     .queued = true, .count = 1, .units = {leader->id}};
    G_RunTiccmd(0, &next);
    if (leader->z.route.count != 1) return fail("movement queues after pickup");
    for (int i = 0; i < 30 * RTS_TICRATE && (alive(box) || leader->z.route.count); ++i)
        if (!tick(model, 1)) return fail("tick travelling pickup");
    if (alive(box) || leader->z.route.count || leader->z.route.active != TC_MOVE ||
        leader->z.grenades + follower->z.grenades != 20)
        return fail("travelling squad picks up and continues its queued move");
    leader->z.grenades = follower->z.grenades = 0;
    box = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center(goal), 0), MT_GRENADES_2);
    if (!box) return fail("spawn queued crate");
    pickup.position = box->core.position; pickup.queued = true;
    G_RunTiccmd(0, &pickup);
    if (leader->z.route.count != 1 || leader->z.route.steps[0].target != box->id)
        return fail("crate target queues behind movement");
    P_RemoveMobj(box);
    for (int i = 0; i < 30 * RTS_TICRATE && (leader->z.route.active || leader->z.route.count); ++i)
        if (!tick(model, 1)) return fail("tick removed queued crate");
    if (leader->z.route.count || leader->z.route.active || leader->z.grenades || follower->z.grenades)
        return fail("removed queued crate is skipped after movement");
    rts_game_model_destroy(model);
    return 0;
}

/* 0x79b25 checks native sprite origins in a strict 16px square. */
static int test_crate_reach(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for crate reach");
    disarm();
    const zmap_t *map = level.native_data;
    mobj_t *box = nth(MT_GRENADES_2, -1, 0);
    mobj_t *robot = P_SpawnMobj((fixed3_t){0}, MT_UNIT_GRUNT);
    isize2_t size;
    if (!box || !robot || !Z_SpriteSize(map, states[mobjinfo[box->type_id].spawnstate].frame, &size))
        return fail("crate reach fixture");
    Z_SetTeam(robot, 0);
    ivec2_t origin = {320, 320};
    place_crate(box, (ivec2_t){20, 20});
    const ivec2_t outside[] = {{16,0}, {-16,0}, {0,16}, {0,-16}};
    for (size_t i = 0; i < sizeof(outside) / sizeof(*outside); ++i) {
        fixed2_t at;
        if (!Z_UnitCentre(map, Z_UnitType(robot), ivec2_add(origin, outside[i]), &at))
            return fail("crate robot origin");
        robot->core.position = fixed3_from_fixed2(at, 0);
        Z_Ticker();
        if (box->remove || robot->z.grenades) return fail("crate boundary is exclusive");
    }
    fixed2_t at;
    if (!Z_UnitCentre(map, Z_UnitType(robot), ivec2_add(origin, (ivec2_t){15,15}), &at))
        return fail("crate robot origin");
    robot->core.position = fixed3_from_fixed2(at, 0);
    Z_Ticker();
    if (box->remove || robot->z.grenades) return fail("adjacent terrain does not dispatch pickup");
    if (!Z_UnitCentre(map, Z_UnitType(robot), ivec2_add(origin, (ivec2_t){7,7}), &at))
        return fail("crate marked tile origin");
    robot->core.position = fixed3_from_fixed2(at, 0);
    Z_Ticker();
    if (!box->remove || robot->z.grenades != 20) return fail("marked terrain dispatches pickup");
    rts_game_model_destroy(model);
    puts("PASS: z crate pickup uses native strict square and sprite origins");
    return 0;
}

static int test_apc_crew(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for APC crew");
    disarm();
    mobj_t *first = nth(MT_UNIT_GRUNT, 0, 0), *second = nth(MT_UNIT_GRUNT, 0, 1);
    if (!first || !second) return fail("two Grunts");
    first->z.squad = second->z.squad = 250;
    first->z.squad_slot = UINT8_MAX; second->z.squad_slot = 1;
    first->hp = 21; second->hp = 13;
    first->z.flame_phase = 3; second->z.flame_phase = 12;
    mobj_t *apc = P_SpawnMobj(first->core.position, MT_UNIT_APC);
    if (!apc) return fail("spawn APC");
    Z_SetTeam(apc, Z_NEUTRAL);
    apc->traits &= ~(MF_MOBILE | MF_ATTACK);
    P_SetMobjState(apc, S_A_P_C);
    ticcmd_t move = {.order = TC_MOVE, .position = first->core.position, .count = 1, .units = {first->id}};
    G_RunTiccmd(0, &move);
    ticcmd_t board = {.order = TC_BOARD, .target = apc->id, .position = apc->core.position,
                      .queued = true, .count = 1, .units = {first->id}};
    G_RunTiccmd(0, &board);
    if (first->z.route.count != 1) return fail("queue APC boarding after movement");
    P_ClearMove(first); first->movement.order_arrived = true;
    Z_RouteTick(first);
    if (first->z.board != apc->id || !tick(model, 2)) return fail("board APC");
    if (apc->z.crew_count != 2 || alive(first) || alive(second)) return fail("APC takes its squad");
    if (apc->z.crew[0].flame_phase != 3 || apc->z.crew[1].flame_phase != 12)
        return fail("boarding retains each crew phase");
    mobj_t *survivor = P_SpawnMobj(apc->core.position, MT_UNIT_GRUNT);
    if (!survivor) return fail("spawn outside squad survivor");
    Z_SetTeam(survivor, 0);
    survivor->z.squad = 250; survivor->z.squad_slot = UINT8_MAX;
    mobj_t *victim = nth(MT_UNIT_GRUNT, 1, 0);
    int armor_hit = Z_HitDamage(apc, NULL, apc, 5, 1);
    if (!victim || armor_hit < 0 || armor_hit > 1 || Z_HitDamage(apc, NULL, victim, 5, 1) != 5)
        return fail("each APC crew bullet uses one driver's weapon");
    int hp = victim->hp;
    if (!Z_Attack(apc, &apc->info->attack, victim) || victim->hp != hp - 10)
        return fail("APC fires one bullet per crew member");
    if (apc->z.crew[0].flame_phase != 4 || apc->z.crew[1].flame_phase != 13)
        return fail("non-Pyro APC firing advances crew phases");
    apc->core.position.z = FIXED_ONE;
    apc->type_id = MT_UNIT_GATLING;
    if (Z_EjectCrew(apc)) return fail("fort-wall crew cannot eject");
    apc->type_id = MT_UNIT_APC;
    apc->core.position.z = 0;
    apc->z.crew_hurt = apc->z.crew[0].hp;
    if (Z_EjectCrew(apc)) return fail("cannot eject a driver with no health");
    apc->z.crew_hurt = 0;
    ticcmd_t unload = {.order = TC_UNLOAD};
    mobj_t *carrier[] = {apc};
    if (!G_GameUnitCommand(0, &unload, carrier, 1, NULL) ||
        apc->team != Z_NEUTRAL || apc->z.driver || apc->z.crew_count)
        return fail("eject crew and leave empty APC");
    int found21 = 0, found13 = 0, leaders = 0, followers = 0;
    bool slots[Z_CREW_MAX] = {false};
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function != P_MobjThinker || mo->remove || mo->type_id != MT_UNIT_GRUNT || mo->team != 0) continue;
        found21 += mo->hp == 21; found13 += mo->hp == 13;
        if ((mo->hp == 21 && mo->z.flame_phase != 4) || (mo->hp == 13 && mo->z.flame_phase != 13))
            return fail("ejected crew retain their phases");
        if ((mo->hp == 21 || mo->hp == 13) && mo->z.just_left_cannon)
            return fail("APC ejection does not suppress cannon entry");
        if (mo->z.squad == 250) {
            if (mo->z.squad_slot == UINT8_MAX) ++leaders;
            else if (mo->z.squad_slot && mo->z.squad_slot < Z_CREW_MAX && !slots[mo->z.squad_slot]) {
                slots[mo->z.squad_slot] = true; ++followers;
            } else return fail("ejected squad slots are unique");
        }
    }
    if (!found21 || !found13 || leaders != 1 || followers != 2)
        return fail("ejected crew retain health and one squad leader");
    if (Z_EjectCrew(apc)) return fail("empty APC cannot eject");
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        P_MobjSetSelected((mobj_t *)th, false);
    mobj_t *other = P_SpawnMobj(apc->core.position, MT_UNIT_APC);
    if (!other) return fail("spawn second player's APC");
    Z_SetTeam(other, 1);
    other->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    int previous_player = consoleplayer;
    consoleplayer = 1;
    bool ejected = Z_EjectCrew(other);
    consoleplayer = previous_player;
    bool selected_driver = false;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && !mo->remove && mo->team == 1 &&
            mo->type_id == MT_UNIT_GRUNT && P_MobjIsSelected(mo)) selected_driver = true;
    }
    if (!ejected || !selected_driver) return fail("second player selects their ejected driver");
    puts("PASS: z APC boards and ejects its squad");
    rts_game_model_destroy(model);
    return 0;
}

static bool rejected_crew_unchanged(mobj_t *vehicle) {
    mobj_t before = *vehicle;
    uint32_t hash = G_Consistency();
    return !Z_EjectCrew(vehicle) && !memcmp(&before.z, &vehicle->z, sizeof(before.z)) &&
           before.traits == vehicle->traits && before.core.state_id == vehicle->core.state_id &&
           G_Consistency() == hash;
}

static int test_malformed_crew(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load malformed crew fixture");
    mobj_t *unit = nth(MT_UNIT_GRUNT, 0, 0);
    mobj_t *apc = unit ? P_SpawnMobj(unit->core.position, MT_UNIT_APC) : NULL;
    if (!apc) return fail("spawn malformed crew carrier");
    Z_SetTeam(apc, 0);
    apc->z.driver = UINT8_MAX;
    if (!rejected_crew_unchanged(apc)) return fail("out-of-range fallback driver is rejected");
    apc->z.driver = MT_UNIT_APC - MT_UNITS + 1;
    if (!rejected_crew_unchanged(apc)) return fail("non-robot fallback driver is rejected");
    apc->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    apc->z.crew_count = 2;
    apc->z.crew[0] = (zcrew_t){.type = MT_UNIT_GRUNT - MT_UNITS, .hp = 20};
    apc->z.crew[1] = (zcrew_t){.type = MT_UNIT_GRUNT - MT_UNITS, .hp = 12};
    apc->z.driver = UINT8_MAX;
    if (!rejected_crew_unchanged(apc)) return fail("invalid driver with stored crew is rejected");
    apc->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    apc->z.crew[1].type = UINT8_MAX;
    if (!rejected_crew_unchanged(apc)) return fail("out-of-range crew member is rejected");
    apc->z.crew[1].type = MT_UNIT_APC - MT_UNITS;
    if (!rejected_crew_unchanged(apc)) return fail("non-robot crew member is rejected");
    apc->z.crew[1].type = MT_UNIT_GRUNT - MT_UNITS;
    apc->z.crew[1].hp = 0;
    if (!rejected_crew_unchanged(apc)) return fail("dead crew member is rejected");
    apc->z.crew[1].hp = -1;
    if (!rejected_crew_unchanged(apc)) return fail("negative-health crew member is rejected");
    apc->z.crew[1].hp = 12;
    apc->z.crew_count = UINT8_MAX;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    uint32_t id = apc->id, hash = G_Consistency();
    if (!G_SaveGameBlob(&save, &size, "Malformed crew", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud))
        return fail("save and load oversized crew count");
    free(save);
    apc = P_MobjById(id);
    if (!apc || apc->z.crew_count != UINT8_MAX || G_Consistency() != hash || !rejected_crew_unchanged(apc))
        return fail("oversized saved crew is bounded and rejected without changes");
    apc->z.crew_count = 2;
    if (!Z_EjectCrew(apc)) return fail("valid crew still ejects after malformed cases");
    rts_game_model_destroy(model);
    puts("PASS: z malformed crew records are rejected without changes");
    return 0;
}

int main(void) {
    if (test_flags() || test_flag_origin_boundary() || test_flag_terrain_and_territory() || test_flag_rebuild_over_crate() || test_flag_player_options() || test_board() || test_board_reach() || test_grenades() || test_specialist_grenades() || test_tough_building_attack() || test_crate_slot_order() || test_object_terrain() || test_crate_leader_remainder() || test_crate_order() || test_crate_order_travel() || test_crate_reach() || test_apc_crew() || test_malformed_crew()) return 1;
    return 0;
}
