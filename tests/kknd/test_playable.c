#include "../rts_model_test.h"
#include "../../games/kknd/info.h"
#include "game.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *msg) { return rts_fail("kknd", msg); }

static int count_owner(const RtsRenderSnapshot *s, uint8_t owner) {
    int n = 0;
    for (int i = 0; i < s->unit_count; ++i)
        if (s->units[i].owner == owner) n++;
    return n;
}

static int count_owner_type(const RtsRenderSnapshot *s, uint8_t owner, uint16_t type) {
    int n = 0;
    for (int i = 0; i < s->unit_count; ++i)
        if (s->units[i].owner == owner && s->units[i].type_id == type) n++;
    return n;
}

static int test_map_and_units(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/KKND" };
    if (!model || !rts_game_model_load(model, &config)) return fail("load default map");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot");
    if (snap.map_width <= 0 || snap.map_height <= 0) return fail("map dimensions");
    if (snap.unit_count != 33) return fail("native CPLC unit count");
    if (count_owner(&snap, 0) != 13) return fail("native player formation");
    if (count_owner(&snap, 1) != 20) return fail("native enemy formation");
    if (count_owner_type(&snap, 0, MT_SURV_RIFLEMAN) != 10)
        return fail("native Survivor infantry");
    if (count_owner_type(&snap, 0, MT_SURV_DIRT_BIKE) != 2)
        return fail("native Survivor bikes");
    if (count_owner_type(&snap, 0, MT_SURV_4X4_PICKUP) != 1)
        return fail("native Survivor pickup");
    if (count_owner_type(&snap, 1, MT_MUTE_BERSERKER) != 17)
        return fail("native mutant berserkers");
    if (count_owner_type(&snap, 1, MT_MUTE_DIRE_WOLF) != 3)
        return fail("native mutant wolves");
    if (count_owner_type(&snap, 0, MT_SURV_DRILLRIG) != 0 ||
        count_owner_type(&snap, 1, MT_MUTE_DRILLRIG) != 0 ||
        count_owner_type(&snap, 0, MT_SURV_OUTPOST) != 0)
        return fail("no synthetic starting buildings");
    fvec2_t player_sum = { 0.0f, 0.0f };
    for (int i = 0; i < snap.unit_count; ++i)
        if (snap.units[i].owner == 0)
            player_sum = fvec2_add(player_sum, snap.units[i].position);
    if (!level.has_camera || !fvec2_near(level.camera,
            fvec2_scale(player_sum, 1.0f / 13.0f), 0.01f))
        return fail("camera follows native player formation");
    if (snap.player_resources[0][0] <= 0) return fail("player starting resources");
    if (snap.player_resources[1][0] <= 0) return fail("enemy starting resources");
    printf("PASS: kknd native mission loads with %d units, %d vents, %d/%d oil\n",
           snap.unit_count, snap.resource_vent_count,
           snap.player_resources[0][0], snap.player_resources[1][0]);
    rts_game_model_destroy(model);
    return 0;
}

static int test_select_and_move(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/KKND" };
    if (!rts_game_model_load(model, &config)) return fail("load for move");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot");
    int player = -1;
    for (int i = 0; i < snap.unit_count; ++i)
        if (snap.units[i].owner == 0 && snap.units[i].type_id == MT_SURV_RIFLEMAN &&
            (snap.units[i].traits & RTS_RENDER_TRAIT_MOBILE)) {
            player = i; break;
        }
    if (player < 0) return fail("find player mobile unit");
    fvec2_t start = snap.units[player].position;
    RtsGameCommand sel = { .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = { player, false } };
    if (!rts_game_model_command(model, &sel)) return fail("select");
    fvec2_t target = { start.x - 3.0f, start.y };
    RtsGameCommand move = { .kind = RTS_GAME_COMMAND_MOVE_SELECTED,
        .data.move_selected = { .target = target } };
    if (!rts_game_model_command(model, &move)) return fail("move");
    for (int t = 0; t < 90; ++t)
        if (!rts_tick(model, &snap)) return fail("tick");
    float dx = snap.units[player].position.x - start.x;
    if (fabsf(dx) < 0.3f) return fail("unit moved");
    printf("PASS: kknd select and move\n");
    rts_game_model_destroy(model);
    return 0;
}

static int test_native_start_has_no_production(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/KKND" };
    if (!rts_game_model_load(model, &config)) return fail("load for production");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot");
    RtsProductDefinition products[64];
    int product_count = rts_game_model_products(model, products, 64);
    if (product_count != 0) return fail("native production remains unimplemented");
    RtsGameCommand build = {
        .kind = RTS_GAME_COMMAND_ACTIVATE_UI_BUTTON,
        .data.activate_ui_button = { .ui_id = 1 },
    };
    if (rts_game_model_command(model, &build)) return fail("native start has no producer");
    printf("PASS: kknd native mission has no synthetic production base\n");
    rts_game_model_destroy(model);
    return 0;
}

