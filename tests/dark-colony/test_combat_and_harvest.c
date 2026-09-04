#include "engine_config.h"
#include "engine.h"
#include "../rts_test.h"
#include "../rts_model_test.h"
#include "../../game/g_game.h"
#include "../../play/p_local.h"
#include "../../games/dark-colony/info.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *message) {
    return rts_fail("combat_and_harvest", message);
}

/* Attack: verify damage, muzzle flash, ground light, hit (blood) effect and death,
 * using two synthetic Troopers so the outcome is deterministic and independent of
 * map travel distance or emergent AI combat elsewhere on the map. */
static int assert_attack_lifecycle(void) {
    mobj_t units[2];
    memset(units, 0, sizeof(units));

    units[0].type_id = MT_DC_TROOPER;
    units[0].owner = 0;
    units[0].allegiance = ALLEGIANCE_PLAYER;
    units[0].hp = units[0].max_hp = 800;
    units[0].traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK;
    units[0].attack.range = 4.0f;
    units[0].attack.damage = 100;
    units[0].attack.cooldown_ms = 500;
    units[0].attack.target = -1;
    units[0].muzzle_flash_ms = 120;
    snprintf(units[0].muzzle_flash_name, sizeof(units[0].muzzle_flash_name), "SPRITES/BLAZ.SPR");
    snprintf(units[0].core.sprite_name, sizeof(units[0].core.sprite_name), "SPRITES/TRSC.SPR");
    units[0].core.position = fixedvec3_from_fvec2((fvec2_t){ 10.0f, 10.0f }, 0);

    units[1] = units[0];
    units[1].owner = 1;
    units[1].allegiance = ALLEGIANCE_ENEMY;
    units[1].traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE;
    units[1].attack.damage = 0;
    units[1].muzzle_flash_name[0] = '\0';
    snprintf(units[1].hit_effect_name, sizeof(units[1].hit_effect_name), "SPRITES/BLOO.SPR");

    int count = 2;
    effect_t effects[16];
    memset(effects, 0, sizeof(effects));
    level_t map;
    memset(&map, 0, sizeof(map));

    int enemy_starting_hp = units[1].hp;
    bool damage_dealt = false, saw_ground_light = false, saw_muzzle_flash = false, saw_hit_effect = false;
    /* Combat is animation-driven (muzzle flash fires on the attack-state's frame,
     * damage lands on a later frame), so observe effects over a window rather than
     * requiring them on the exact tick hp changes. */
    for (int tries = 0; tries < 200 && units[1].hp > 0; ++tries) {
        P_Ticker(&map, units, &count, effects, 16, &game_info, RTS_FIXED_DT);
        P_UpdateEffects(&map, effects, 16, &game_info, RTS_FIXED_DT);
        if (units[1].hp < enemy_starting_hp) damage_dealt = true;
        for (int i = 0; i < 16; ++i) {
            if (!effects[i].active) continue;
            if (effects[i].ground_light) saw_ground_light = true;
            if (strstr(effects[i].core.sprite_name, "BLAZ")) saw_muzzle_flash = true;
            if (strstr(effects[i].core.sprite_name, "BLOO")) saw_hit_effect = true;
        }
        if (damage_dealt && saw_ground_light && saw_muzzle_flash && saw_hit_effect) break;
    }
    if (!damage_dealt) return fail("attacker never dealt damage to enemy");
    if (!saw_ground_light) return fail("attack did not spawn a ground-light effect");
    if (!saw_muzzle_flash) return fail("attack did not spawn a visible muzzle-flash effect");
    if (!saw_hit_effect) return fail("attack did not spawn a visible hit/blood effect");

    bool died = false;
    for (int tries = 0; tries < 400 && !died; ++tries) {
        P_Ticker(&map, units, &count, effects, 16, &game_info, RTS_FIXED_DT);
        P_UpdateEffects(&map, effects, 16, &game_info, RTS_FIXED_DT);
        if (units[1].hp <= 0) died = true;
    }
    if (!died) return fail("enemy unit never died");

    bool saw_corpse = false;
    for (int tries = 0; tries < 90 && !saw_corpse; ++tries) {
        P_Ticker(&map, units, &count, effects, 16, &game_info, RTS_FIXED_DT);
        P_UpdateEffects(&map, effects, 16, &game_info, RTS_FIXED_DT);
        for (int i = 0; i < map.decoration_count; ++i) {
            if (strstr(map.decorations[i].sprite_name, "TRSC")) saw_corpse = true;
        }
    }
    if (!saw_corpse) return fail("dead unit left no corpse decoration");
    free(map.decorations);

    printf("PASS: attack lifecycle (damage, muzzle flash, ground light, blood, death, corpse)\n");
    return 0;
}

