#include "../rts_model_test.h"
#include "../../games/dark-reign/dr_types.h"
#include "../../games/dark-reign/info.h"
#include "../../play/p_local.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *msg) { return rts_fail("dark-reign", msg); }

static int count_owner_type(const RtsRenderSnapshot *s, uint8_t owner, uint16_t type) {
    int n = 0;
    for (int i = 0; i < s->unit_count; ++i)
        if (s->units[i].owner == owner && s->units[i].type_id == type) n++;
    return n;
}

static int test_map_loads(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {
        .data_root = "data/REIGN/dark",
        .map_path = "scenario/MULTI/2NIC/2NIC.SCN",
    };
    if (!model || !rts_game_model_load(model, &config)) return fail("load 2NIC map");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot 2NIC");
    if (snap.map_width <= 0 || snap.map_height <= 0) return fail("map dimensions");
    if (snap.unit_count < 6) return fail("units spawn from SCN");
    if (count_owner_type(&snap, 0, MT_FG_CONSTRUCTION_CREW) < 1)
        return fail("player has construction crews");
    if (count_owner_type(&snap, 1, MT_FG_CONSTRUCTION_CREW) < 1)
        return fail("enemy has construction crews");
    if (snap.player_resources[0][0] <= 0 || snap.player_resources[1][0] <= 0)
        return fail("team credits from SCN");
    if (snap.resource_vent_count <= 0) return fail("resource vents from mines");
    printf("PASS: dark-reign map loads with %d units, %d vents\n",
           snap.unit_count, snap.resource_vent_count);
    rts_game_model_destroy(model);
    return 0;
}

static int test_select_and_move(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {
        .data_root = "data/REIGN/dark",
        .map_path = "scenario/MULTI/2NIC/2NIC.SCN",
    };
    if (!rts_game_model_load(model, &config)) return fail("load for select/move");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot for select/move");
    int player = -1;
    for (int i = 0; i < snap.unit_count; ++i)
        if (snap.units[i].owner == 0 && (snap.units[i].traits & RTS_RENDER_TRAIT_MOBILE)) {
            player = i; break;
        }
    if (player < 0) return fail("find player mobile unit");
    RtsGameCommand sel = { .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = { player, false } };
    if (!rts_game_model_command(model, &sel)) return fail("select");
    fvec2_t target = { snap.units[player].position.x + 3.0f,
                       snap.units[player].position.y };
    RtsGameCommand move = { .kind = RTS_GAME_COMMAND_MOVE_SELECTED,
        .data.move_selected = { .target = target } };
    if (!rts_game_model_command(model, &move)) return fail("move");
    for (int t = 0; t < 60; ++t)
        if (!rts_tick(model, &snap)) return fail("tick movement");
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot after move");
    float dx = snap.units[player].position.x - target.x;
    if (fabsf(dx) > 2.0f) return fail("unit moved toward target");
    printf("PASS: dark-reign select and move\n");
    rts_game_model_destroy(model);
    return 0;
}

static int test_production(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {
        .data_root = "data/REIGN/dark",
        .map_path = "scenario/MULTI/2NIC/2NIC.SCN",
    };
    if (!rts_game_model_load(model, &config)) return fail("load for production");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot for production");
    RtsProductDefinition products[64];
    int product_count = rts_game_model_products(model, products, 64);
    if (product_count < 10) return fail("product table has entries");
    RtsGameCommand build_hq = {
        .kind = RTS_GAME_COMMAND_ACTIVATE_UI_BUTTON,
        .data.activate_ui_button = { .ui_id = 10001 },
    };
    int resources_before = snap.player_resources[0][0];
    if (!rts_game_model_command(model, &build_hq)) return fail("build FG HQ queued");
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot after HQ queue");
    if (snap.player_resources[0][0] >= resources_before)
        return fail("HQ cost deducted");
    bool hq_built = false;
    for (int t = 0; t < 30 * 30 && !hq_built; ++t) {
        if (!rts_tick(model, &snap)) return fail("tick HQ build");
        RtsGameEvent ev;
        while (rts_game_model_poll_event(model, &ev))
            if (ev.type == RTS_GAME_EVENT_BUILDING_BUILT ||
                ev.type == RTS_GAME_EVENT_UNIT_BUILT) hq_built = true;
    }
    if (!hq_built) return fail("HQ build completed");
    if (count_owner_type(&snap, 0, MT_FG_HQ1) < 1)
        return fail("HQ was built");
    printf("PASS: dark-reign production builds HQ\n");
    rts_game_model_destroy(model);
    return 0;
}

