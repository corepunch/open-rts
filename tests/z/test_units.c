#include "t_local.h"
#include "engine.h"
#include "../../games/z/info.h"

#include <math.h>
#include <stdio.h>

static int fail(const char *msg) { return rts_fail("z", msg); }

static int count(uint8_t team, int first_type, int last_type) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (mo->team == team && mo->type_id >= first_type && mo->type_id <= last_type && mo->hp > 0) n++;
    }
    return n;
}

static mobj_t *find(uint8_t team, int type) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo->team == team && mo->type_id == type && mo->hp > 0) return mo;
    }
    return NULL;
}

static bool alive(const mobj_t *target) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if ((const mobj_t *)th == target) return true;
    return false;
}

static RtsGameModel *load_map(const char *map) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/Z", .map_path = map };
    if (model && rts_game_model_load(model, &config)) return model;
    rts_game_model_destroy(model);
    return NULL;
}

static RtsGameModel *load(void) { return load_map(NULL); }

/* Level 1 starts both sides with LEVELS.DAT's groups 2, 2, 2 and 10: nine
 * Grunts and a Gatling. The player's wait at the fort, the computer's
 * stand where CPUPLR01.DAT puts them; four empty Jeeps wait on the roads. */
static int test_level_units(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load level 1");
    if (count(0, MT_UNIT_GRUNT, MT_UNIT_GRUNT) != 9) return fail("player has nine Grunts");
    if (count(0, MT_UNIT_GATLING, MT_UNIT_GATLING) != 1) return fail("player has a Gatling");
    if (count(1, MT_UNIT_GRUNT, MT_UNIT_GRUNT) != 9) return fail("computer has nine Grunts");
    if (count(1, MT_UNIT_GATLING, MT_UNIT_GATLING) != 1) return fail("computer has a Gatling");
    mobj_t *jeep = find(4, MT_UNIT_JEEP);
    if (count(4, MT_UNIT_JEEP, MT_UNIT_JEEP) != 4 || !jeep) return fail("four empty Jeeps");
    if (jeep->traits & (MF_SELECTABLE | MF_MOBILE | MF_ATTACK)) return fail("empty Jeeps are inert");
    mobj_t *gun = find(0, MT_UNIT_GATLING);
    if (!gun || gun->core.position.z <= 0 || (gun->traits & MF_MOBILE)) return fail("Gatling stands on the fort");
    gun = find(1, MT_UNIT_GATLING);
    if (!gun || gun->core.position.z <= 0) return fail("computer's Gatling stands on its fort");
    puts("PASS: z level 1 loads both armies and the empty Jeeps");
    rts_game_model_destroy(model);
    return 0;
}

/* Select a player unit of `type` on `map`, order it four cells south and
 * watch it walk there. */
static int test_select_and_move(const char *map, int type, const char *label) {
    RtsGameModel *model = load_map(map);
    if (!model) return fail("load for move");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot");
    int unit = -1;
    for (int i = 0; i < snap.unit_count && unit < 0; ++i)
        if (snap.units[i].owner == 0 && snap.units[i].type_id == type) unit = i;
    if (unit < 0) return fail("find the unit to move");
    fvec2_t start = snap.units[unit].position;
    RtsGameCommand select = { .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = { unit, false } };
    if (!rts_game_model_command(model, &select)) return fail("select a Grunt");
    RtsGameCommand move = { .kind = RTS_GAME_COMMAND_MOVE_SELECTED,
        .data.move_selected = { .target = fixed2_from_fvec2((fvec2_t){start.x, start.y + 4.0f}) } };
    if (!rts_game_model_command(model, &move)) return fail("order a move");
    bool walked = false;
    for (int t = 0; t < 90; ++t) {
        if (!rts_tick(model, &snap)) return fail("tick");
        walked |= states[snap.units[unit].state_id].group == 2;
    }
    if (fabsf(snap.units[unit].position.y - start.y) < 1.0f) return fail("unit moved");
    if (!walked) return fail("unit played its walk");
    printf("PASS: z %s selects, walks and moves\n", label);
    rts_game_model_destroy(model);
    return 0;
}

/* Two Grunts in range shoot until one falls, plays its death and goes. */
static int test_fight_to_death(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for combat");
    mobj_t *ours = find(0, MT_UNIT_GRUNT), *theirs = find(1, MT_UNIT_GRUNT);
    if (!ours || !theirs) return fail("find both Grunts");
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo != ours && mo != theirs && (mo->traits & MF_ATTACK)) mo->traits &= ~MF_ATTACK;
    }
    theirs->core.position = fixed3_add_planar(ours->core.position, (fixed3_t){FIXED_FROM_INT(3), 0, 0});
    RtsRenderSnapshot snap;
    mobj_t *dead = NULL;
    bool fired = false;
    for (int t = 0; t < 60 * RTS_TICRATE && !dead; ++t) {
        if (!rts_tick(model, &snap)) return fail("tick");
        fired |= states[ours->core.state_id].group == 3 || states[theirs->core.state_id].group == 3;
        if (ours->hp <= 0) dead = ours;
        else if (theirs->hp <= 0) dead = theirs;
    }
    if (!fired) return fail("Grunts fire");
    if (!dead) return fail("one Grunt dies");
    if (dead->core.state_id != S_GRUNT_DIE) return fail("dead Grunt falls");
    for (int t = 0; t < 10 * RTS_TICRATE && alive(dead); ++t)
        if (!rts_tick(model, &snap)) return fail("tick");
    if (alive(dead)) return fail("dead Grunt is removed");
    puts("PASS: z Grunts fire, die and leave");
    rts_game_model_destroy(model);
    return 0;
}

/* Every vehicle composes eight directions of track frames and a wreck. */
static int test_vehicle_sheets(void) {
    static spritecache_t cache;
    static const struct { const char *name; int frames; } vehicles[] = {
        {"MED_TANK", 3}, {"HEAVY_TANK", 3}, {"LIGHT_TANK", 3}, {"APC", 3},
        {"JEEP", 2}, {"CRANE", 3}, {"M_MISSILE", 3},
    };
    RtsGameModel *model = load();
    if (!model) return fail("load for sheets");
    if (!R_InitSprites("data/Z", &level, NULL, 0, &cache)) return fail("load sprites");
    for (size_t v = 0; v < sizeof(vehicles) / sizeof(*vehicles); ++v) {
        cachedsprite_t *entry = R_CacheFind(&cache, vehicles[v].name);
        if (!entry) return fail("vehicle sheet loads");
        const spritedef_t *def = &entry->sprite.spritedef;
        if (def->numframes != vehicles[v].frames + 1) return fail("track frames and a wreck");
        for (int f = 0; f < def->numframes; ++f) {
            int turns = f < vehicles[v].frames ? 8 : 1;
            if (def->spriteframes[f].rotations != turns) return fail("eight directions, one wreck");
            for (int r = 0; r < turns; ++r)
                if (!def->spriteframes[f].directions[r].layers) return fail("every direction drawn");
        }
    }
    R_FreeSpriteCache(&cache);
    puts("PASS: z vehicles compose eight directions and a wreck");
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    if (test_level_units() || test_select_and_move(NULL, MT_UNIT_GRUNT, "Grunt") ||
        test_select_and_move("LEVEL09/LEVEL09.MAP", MT_UNIT_MED_TANK, "Medium Tank") ||
        test_vehicle_sheets() || test_fight_to_death()) return 1;
    return 0;
}