/* Regression: a hidden (not-yet-revealed) attacker must not leak visible combat effects. */
static int assert_hidden_attacker_effects_suppressed(void) {
    mobj_t attacker, target;
    memset(&attacker, 0, sizeof(attacker));
    memset(&target, 0, sizeof(target));
    attacker.hidden = true;
    attacker.hp = 800;
    attacker.max_hp = 800;
    attacker.traits = MF_SELECTABLE | MF_MOBILE | MF_ATTACK;
    attacker.attack.range = 4.0f;
    attacker.attack.damage = 100;
    attacker.attack.cooldown_ms = 500;
    snprintf(attacker.muzzle_flash_name, sizeof(attacker.muzzle_flash_name), "SPRITES/BLAZ.SPR");
    target.hp = 800;
    target.max_hp = 800;
    target.allegiance = ALLEGIANCE_PLAYER;
    attacker.allegiance = ALLEGIANCE_ENEMY;
    snprintf(target.hit_effect_name, sizeof(target.hit_effect_name), "SPRITES/BLOO.SPR");

    mobj_t units[2];
    units[0] = attacker;
    units[1] = target;
    int count = 2;
    effect_t effects[8];
    memset(effects, 0, sizeof(effects));
    level_t map;
    memset(&map, 0, sizeof(map));

    units[0].attack.target = 1;
    P_Ticker(&map, units, &count, effects, 8, &game_info, RTS_FIXED_DT);

    for (int i = 0; i < 8; ++i) {
        if (effects[i].active && effects[i].ground_light)
            return fail("hidden attacker must not spawn a ground-light effect");
        if (effects[i].active && strstr(effects[i].core.sprite_name, "BLAZ"))
            return fail("hidden attacker must not spawn a muzzle-flash effect");
    }
    printf("PASS: hidden attacker suppresses muzzle flash and ground light\n");
    return 0;
}

/* Exploiter mining: harvest order drives the deploy/work state machine and grows resources. */
static int assert_exploiter_harvest_lifecycle(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/DCOLONY", .map_path = "SCENARIO/HUMAN/HUMAN02.MAP" };
    static RtsRenderSnapshot snapshot;
    if (!model || !rts_game_model_load(model, &config)) return fail("load HUMAN02 for harvest");
    if (!rts_game_model_snapshot(model, &snapshot)) return fail("snapshot for harvest");

    int exploiter = -1;
    for (int i = 0; i < snapshot.unit_count; ++i)
        if (strstr(snapshot.units[i].sprite_name, "EXPL.SPR") != NULL) { exploiter = i; break; }
    for (int tries = 0; exploiter < 0 && tries < 30 * 30; ++tries) {
        if (!rts_tick(model, &snapshot)) return fail("tick to find exploiter");
        for (int i = 0; i < snapshot.unit_count; ++i)
            if (strstr(snapshot.units[i].sprite_name, "EXPL.SPR") != NULL) { exploiter = i; break; }
    }
    if (exploiter < 0) return fail("find player exploiter unit");
    int start_resources = snapshot.player_resources[0][0];

    RtsGameCommand select = { .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
        .data.select_unit_index = { exploiter, false } };
    RtsGameCommand harvest = { .kind = RTS_GAME_COMMAND_HARVEST_SELECTED,
        .data.harvest_selected = { .target = { 69.5f, 48.5f } } };
    if (!rts_game_model_command(model, &select)) return fail("select exploiter");
    if (!rts_game_model_command(model, &harvest)) return fail("issue harvest command");

    bool saw_harvest_target = false;
    bool saw_deploy_or_work_state = false;
    for (int tries = 0; tries < 30 * 30 && !(saw_harvest_target && saw_deploy_or_work_state); ++tries) {
        if (!rts_tick(model, &snapshot)) return fail("tick harvest approach");
        int now = -1;
        for (int i = 0; i < snapshot.unit_count; ++i)
            if (strstr(snapshot.units[i].sprite_name, "EXPL.SPR") != NULL) { now = i; break; }
        if (now < 0) return fail("exploiter disappeared during harvest");
        if (snapshot.units[now].harvest_target >= 0) saw_harvest_target = true;
        int state_id = snapshot.units[now].state_id;
        if (state_id >= S_DC_EXPL_DEPLOY1 && state_id <= S_DC_EXPL_WORK2)
            saw_deploy_or_work_state = true;
    }
    if (!saw_harvest_target) return fail("exploiter never recorded a harvest target");
    if (!saw_deploy_or_work_state) return fail("exploiter never entered a deploy/work state");

    int resources_after = 0;
    for (int tries = 0; tries < 30 * 60; ++tries) {
        if (!rts_tick(model, &snapshot)) return fail("tick harvest mining");
        resources_after = snapshot.player_resources[0][0];
        if (resources_after > start_resources) break;
    }
    if (resources_after <= start_resources) return fail("harvesting never increased player resources");

    rts_game_model_destroy(model);
    printf("PASS: exploiter harvest lifecycle (deploy/work states and resource growth)\n");
    return 0;
}