static int test_ai_production(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {
        .data_root = "data/REIGN/dark",
        .map_path = "scenario/MULTI/2NIC/2NIC.SCN",
    };
    if (!rts_game_model_load(model, &config)) return fail("load for AI");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot for AI");
    int initial_enemy = 0;
    int initial_player = 0;
    int initial_resources = snap.player_resources[0][0];
    for (int i = 0; i < snap.unit_count; ++i) {
        initial_enemy += snap.units[i].owner == 1;
        initial_player += snap.units[i].owner == 0;
    }
    for (int t = 0; t < 30 * 120; ++t) {
        if (!rts_tick(model, NULL)) return fail("tick AI");
    }
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot after AI ticks");
    int enemies = 0, players = 0;
    for (int i = 0; i < snap.unit_count; ++i) {
        enemies += snap.units[i].owner == 1;
        players += snap.units[i].owner == 0;
    }
    if (enemies <= initial_enemy) return fail("enemy AI produced units");
    if (players > initial_player || snap.player_resources[0][0] < initial_resources)
        return fail("AI leaves human production and resources alone");
    printf("PASS: dark-reign enemy AI production (%d -> %d units)\n", initial_enemy, enemies);
    rts_game_model_destroy(model);
    return 0;
}

static int test_combat(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {
        .data_root = "data/REIGN/dark",
        .map_path = "scenario/MULTI/2NIC/2NIC.SCN",
    };
    if (!rts_game_model_load(model, &config)) return fail("load for combat");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot for combat");

    /* Require both player attack unit and enemy unit to exist initially. */
    int player = -1, enemy = -1;
    for (int i = 0; i < snap.unit_count; ++i) {
        if (player < 0 && snap.units[i].owner == 0 &&
            (snap.units[i].traits & (RTS_RENDER_TRAIT_MOBILE | RTS_RENDER_TRAIT_ATTACK)) ==
            (RTS_RENDER_TRAIT_MOBILE | RTS_RENDER_TRAIT_ATTACK))
            player = i;
        if (snap.units[i].owner == 1 &&
            (enemy < 0 || (snap.units[i].traits & RTS_RENDER_TRAIT_MOBILE)))
            enemy = i;
    }
    if (player < 0) return fail("find attack-capable player unit");
    if (enemy < 0) return fail("find enemy unit");

    /* Record initial HP of all enemy units. */
    uint32_t enemy_ids[RTS_MODEL_MAX_SNAPSHOT_UNITS];
    int enemy_hps[RTS_MODEL_MAX_SNAPSHOT_UNITS];
    int enemy_count = 0;
    for (int i = 0; i < snap.unit_count; ++i) {
        if (snap.units[i].owner == 1 && snap.units[i].hp > 0) {
            enemy_ids[enemy_count] = snap.units[i].id;
            enemy_hps[enemy_count] = snap.units[i].hp;
            enemy_count++;
        }
    }
    if (enemy_count == 0) return fail("find enemy units for HP tracking");

    fvec2_t enemy_pos = snap.units[enemy].position;
    RtsGameCommand selall = { .kind = RTS_GAME_COMMAND_SELECT_ALL_PLAYER_UNITS };
    rts_game_model_command(model, &selall);
    RtsGameCommand move = { .kind = RTS_GAME_COMMAND_MOVE_SELECTED,
        .data.move_selected = { .target = enemy_pos } };
    rts_game_model_command(model, &move);

    /* Tick up to 5 min; any enemy HP drop or death counts as combat. */
    bool saw_damage = false;
    for (int t = 0; t < 30 * 300 && !saw_damage; ++t) {
        if (!rts_tick(model, &snap)) return fail("tick combat");
        if (t % 90 == 0) {
            rts_game_model_command(model, &selall);
            rts_game_model_command(model, &move);
        }
        for (int j = 0; j < enemy_count && !saw_damage; ++j) {
            int idx = rts_find_unit_by_id(&snap, enemy_ids[j]);
            if (idx < 0 || snap.units[idx].hp < enemy_hps[j])
                saw_damage = true;
        }
    }
    if (!saw_damage) return fail("enemy took damage in combat");
    printf("PASS: dark-reign combat (enemy HP decreased)\n");
    rts_game_model_destroy(model);
    return 0;
}