static int test_ai_production(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/KKND" };
    if (!rts_game_model_load(model, &config)) return fail("load for AI");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot");
    int initial_enemy = 0;
    int initial_player = 0;
    int initial_resources = snap.player_resources[0][0];
    for (int i = 0; i < snap.unit_count; ++i) {
        initial_enemy += snap.units[i].owner == 1;
        initial_player += snap.units[i].owner == 0;
    }
    for (int t = 0; t < 30 * 60; ++t)
        if (!rts_tick(model, NULL)) return fail("tick AI");
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot after AI");
    int enemies = 0, players = 0;
    for (int i = 0; i < snap.unit_count; ++i) {
        enemies += snap.units[i].owner == 1;
        players += snap.units[i].owner == 0;
    }
    if (enemies != initial_enemy) return fail("enemy AI did not invent a base");
    if (players > initial_player || snap.player_resources[0][0] < initial_resources)
        return fail("AI leaves human production and resources alone");
    printf("PASS: kknd enemy AI preserves native force (%d units)\n", enemies);
    rts_game_model_destroy(model);
    return 0;
}

static int test_combat(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/KKND" };
    if (!rts_game_model_load(model, &config)) return fail("load for combat");
    RtsRenderSnapshot snap;
    if (!rts_game_model_snapshot(model, &snap)) return fail("snapshot");

    int player = -1, enemy = -1;
    for (int i = 0; i < snap.unit_count; ++i)
        if (player < 0 && snap.units[i].owner == 0 &&
            (snap.units[i].traits & (RTS_RENDER_TRAIT_MOBILE | RTS_RENDER_TRAIT_ATTACK)) ==
            (RTS_RENDER_TRAIT_MOBILE | RTS_RENDER_TRAIT_ATTACK))
            player = i;
    for (int i = 0; i < snap.unit_count; ++i)
        if (snap.units[i].owner == 1 && (enemy < 0 ||
            fvec2_distance_squared(snap.units[player].position, snap.units[i].position) <
            fvec2_distance_squared(snap.units[player].position, snap.units[enemy].position)))
            enemy = i;
    if (player < 0) return fail("find attack-capable player unit");
    if (enemy < 0) return fail("find enemy unit");

    uint32_t player_id = snap.units[player].id;
    uint32_t enemy_id  = snap.units[enemy].id;
    int initial_hp  = snap.units[enemy].hp;
    fvec2_t enemy_pos = snap.units[enemy].position;

    RtsGameCommand sel = { .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = { player, false } };
    rts_game_model_command(model, &sel);
    RtsGameCommand move = { .kind = RTS_GAME_COMMAND_MOVE_SELECTED,
        .data.move_selected = { .target = enemy_pos } };
    rts_game_model_command(model, &move);

    bool saw_damage = false;
    for (int t = 0; t < 30 * 120 && !saw_damage; ++t) {
        if (!rts_tick(model, &snap)) return fail("tick combat");
        if (t % 60 == 0) {
            int pidx = rts_find_unit_by_id(&snap, player_id);
            int eidx2 = rts_find_unit_by_id(&snap, enemy_id);
            if (pidx >= 0) {
                RtsGameCommand resel = { .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
                    .data.select_unit_index = { pidx, false } };
                rts_game_model_command(model, &resel);
                if (eidx2 >= 0) {
                    enemy_pos = snap.units[eidx2].position;
                    RtsGameCommand atk = { .kind = RTS_GAME_COMMAND_ATTACK_UNIT,
                        .data.attack_unit = { enemy_id, eidx2 } };
                    if (!rts_game_model_command(model, &atk)) {
                        RtsGameCommand mv = { .kind = RTS_GAME_COMMAND_MOVE_SELECTED,
                            .data.move_selected = { .target = enemy_pos } };
                        rts_game_model_command(model, &mv);
                    }
                } else {
                    RtsGameCommand mv = { .kind = RTS_GAME_COMMAND_MOVE_SELECTED,
                        .data.move_selected = { .target = enemy_pos } };
                    rts_game_model_command(model, &mv);
                }
            }
        }
        int eidx = rts_find_unit_by_id(&snap, enemy_id);
        if (eidx < 0 || snap.units[eidx].hp < initial_hp)
            saw_damage = true;
    }
    if (!saw_damage) return fail("enemy took damage in combat");
    printf("PASS: kknd combat (enemy HP decreased)\n");
    rts_game_model_destroy(model);
    return 0;
}

static int test_metadata_consistency(void) {
    int mismatches = 0;
    for (int i = 0; i < NUMMOBJTYPES; ++i) {
        if (!(mobjinfo[i].flags & MF_ATTACK) && mobjinfo[i].missilestate == S_NULL)
            continue;
        bool found = false;
        for (int j = 0; j < num_actor_types; ++j) {
            if (actor_types[j].id != (uint16_t)i) continue;
            found = true;
            if (!(actor_types[j].traits & MF_ATTACK) || actor_types[j].attack.damage <= 0) {
                printf("FAIL: mobjinfo[%d] claims attack (flags=0x%x missilestate=%d) "
                       "but ActorType has no attack (traits=0x%x damage=%d)\n",
                       i, mobjinfo[i].flags, mobjinfo[i].missilestate,
                       actor_types[j].traits, actor_types[j].attack.damage);
                mismatches++;
            }
            break;
        }
        if (!found) {
            printf("WARN: mobjinfo[%d] has attack flags but no matching ActorType\n", i);
        }
    }
    if (mismatches > 0) return fail("mobjinfo/ActorType combat metadata consistent");
    printf("PASS: kknd mobjinfo/ActorType combat metadata consistent\n");
    return 0;
}

int main(void) {
    RTS_RUN(test_map_and_units());
    RTS_RUN(test_select_and_move());
    RTS_RUN(test_native_start_has_no_production());
    RTS_RUN(test_ai_production());
    RTS_RUN(test_metadata_consistency());
    RTS_RUN(test_combat());
    return 0;
}