/* Unit creation: a completed production order spawns a live, renderable unit. */
static int assert_unit_creation_from_production(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/DCOLONY", .map_path = "SCENARIO/HUMAN/HUMAN02.MAP" };
    static RtsRenderSnapshot snapshot;
    if (!model || !rts_game_model_load(model, &config)) return fail("load HUMAN02 for production");
    if (!rts_game_model_snapshot(model, &snapshot)) return fail("snapshot for production");

    /* Resources trickle in mainly from harvesting; get the scripted exploiter mining
     * so an Exploiter build order becomes affordable within a bounded tick budget. */
    int exploiter = -1;
    for (int tries = 0; exploiter < 0 && tries < 30 * 30; ++tries) {
        for (int i = 0; i < snapshot.unit_count; ++i)
            if (strstr(snapshot.units[i].sprite_name, "EXPL.SPR") != NULL) { exploiter = i; break; }
        if (exploiter < 0 && !rts_tick(model, &snapshot)) return fail("tick to find exploiter for production");
    }
    if (exploiter >= 0) {
        RtsGameCommand select_exploiter = { .kind = RTS_GAME_COMMAND_SELECT_UNIT_INDEX,
            .data.select_unit_index = { exploiter, false } };
        RtsGameCommand harvest = { .kind = RTS_GAME_COMMAND_HARVEST_SELECTED,
            .data.harvest_selected = { .target = { 69.5f, 48.5f } } };
        if (!rts_game_model_command(model, &select_exploiter) ||
            !rts_game_model_command(model, &harvest)) return fail("harvest command for production");
    }

    RtsProductDefinition products[64];
    int product_count = rts_game_model_products(model, products, 64);
    bool build_accepted = false;
    uint32_t producer_id = 0;
    int product_type = -1;
    for (int tries = 0; tries < 30 * 100 && !build_accepted; ++tries) {
        product_count = rts_game_model_products(model, products, 64);
        for (int p = 0; p < product_count && !build_accepted; ++p) {
            if (!products[p].available || products[p].product_class != RTS_PRODUCT_UNIT) continue;
            if (products[p].ui_id != 89) continue; /* Exploiter: known-buildable on HUMAN02. */
            for (int i = 0; i < snapshot.unit_count; ++i) {
                if (snapshot.units[i].owner != 0) continue;
                RtsGameCommand build = { .kind = RTS_GAME_COMMAND_BUILD_PRODUCT,
                    .data.build_product = { snapshot.units[i].id, i, products[p].ui_id } };
                if (rts_game_model_command(model, &build)) {
                    build_accepted = true;
                    producer_id = snapshot.units[i].id;
                    product_type = products[p].product_type;
                    break;
                }
            }
        }
        if (!build_accepted) {
            if (!rts_tick(model, &snapshot)) return fail("tick while waiting for resources");
        }
    }
    if (!build_accepted) return fail("accept an available unit build command");

    bool built = false;
    RtsGameEvent event;
    memset(&event, 0, sizeof(event));
    for (int tries = 0; tries < 30 * 60 && !built; ++tries) {
        if (!rts_tick(model, &snapshot)) return fail("tick unit production");
        RtsGameEvent polled;
        while (rts_game_model_poll_event(model, &polled)) {
            if (polled.type == RTS_GAME_EVENT_UNIT_BUILT && polled.target_id == producer_id &&
                polled.product_type == product_type) {
                built = true;
                event = polled;
            }
        }
    }
    if (!built) return fail("unit-built event never observed");

    int spawned = rts_find_unit_by_id(&snapshot, event.subject_id);
    if (spawned < 0) return fail("spawned unit not present in snapshot");
    if (snapshot.units[spawned].owner != 0) return fail("spawned unit has wrong owner");
    if (snapshot.units[spawned].hp <= 0 || snapshot.units[spawned].hp != snapshot.units[spawned].max_hp)
        return fail("spawned unit does not start at full health");
    if (snapshot.units[spawned].sprite_name[0] == '\0')
        return fail("spawned unit has no sprite assigned");
    if ((snapshot.units[spawned].traits & RTS_RENDER_TRAIT_RENDERABLE) == 0)
        return fail("spawned unit is not renderable");

    rts_game_model_destroy(model);
    printf("PASS: unit creation from production (spawn, sprite, ownership, full health)\n");
    return 0;
}

int main(void) {
    RTS_RUN(assert_attack_lifecycle());
    RTS_RUN(assert_hidden_attacker_effects_suppressed());
    RTS_RUN(assert_exploiter_harvest_lifecycle());
    RTS_RUN(assert_unit_creation_from_production());
    return 0;
}