static mobj_t *player_harvester(void) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *unit = (mobj_t *)th;
        if (!unit->remove && unit->hp > 0 && unit->owner == 0 &&
            (unit->traits & MF_HARVESTER))
            return unit;
    }
    return NULL;
}

static int nearest_vent_index(fvec2_t position) {
    int best = -1;
    float best_dist2 = 1e30f;
    for (int i = 0; i < level.resource_vent_count; ++i) {
        const resourcevent_t *vent = &level.resource_vents[i];
        if (!vent->active || vent->amount <= 0) continue;
        float dist2 = fvec2_distance_squared(vent->attachment, position);
        if (dist2 < best_dist2) {
            best_dist2 = dist2;
            best = i;
        }
    }
    return best;
}

static int test_harvesting(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {
        .data_root = "data/REIGN/dark",
        .map_path = "scenario/FIXED/M01F/M01F.SCN",
    };
    if (!model || !rts_game_model_load(model, &config)) return fail("load M01F for harvest");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot M01F harvest");
    /* M01F has 4 Taelon mines (impmn) and 6 water extractors (impww). */
    if (snap.resource_vent_count < 10) return fail("water wells and taelon mines are vents");

    mobj_t *harvester = player_harvester();
    if (!harvester) return fail("find starting Freighter");
    int vent_index = nearest_vent_index(fixed3_xy_to_fvec2(harvester->core.position));
    if (vent_index < 0) return fail("find nearest extractor");
    const resourcevent_t *vent = &level.resource_vents[vent_index];
    if (vent->footprint.w != 3 || vent->footprint.h != 3)
        return fail("extractor footprint is 3x3");

    int harvester_index = rts_find_unit_by_id(&snap, harvester->id);
    if (harvester_index < 0) return fail("snapshot has Freighter");
    RtsGameCommand sel = { .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = { harvester_index, false } };
    if (!rts_game_model_command(model, &sel)) return fail("select Freighter");
    /* Click the north-west cell of the 3x3 pit, not the attachment centre. */
    fvec2_t pit_corner = { (float)vent->cell.x + 0.25f, (float)vent->cell.y + 0.25f };
    RtsGameCommand harvest = { .kind = RTS_GAME_COMMAND_HARVEST_SELECTED,
        .data.harvest_selected = { .target = pit_corner } };
    if (!rts_game_model_command(model, &harvest)) return fail("order Freighter onto extractor");

    bool mining = false;
    for (int t = 0; t < 30 * 90 && !mining; ++t) {
        if (!rts_tick(model, &snap)) return fail("tick harvest");
        mining = harvester->harvest.phase == HARVEST_PHASE_MINING &&
                 harvester->core.state_id >= S_UCFRGST0_HARVEST1 &&
                 harvester->core.state_id <= S_UCFRGST0_HARVEST15;
    }
    if (!mining) return fail("Freighter started harvest animation at the pit");
    int cargo_or_stock = harvester->harvest.cargo + snap.player_resources[0][0];
    for (int t = 0; t < 30 * 20; ++t) {
        if (!rts_tick(model, &snap)) return fail("tick mining");
        if (harvester->harvest.cargo + snap.player_resources[0][0] > cargo_or_stock)
            break;
    }
    if (harvester->harvest.cargo + snap.player_resources[0][0] <= cargo_or_stock)
        return fail("Freighter extracted resources");
    printf("PASS: dark-reign Freighter mines with harvest animation (state %d cargo %d)\n",
           harvester->core.state_id, harvester->harvest.cargo);
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    RTS_RUN(test_map_loads());
    RTS_RUN(test_select_and_move());
    RTS_RUN(test_production());
    RTS_RUN(test_ai_production());
    RTS_RUN(test_combat());
    RTS_RUN(test_harvesting());
    return 0;
}
