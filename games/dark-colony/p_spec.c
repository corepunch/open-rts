#include "game.h"
#include "dc_facing.h"
#include "info.h"
#include "dc_types.h"
#include "w_spr.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const actortype_t *actor_type_by_id(uint16_t type_id);
void apply_actor_type_defaults(mobj_t *unit, const actortype_t *type);

static void replace_extension(char *dst, size_t dst_size, const char *path, const char *ext) {
    snprintf(dst, dst_size, "%s", path);
    char *dot = strrchr(dst, '.');
    char *slash = strrchr(dst, '/');
    if (dot && (!slash || dot > slash)) {
        snprintf(dot, dst_size - (size_t)(dot - dst), "%s", ext);
    } else {
        strncat(dst, ext, dst_size - strlen(dst) - 1);
    }
}

static char *load_text(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fclose(fp);
    blob_t blob;
    if (!W_ReadFile(path, &blob)) return NULL;
    char *text = malloc(blob.size + 1);
    if (!text) {
        W_FreeFile(&blob);
        return NULL;
    }
    memcpy(text, blob.bytes, blob.size);
    text[blob.size] = '\0';
    W_FreeFile(&blob);
    return text;
}

static void trim_copy(char *dst, size_t dst_size, const char *src) {
    while (*src && isspace((unsigned char)*src)) src++;
    size_t len = strlen(src);
    while (len > 0 && isspace((unsigned char)src[len - 1])) len--;
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

typedef enum {
    SCRIPT_CMD_NONE,
    SCRIPT_CMD_MSG,
    SCRIPT_CMD_REINFORCE,
    SCRIPT_CMD_REINFORCE2,
    SCRIPT_CMD_NEWTYPE,
    SCRIPT_CMD_BAIL,
    SCRIPT_CMD_NEWRATE,
    SCRIPT_CMD_SETARRAY,
    SCRIPT_CMD_SETLIFES,
} ScriptCommandType;

typedef enum {
    MISSION_ACTIVE,
    MISSION_WON,
    MISSION_LOST,
    MISSION_ALLY_LOST,
} MissionState;

typedef enum {
    COND_COUNTER_GT,    /* c > N */
    COND_ALL_BUILDINGS_DESTROYED, /* b(team,0..4)==0 */
    COND_UNIT_TYPE_EXISTS, /* s(team,type,slot)==1 */
    COND_TRIP_PLAYER_NEAR, /* S==0 */
    COND_COUNTER_GT_STATE, /* c > s(team,type,slot) */
} ConditionKind;

typedef struct {
    int team;
    int slots[5];
    int slot_count;
} BuildingCondition;

typedef struct {
    int team;
    int type;
    int slot;
    int expected_value;
} UnitStateCondition;

enum {
    SCRIPT_COUNTER_MS = 1000,
    DROPSHIP_FLIGHT_TICS = 50,
    DROPSHIP_MAX_PAYLOAD_TYPES = 5,
    MAX_CITY_SLOTS = 5,
};

typedef struct {
    ScriptCommandType type;
    int a[8];
} ScriptCommand;

typedef struct {
    int id;
    bool trip;
    bool fired;
    int c_gt;
    bool requires_player_near;
    int trigger_x;
    int trigger_y;
    ScriptCommand commands[32];
    int command_count;
    ConditionKind condition_kind;
    bool condition_negated; /* true if condition should be negated (e.g. enabled=0) */
    BuildingCondition building_cond;
    UnitStateCondition unit_state_cond;
    int counter_gt_state_team;
    int counter_gt_state_type;
    int counter_gt_state_slot;
} ScriptBlock;

typedef struct {
    int id;
    char text[256];
} ScriptMessage;

typedef enum {
    DROPSHIP_APPROACH,
    DROPSHIP_REPOSITION,
    DROPSHIP_UNLOAD,
    DROPSHIP_DEPART,
} DropshipPhase;

typedef struct {
    int type;
    int count;
} DropshipPayload;

typedef struct Mission Mission;

static const ivec2_t drop_formation[] = {
    { 0, 0 }, { -1, 0 }, { 1, 0 }, { 0, -1 },
    { 0, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 },
    { 1, 1 }, { -2, 0 }, { 2, 0 }, { 0, -2 },
};

typedef struct {
    bool active;
    DropshipPhase phase;
    int team;
    ivec2_t origin;
    fvec2_t start_center;
    fvec2_t target_center;
    fvec2_t flight_vector;
    fvec2_t center;
    DropshipPayload payload[DROPSHIP_MAX_PAYLOAD_TYPES];
    int payload_count;
    int payload_index;
    int released_count;
    bool release_pending;
    int phase_duration_ms;
    int elapsed_ms;
    int effect_slots[DROPSHIP_MAX_PARTS];
} Dropship;

typedef enum {
    DROPSHIP_ANIMATION_MOVE,
    DROPSHIP_ANIMATION_UNLOAD,
} DropshipAnimationKind;

typedef struct {
    Mission *mission;
    level_t *map;
    mobj_t *units;
    int *unit_count;
    effect_t *effects;
    int max_effects;
    const gameinfo_t *game_info;
} DropshipUpdateContext;

typedef void (*DropshipComplete)(DropshipUpdateContext *context,
                                 Dropship *ship);

typedef struct {
    DropshipAnimationKind animation;
    bool moves;
    DropshipComplete complete;
} DropshipPhaseDef;

static void dropship_approach_done(DropshipUpdateContext *context,
                                   Dropship *ship);
static void dropship_unload_done(DropshipUpdateContext *context,
                                 Dropship *ship);
static void dropship_depart_done(DropshipUpdateContext *context,
                                 Dropship *ship);

static const DropshipPhaseDef dropship_phase_defs[] = {
    [DROPSHIP_APPROACH] = {
        .animation = DROPSHIP_ANIMATION_MOVE,
        .moves = true,
        .complete = dropship_approach_done,
    },
    [DROPSHIP_REPOSITION] = {
        .animation = DROPSHIP_ANIMATION_MOVE,
        .moves = true,
        .complete = dropship_approach_done,
    },
    [DROPSHIP_UNLOAD] = {
        .animation = DROPSHIP_ANIMATION_UNLOAD,
        .moves = false,
        .complete = dropship_unload_done,
    },
    [DROPSHIP_DEPART] = {
        .animation = DROPSHIP_ANIMATION_MOVE,
        .moves = true,
        .complete = dropship_depart_done,
    },
};

typedef struct {
    int team;
    int slot;
    int anchor_x;
    int anchor_y;
    int race;
} CitySlotInfo;

struct Mission {
    ScriptMessage messages[64];
    int message_count;
    ScriptBlock blocks[64];
    int block_count;
    Dropship dropships[8];
    DropshipAnimations dropship_animations;
    int elapsed_ms;
    int ai_elapsed_ms;
    int ai_wave_elapsed_ms;
    uint32_t ai_wave_target_id;
    MissionState state;
    int city_slot_count;
    CitySlotInfo city_slots[32];
    int script_arrays[16];
};

typedef struct {
    int think_interval_ms;
    int attack_wave_interval_ms;
    float defense_radius;
    float attack_eagerness;
} AiConfig;

/* These values mirror the observable Krusty attack policy: think in batches,
 * prefer dangerous mobile targets, then use distance as the stable tie-break.
 * The table is intentionally plugin-local until DC's unitid/depend tables are
 * fully decoded. */
static const AiConfig ai_config = {
    .think_interval_ms = 500,
    .attack_wave_interval_ms = 5000,
    .defense_radius = 12.0f,
    .attack_eagerness = 1.0f,
};

static int ai_target(const mobj_t *attacker, const mobj_t *units,
                                 int unit_count, int preferred_index, bool defending) {
    int best = -1;
    float best_score = -INFINITY;
    fvec2_t attacker_position = fixedvec3_xy_to_fvec2(attacker->core.position);
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *candidate = &units[i];
        if (candidate == attacker || candidate->remove || candidate->hp <= 0 ||
            candidate->owner == attacker->owner) continue;
        fvec2_t delta = fvec2_sub(fixedvec3_xy_to_fvec2(candidate->core.position),
                                  attacker_position);
        float distance2 = fvec2_length_squared(delta);
        float threat = (candidate->traits & MF_ATTACK) != 0 ? 2.0f : 0.0f;
        float mobility = (candidate->traits & MF_MOBILE) != 0 ? 0.5f : 0.0f;
        float preferred = i == preferred_index ? 1.5f : 0.0f;
        if (defending && (candidate->traits & MF_ATTACK) != 0) preferred += 1.0f;
        float score = ai_config.attack_eagerness * (threat + mobility) -
                      distance2 * 0.02f + preferred;
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

static int ai_find_wave_target(const mobj_t *units, int unit_count) {
    int best = -1;
    float best_score = -INFINITY;
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *candidate = &units[i];
        if (candidate->remove || candidate->hp <= 0 || candidate->owner != 0) continue;
        float score = (candidate->traits & MF_ATTACK) != 0 ? 3.0f : 0.0f;
        score += (candidate->traits & MF_MOBILE) != 0 ? 1.0f : 0.0f;
        score += candidate->max_hp > 0 ? (float)candidate->max_hp / 2000.0f : 0.0f;
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

static bool ai_is_defending(const mobj_t *units, int unit_count,
                                        fvec2_t base_position) {
    float radius2 = ai_config.defense_radius *
                    ai_config.defense_radius;
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *unit = &units[i];
        if (unit->remove || unit->hp <= 0 || unit->owner != 0 ||
            (unit->traits & MF_ATTACK) == 0) continue;
        if (fvec2_distance_squared(fixedvec3_xy_to_fvec2(unit->core.position),
                                   base_position) <= radius2) return true;
    }
    return false;
}

static int ai_nearest_vent(const level_t *map, fvec2_t position) {
    int best = -1;
    float best_distance2 = INFINITY;
    if (!map || !map->resource_vents) return best;
    for (int i = 0; i < map->resource_vent_count; ++i) {
        const resourcevent_t *vent = &map->resource_vents[i];
        if (!vent->active || vent->amount <= 0 || vent->rate <= 0) continue;
        float distance2 = fvec2_distance_squared(position, vent->attachment);
        if (distance2 < best_distance2) {
            best_distance2 = distance2;
            best = i;
        }
    }
    return best;
}

static void update_ai_economy(const level_t *map, mobj_t *units,
                                          int unit_count) {
    if (!map || !units) return;
    for (int i = 0; i < unit_count; ++i) {
        mobj_t *unit = &units[i];
        if (unit->remove || unit->hp <= 0 || unit->owner == 0 ||
            (unit->traits & (MF_MOBILE | MF_HARVESTER)) !=
                (MF_MOBILE | MF_HARVESTER) || unit->harvest.target >= 0) continue;
        int vent_index = ai_nearest_vent(
            map, fixedvec3_xy_to_fvec2(unit->core.position));
        if (vent_index >= 0)
            P_HarvestUnitTo(map, unit, map->resource_vents[vent_index].attachment);
    }
}

static void update_ai(Mission *mission, const level_t *map,
                                  mobj_t *units, int unit_count, int dt_ms) {
    if (!map || !units || unit_count <= 0 ||
        !map_has_ai(map, 1)) return;
    mission->ai_elapsed_ms += dt_ms;
    if (mission->ai_elapsed_ms < ai_config.think_interval_ms) return;
    mission->ai_elapsed_ms %= ai_config.think_interval_ms;
    update_ai_economy(map, units, unit_count);

    mission->ai_wave_elapsed_ms += ai_config.think_interval_ms;
    int wave_target = -1;
    if (mission->ai_wave_elapsed_ms >= ai_config.attack_wave_interval_ms) {
        mission->ai_wave_elapsed_ms %= ai_config.attack_wave_interval_ms;
        wave_target = ai_find_wave_target(units, unit_count);
        mission->ai_wave_target_id = wave_target >= 0 ? units[wave_target].id : 0;
    }
    if (wave_target < 0 && mission->ai_wave_target_id != 0) {
        for (int i = 0; i < unit_count; ++i) {
            if (units[i].id == mission->ai_wave_target_id &&
                !units[i].remove && units[i].hp > 0) {
                wave_target = i;
                break;
            }
        }
    }

    fvec2_t base_position = { 0.0f, 0.0f };
    int base_count = 0;
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *unit = &units[i];
        if (unit->remove || unit->hp <= 0 || unit->owner == 0 ||
            (unit->traits & MF_MOBILE) != 0) continue;
        base_position = fvec2_add(base_position,
                                  fixedvec3_xy_to_fvec2(unit->core.position));
        base_count++;
    }
    if (base_count > 0) base_position = fvec2_scale(base_position, 1.0f / (float)base_count);
    bool defending = base_count > 0 &&
                     ai_is_defending(units, unit_count, base_position);

    for (int i = 0; i < unit_count; ++i) {
        mobj_t *attacker = &units[i];
        if (attacker->remove || attacker->hp <= 0 || attacker->owner == 0 ||
            (attacker->traits & (MF_MOBILE | MF_ATTACK)) != (MF_MOBILE | MF_ATTACK)) {
            continue;
        }
        int target_index = ai_target(attacker, units, unit_count,
                                                 defending ? -1 : wave_target,
                                                 defending);
        if (target_index < 0) continue;
        mobj_t *target = &units[target_index];
        attacker->attack.target = target_index;
        fvec2_t target_position = fixedvec3_xy_to_fvec2(target->core.position);
        fvec2_t attacker_position = fixedvec3_xy_to_fvec2(attacker->core.position);
        float range = attacker->attack.range > 0.0f ? attacker->attack.range : 1.0f;
        if (fvec2_distance_squared(attacker_position, target_position) > range * range) {
            P_MoveUnitTo(map, attacker, target_position);
        }
    }
}

static const char *script_message(const Mission *mission, int id) {
    if (!mission) return NULL;
    for (int i = 0; i < mission->message_count; ++i)
        if (mission->messages[i].id == id) return mission->messages[i].text;
    return NULL;
}

static uint16_t script_unit_type(int team, int type) {
    if (team != 0) {
        if (type == 0 || (type >= 69 && type <= 76)) return MT_DC_GREY;
        return MT_DC_GREY;
    }
    if (type == 0 || (type >= 69 && type <= 72)) return MT_DC_TROOPER;
    switch (type) {
        case 2: return MT_DC_REAPER;
        case 3: return MT_DC_THUNDERBOLT;
        case 4: return MT_DC_CYBORG;
        case 5: return MT_DC_SCOUT;
        case 6: return MT_DC_EXPLOITER;
        default: return MT_DC_TROOPER;
    }
}

static bool player_near(const level_t *map, const mobj_t *units,
                                    int unit_count, int gx, int gy) {
    (void)map;
    fvec2_t center = fvec2_cell_center((ivec2_t){ gx, gy });
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].owner != 0 || units[i].remove || units[i].hp <= 0) continue;
        if (fvec2_distance_squared(
            fixedvec3_xy_to_fvec2(units[i].core.position), center) <= 16.0f) return true;
    }
    return false;
}

static int spawn_dropship_part(effect_t *effects, int max_effects,
                                           fvec2_t center, int duration_ms) {
    for (int i = 0; i < max_effects; ++i) {
        if (effects[i].active) continue;
        effect_t *effect = &effects[i];
        memset(effect, 0, sizeof(*effect));
        effect->active = true;
        effect->fin_placement = true;
        effect->core.position = fixedvec3_from_fvec2(center, 0);
        effect->duration_ms = duration_ms;
        effect->frame_ms = duration_ms + 1;
        return i;
    }
    return -1;
}

static void clear_dropship_parts(Dropship *ship,
                                             effect_t *effects, int max_effects) {
    for (int i = 0; i < DROPSHIP_MAX_PARTS; ++i) {
        int slot = ship->effect_slots[i];
        if (slot >= 0 && slot < max_effects) memset(&effects[slot], 0, sizeof(effects[slot]));
        ship->effect_slots[i] = -1;
    }
}

static int dropship_frame_at(const DropshipAnimation *animation,
                                         int elapsed_ms) {
    if (animation->duration_ms > 0) elapsed_ms %= animation->duration_ms;
    int frame_end_ms = 0;
    for (int i = 0; i < animation->frame_count; ++i) {
        frame_end_ms += animation->frames[i].duration_ms;
        if (elapsed_ms < frame_end_ms) return i;
    }
    return animation->frame_count - 1;
}

static void sync_dropship_parts(Dropship *ship,
                                            const DropshipAnimation *animation,
                                            effect_t *effects, int max_effects) {
    if (!animation->valid || animation->frame_count <= 0) return;
    const DropshipFrame *frame =
        &animation->frames[dropship_frame_at(animation, ship->elapsed_ms)];
    int runtime_part = 0;
    for (int part_index = 0;
         part_index < frame->part_count && runtime_part < DROPSHIP_MAX_PARTS;
         ++part_index) {
        const DropshipPart *part = &frame->parts[part_index];
        if (dropship_phase_defs[ship->phase].animation != DROPSHIP_ANIMATION_UNLOAD &&
            (strcmp(part->sprite_name, "SPRITES/DUTS.SPR") == 0 ||
             strcmp(part->sprite_name, "SPRITES/CLOD.SPR") == 0)) continue;
        int slot = ship->effect_slots[runtime_part];
        if (slot < 0 || slot >= max_effects || !effects[slot].active) {
            slot = spawn_dropship_part(
                effects, max_effects, ship->center, ship->phase_duration_ms + 1);
            ship->effect_slots[runtime_part] = slot;
        }
        if (slot >= 0) {
            effect_t *effect = &effects[slot];
            /* Slots persist across phase transitions now, so refresh the
             * lifetime every tick or P_UpdateEffects reaps them once age_ms
             * exceeds the duration_ms captured when the slot was first spawned. */
            effect->age_ms = 0;
            effect->duration_ms = ship->phase_duration_ms + 1;
            effect->core.position = fixedvec3_from_fvec2(ship->center, 0);
            effect->core.frame = part->sprite_frame;
            effect->core.render_offset = part->offset;
            /* Palette remap is authored per animation label in DROP.FIN, not per team;
             * force the ship's own team color so it doesn't flip between phases. */
            effect->core.render_remap = ship->team;
            effect->core.render_intensity = part->render_intensity;
            effect->core.render_flags = (uint32_t)part->flags;
            effect->render_selector = part->render_selector;
            snprintf(effect->core.sprite_name, sizeof(effect->core.sprite_name),
                     "%s", part->sprite_name);
        }
        runtime_part++;
    }
    for (int i = runtime_part; i < DROPSHIP_MAX_PARTS; ++i) {
        int slot = ship->effect_slots[i];
        if (slot >= 0 && slot < max_effects) memset(&effects[slot], 0, sizeof(effects[slot]));
        ship->effect_slots[i] = -1;
    }
}

static const DropshipAnimation *dropship_animation(
    const Mission *mission, DropshipPhase phase) {
    const DropshipAnimation *animations[] = {
        [DROPSHIP_ANIMATION_MOVE] = &mission->dropship_animations.move,
        [DROPSHIP_ANIMATION_UNLOAD] = &mission->dropship_animations.unload,
    };
    return animations[dropship_phase_defs[phase].animation];
}

static Dropship *spawn_drop_effect(
    Mission *mission, effect_t *effects, int max_effects,
    int team, int gx, int gy) {
    if (!mission || !effects || max_effects <= 0) return NULL;

    Dropship *ship = NULL;
    for (int i = 0; i < (int)(sizeof(mission->dropships) / sizeof(mission->dropships[0])); ++i) {
        if (!mission->dropships[i].active) { ship = &mission->dropships[i]; break; }
    }
    if (!ship) return NULL;

    memset(ship, 0, sizeof(*ship));
    ship->active = true;
    ship->phase = DROPSHIP_APPROACH;
    ship->team = team;
    ship->origin = (ivec2_t){ gx, gy };
    ship->start_center = fvec2_cell_center((ivec2_t){ gx - 1, gy - 1 });
    ship->target_center = fvec2_cell_center(ship->origin);
    ship->flight_vector = fvec2_sub(ship->target_center, ship->start_center);
    ship->center = ship->start_center;
    ship->phase_duration_ms = (DROPSHIP_FLIGHT_TICS * 1000 + 15) / 30;
    for (int i = 0; i < DROPSHIP_MAX_PARTS; ++i) ship->effect_slots[i] = -1;
    sync_dropship_parts(
        ship, &mission->dropship_animations.move, effects, max_effects);

    if (getenv("OPEN_RTS_DEBUG_SCRIPT")) {
        fprintf(stderr, "Dark Colony dropship approaching %d,%d\n", gx, gy);
    }
    return ship;
}

static void spawn_script_unit(const level_t *map, mobj_t *units, int *unit_count, int team,
                                          int gx, int gy, int type,
                                          const gameinfo_t *game_info) {
    if (!units || !unit_count || *unit_count >= MAXMOBJS) return;
    mobj_t *unit = &units[*unit_count];
    memset(unit, 0, sizeof(*unit));
    int spawn_x = gx;
    int spawn_y = gy;
    if (map) {
        if (spawn_x < 0) spawn_x = 0;
        if (spawn_x >= map->width) spawn_x = map->width - 1;
        if (spawn_y < 0) spawn_y = 0;
        if (spawn_y >= map->height) spawn_y = map->height - 1;
    }
    unit->core.position = fixedvec3_from_fvec2(
        fvec2_cell_center((ivec2_t){ spawn_x, spawn_y }), 0);
    unit->owner = team == 0 ? 0 : 1;
    if (unit->owner == 0) {
        bool has_selected_player = false;
        for (int i = 0; i < *unit_count; ++i) {
            if (units[i].owner == 0 && units[i].selected) {
                has_selected_player = true;
                break;
            }
        }
        unit->selected = !has_selected_player;
    }
    unit->core.angle = dc_direction_to_angle(unit->owner == 0 ? 6 : 14);
    uint16_t type_id = script_unit_type(team, type);
    const actortype_t *actor = actor_type_by_id(type_id);
    unit->native_type_id = (uint16_t)(type >= 0 ? type : 0);
    apply_actor_type_defaults(unit, actor);
    P_SpawnMobj(game_info, unit);
    (*unit_count)++;
}

static bool dropship_cell_occupied(const mobj_t *units, int unit_count,
                                               ivec2_t cell) {
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].remove || units[i].hp <= 0) continue;
        fvec2_t position = fixedvec3_xy_to_fvec2(units[i].core.position);
        if ((int)floorf(position.x) == cell.x && (int)floorf(position.y) == cell.y)
            return true;
    }
    return false;
}

static fvec2_t drop_position(const level_t *map, const mobj_t *units,
                                         int unit_count, fvec2_t center, int slot) {
    int formation_count = (int)(sizeof(drop_formation) /
                                sizeof(drop_formation[0]));
    ivec2_t offset = drop_formation[slot % formation_count];
    fvec2_t candidate = fvec2_add(center, (fvec2_t){ (float)offset.x, (float)offset.y });
    if (map && L_IsWalkable(map, (int)floorf(candidate.x), (int)floorf(candidate.y)) &&
        !dropship_cell_occupied(units, unit_count,
                                            (ivec2_t){ (int)floorf(candidate.x),
                                                       (int)floorf(candidate.y) })) {
        return candidate;
    }
    return center;
}

static void set_dropship_phase(Dropship *ship,
                                           DropshipPhase phase,
                                           int duration_ms,
                                           effect_t *effects, int max_effects) {
    /* Do not clear effect slots here: the caller already synced the ending
     * phase's trailing frame into them this tick, and sync_dropship_parts
     * reconciles slot contents for the new phase on the next tick. Clearing
     * here would erase that trailing frame before it is ever rendered. */
    (void)effects;
    (void)max_effects;
    ship->phase = phase;
    ship->elapsed_ms = 0;
    ship->phase_duration_ms = duration_ms > 0 ? duration_ms : 1;
}

static void dropship_approach_done(DropshipUpdateContext *context,
                                   Dropship *ship) {
    ship->center = ship->target_center;
    ship->start_center = ship->center;
    ship->target_center = ship->center;
    ship->release_pending = true;
    set_dropship_phase(ship, DROPSHIP_UNLOAD,
                       context->mission->dropship_animations.unload.duration_ms,
                       context->effects, context->max_effects);
}

static void dropship_unload_done(DropshipUpdateContext *context,
                                 Dropship *ship) {
    if (ship->release_pending && ship->payload_index < ship->payload_count) {
        DropshipPayload *payload = &ship->payload[ship->payload_index];
        fvec2_t origin_center = fvec2_cell_center(ship->origin);
        fvec2_t drop_pos = drop_position(
            context->map, context->units, *context->unit_count,
            origin_center, ship->released_count);
        ivec2_t release_cell = {
            (int)floorf(drop_pos.x), (int)floorf(drop_pos.y)
        };
        spawn_script_unit(context->map, context->units, context->unit_count, ship->team,
                          release_cell.x, release_cell.y,
                  payload->type, context->game_info);
        ship->released_count++;
        payload->count--;
        if (payload->count <= 0) ship->payload_index++;
        ship->release_pending = false;
    }

    if (ship->payload_index < ship->payload_count) {
        fvec2_t origin_center = fvec2_cell_center(ship->origin);
        ship->start_center = ship->center;
        ship->target_center = origin_center;
        ship->release_pending = true;
        set_dropship_phase(
            ship, DROPSHIP_REPOSITION,
            (DROPSHIP_FLIGHT_TICS * 1000 + 15) / 30,
            context->effects, context->max_effects);
        return;
    }

    ship->start_center = ship->center;
    ship->target_center = fvec2_add(ship->center, ship->flight_vector);
    set_dropship_phase(ship, DROPSHIP_DEPART,
                       (DROPSHIP_FLIGHT_TICS * 1000 + 15) / 30,
                       context->effects, context->max_effects);
}

static void dropship_depart_done(DropshipUpdateContext *context,
                                 Dropship *ship) {
    clear_dropship_parts(ship, context->effects, context->max_effects);
    ship->active = false;
}

static void update_dropship(Mission *mission,
                                        Dropship *ship,
                                        level_t *map, mobj_t *units, int *unit_count,
                                        effect_t *effects, int max_effects,
                                        const gameinfo_t *game_info, int dt_ms) {
    DropshipUpdateContext context = {
        .mission = mission,
        .map = map,
        .units = units,
        .unit_count = unit_count,
        .effects = effects,
        .max_effects = max_effects,
        .game_info = game_info,
    };
    ship->elapsed_ms += dt_ms;
    const DropshipPhaseDef *phase_def = &dropship_phase_defs[ship->phase];
    if (phase_def->moves) {
        float progress = (float)ship->elapsed_ms / (float)ship->phase_duration_ms;
        if (progress > 1.0f) progress = 1.0f;
        ship->center = fvec2_add(ship->start_center,
                                fvec2_scale(fvec2_sub(ship->target_center,
                                                     ship->start_center), progress));
    }

    if (ship->elapsed_ms >= ship->phase_duration_ms) {
        /* Render the ending phase's final frame before switching animations,
         * otherwise the last frame is skipped and the ship appears to blink. */
        int trailing_elapsed = ship->phase_duration_ms > 0 ? ship->phase_duration_ms - 1 : 0;
        int leftover_elapsed = ship->elapsed_ms;
        ship->elapsed_ms = trailing_elapsed;
        sync_dropship_parts(
            ship, dropship_animation(mission, ship->phase), effects, max_effects);
        ship->elapsed_ms = leftover_elapsed;

        phase_def->complete(&context, ship);
        return;
    }

    sync_dropship_parts(
        ship, dropship_animation(mission, ship->phase), effects, max_effects);
}

static void execute_script_block(Mission *mission, ScriptBlock *block,
                                              level_t *map, mobj_t *units, int *unit_count,
                                              effect_t *effects, int max_effects,
                                              const gameinfo_t *game_info, hudtext_t *hud) {
    if (!mission || !block) return;
    for (int i = 0; i < block->command_count; ++i) {
        ScriptCommand *cmd = &block->commands[i];
        if (cmd->type == SCRIPT_CMD_MSG) {
            const char *message = script_message(mission, cmd->a[0]);
            if (message) HU_PushMessage(hud, message, -1);
        } else if (cmd->type == SCRIPT_CMD_REINFORCE ||
                   cmd->type == SCRIPT_CMD_REINFORCE2) {
            int team = cmd->a[0], x = cmd->a[1], y = cmd->a[2];
            int count = cmd->a[3] > 0 ? cmd->a[3] : 1;
            int type = cmd->a[4];
            if (cmd->type == SCRIPT_CMD_REINFORCE && cmd->a[5]) {
                Dropship *ship = spawn_drop_effect(
                    mission, effects, max_effects, team, x, y);
                for (int j = i; j < block->command_count; ++j) {
                    ScriptCommand *drop_cmd = &block->commands[j];
                    if (drop_cmd->type != SCRIPT_CMD_REINFORCE) break;
                    if (j != i && drop_cmd->a[5]) break;
                    if (ship && ship->payload_count < DROPSHIP_MAX_PAYLOAD_TYPES) {
                        DropshipPayload *payload =
                            &ship->payload[ship->payload_count++];
                        payload->type = drop_cmd->a[4];
                        payload->count = drop_cmd->a[3] > 0 ? drop_cmd->a[3] : 1;
                    }
                }
            }
            for (int n = 0; n < count; ++n) {
                if (cmd->type == SCRIPT_CMD_REINFORCE2)
                    spawn_script_unit(map, units, unit_count, team, x, y, type, game_info);
            }
        } else if (cmd->type == SCRIPT_CMD_BAIL) {
            int result = cmd->a[0];
            int code = cmd->a[1];
            MissionState new_state = MISSION_ACTIVE;
            const char *msg = NULL;
            if (result == 0 && code == 1) {
                new_state = MISSION_WON;
                msg = "ALIEN HIVE DESTROYED.";
            } else if (result == 1 && code == 2) {
                new_state = MISSION_LOST;
                msg = "ALL YOUR BUILDINGS HAVE BEEN DESTROYED.";
            } else if (result == 1 && code == 3) {
                new_state = MISSION_ALLY_LOST;
                msg = "ALLIED BASE LOST.";
            }
            if (new_state != MISSION_ACTIVE) {
                if (getenv("OPEN_RTS_DEBUG_SCRIPT")) {
                    fprintf(stderr, "Dark Colony mission state -> %d (bail %d %d)\n",
                            new_state, result, code);
                }
                mission->state = new_state;
                if (msg) HU_PushMessage(hud, msg, -1);
            }
        } else if (cmd->type == SCRIPT_CMD_NEWRATE) {
            /* newrate updates resource vent rates; store for later use */
        } else if (cmd->type == SCRIPT_CMD_SETARRAY) {
            int index = cmd->a[0];
            int value = cmd->a[1];
            if (index >= 0 && index < (int)(sizeof(mission->script_arrays) / sizeof(mission->script_arrays[0]))) {
                mission->script_arrays[index] = value;
            }
        } else if (cmd->type == SCRIPT_CMD_SETLIFES) {
            /* setlifes sets a unit's remaining lives; not yet implemented */
        }
    }
    block->fired = true;
}

static void parse_messages(Mission *mission, const char *path) {
    char *text = load_text(path);
    if (!text) return;
    ScriptMessage *current = NULL;
    for (char *line = text; line && *line;) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
            char nl = *next; *next++ = '\0';
            if (nl == '\r' && *next == '\n') next++;
        }
        char token[256];
        trim_copy(token, sizeof(token), line);
        int id = 0;
        if (sscanf(token, "text %d", &id) == 1) {
            if (mission->message_count < (int)(sizeof(mission->messages) / sizeof(mission->messages[0]))) {
                current = &mission->messages[mission->message_count++];
                memset(current, 0, sizeof(*current));
                current->id = id;
            }
        } else if (current && token[0] != '\0') {
            size_t len = strlen(current->text);
            snprintf(current->text + len, sizeof(current->text) - len, "%s%s",
                     len > 0 ? " " : "", token);
        }
        line = next;
    }
    free(text);
}

static void script_add_command(ScriptBlock *block,
                                           ScriptCommand command) {
    if (!block || block->command_count >= (int)(sizeof(block->commands) / sizeof(block->commands[0])))
        return;
    block->commands[block->command_count++] = command;
}

static int parse_command_ints(const char *token, const char *keyword,
                                          int *out, int max_out) {
    if (!token || !keyword || !out || max_out <= 0) return -1;
    size_t keyword_len = strlen(keyword);
    if (strncmp(token, keyword, keyword_len) != 0) return -1;
    if (token[keyword_len] != '\0' && !isspace((unsigned char)token[keyword_len])) return -1;

    const char *p = token + keyword_len;
    int count = 0;
    while (*p && count < max_out) {
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0') break;
        char *end = NULL;
        long value = strtol(p, &end, 10);
        if (end == p) break;
        out[count++] = (int)value;
        p = end;
    }
    return count;
}

static void add_reinforce_commands(ScriptBlock *block,
                                               ScriptCommandType type,
                                               const int *v, int count) {
    if (!block || !v || count < 5) return;
    int team = v[0];
    int x = v[1];
    int y = v[2];
    bool drop_added = false;
    for (int pair = 3; pair + 1 < count; pair += 2) {
        ScriptCommand cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = type;
        cmd.a[0] = team;
        cmd.a[1] = x;
        cmd.a[2] = y;
        cmd.a[3] = v[pair + 1];
        cmd.a[4] = v[pair];
        if (cmd.a[3] <= 0) continue;
        cmd.a[5] = type == SCRIPT_CMD_REINFORCE && !drop_added;
        drop_added = drop_added || cmd.a[5];
        script_add_command(block, cmd);
    }
    if (block->trigger_x < 0) {
        block->trigger_x = x;
        block->trigger_y = y;
    }
}

static void parse_block_condition(ScriptBlock *block, const char *cond) {
    if (!block || !cond) return;
    /* Detect b(team,slot)==0 patterns: ((b(2,0)==0)&&(b(2,1)==0)&&...) */
    int team = -1, slot = -1;
    if (sscanf(cond, "b(%d,%d)==0", &team, &slot) == 2 ||
        sscanf(cond, "(b(%d,%d))==0", &team, &slot) == 2) {
        block->condition_kind = COND_ALL_BUILDINGS_DESTROYED;
        block->building_cond.team = team;
        block->building_cond.slot_count = 1;
        block->building_cond.slots[0] = slot;
        return;
    }
    /* Detect compound b() conditions: ((b(2,0)==0)&&(b(2,1)==0)&&(b(2,2)==0)&&...) */
    if (strstr(cond, "b(") && strstr(cond, "==0")) {
        block->condition_kind = COND_ALL_BUILDINGS_DESTROYED;
        block->building_cond.team = -1;
        block->building_cond.slot_count = 0;
        const char *p = cond;
        while (*p && block->building_cond.slot_count < MAX_CITY_SLOTS) {
            if (sscanf(p, " b(%d,%d)==0", &team, &slot) == 2 ||
                sscanf(p, "(b(%d,%d))==0", &team, &slot) == 2) {
                if (block->building_cond.team < 0)
                    block->building_cond.team = team;
                block->building_cond.slots[block->building_cond.slot_count++] = slot;
            }
            p++;
        }
        return;
    }
    /* Detect s(team,type,slot)==N */
    int expected = -1;
    if (sscanf(cond, "s(%d,%d,%d)==%d", &team, &slot, &expected, &expected) == 4 ||
        sscanf(cond, "(s(%d,%d,%d))==%d", &team, &slot, &expected, &expected) == 4) {
        block->condition_kind = COND_UNIT_TYPE_EXISTS;
        block->unit_state_cond.team = team;
        block->unit_state_cond.type = slot;
        block->unit_state_cond.slot = expected;
        block->unit_state_cond.expected_value = expected;
        return;
    }
    /* Detect compound s() conditions with || */
    if (strstr(cond, "s(") && strstr(cond, "==1")) {
        block->condition_kind = COND_UNIT_TYPE_EXISTS;
        block->unit_state_cond.team = -1;
        block->unit_state_cond.type = -1;
        block->unit_state_cond.slot = -1;
        block->unit_state_cond.expected_value = 1;
        const char *p = cond;
        while (*p) {
            int s_team = -1, s_type = -1, s_slot = -1;
            if (sscanf(p, " s(%d,%d,%d)==1", &s_team, &s_type, &s_slot) == 3 ||
                sscanf(p, "(s(%d,%d,%d))==1", &s_team, &s_type, &s_slot) == 3) {
                if (block->unit_state_cond.team < 0) {
                    block->unit_state_cond.team = s_team;
                    block->unit_state_cond.type = s_type;
                    block->unit_state_cond.slot = s_slot;
                }
            }
            p++;
        }
        return;
    }
    /* Detect c>s(team,type,slot) */
    if (sscanf(cond, "c>s(%d,%d,%d)", &team, &slot, &expected) == 3) {
        block->condition_kind = COND_COUNTER_GT_STATE;
        block->counter_gt_state_team = team;
        block->counter_gt_state_type = slot;
        block->counter_gt_state_slot = expected;
        return;
    }
}

static void parse_tro(Mission *mission, const char *path) {
    char *text = load_text(path);
    if (!text) return;
    ScriptBlock *block = NULL;
    for (char *line = text; line && *line;) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
            char nl = *next; *next++ = '\0';
            if (nl == '\r' && *next == '\n') next++;
        }
        char token[256];
        trim_copy(token, sizeof(token), line);
        if (token[0] == '\0') {
            line = next;
            continue;
        }
        int id = 0, enabled = 0, c_gt = 0;
        char kind[16] = { 0 };
        if (sscanf(token, "%d %15s %d", &id, kind, &enabled) == 3 &&
            (strcmp(kind, "norm") == 0 || strcmp(kind, "trip") == 0)) {
            if (mission->block_count < (int)(sizeof(mission->blocks) / sizeof(mission->blocks[0]))) {
                block = &mission->blocks[mission->block_count++];
                memset(block, 0, sizeof(*block));
                block->id = id;
                block->trip = strcmp(kind, "trip") == 0;
                block->c_gt = -1;
                block->trigger_x = -1;
                block->trigger_y = -1;
                block->condition_kind = COND_COUNTER_GT;
                block->condition_negated = (enabled == 0);
                if (sscanf(token, "%*d %*s %*d (c>%d)", &c_gt) == 1) block->c_gt = c_gt;
                if (block->trip) block->requires_player_near = true;
                /* Parse condition from block header */
                const char *cond_start = strchr(token, '(');
                if (cond_start) {
                    /* Skip leading parentheses */
                    while (*cond_start == '(') cond_start++;
                    /* Find matching close paren */
                    const char *cond_end = cond_start + strlen(cond_start);
                    while (cond_end > cond_start && *(cond_end - 1) == ')') cond_end--;
                    char cond_buf[256];
                    size_t cond_len = (size_t)(cond_end - cond_start);
                    if (cond_len < sizeof(cond_buf)) {
                        memcpy(cond_buf, cond_start, cond_len);
                        cond_buf[cond_len] = '\0';
                        /* Strip inner parentheses */
                        char inner[256];
                        const char *src = cond_buf;
                        char *dst = inner;
                        while (*src && (size_t)(dst - inner) < sizeof(inner) - 1) {
                            if (*src != '(' && *src != ')') *dst++ = *src;
                            src++;
                        }
                        *dst = '\0';
                        parse_block_condition(block, inner);
                    }
                }
            }
        } else if (block && strcmp(token, "end") == 0) {
            block = NULL;
        } else if (block) {
            ScriptCommand cmd;
            memset(&cmd, 0, sizeof(cmd));
            int v[32] = { 0 };
            int parsed = 0;
            if (sscanf(token, "msg %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4]) == 5) {
                cmd.type = SCRIPT_CMD_MSG;
                cmd.a[0] = v[2];
                script_add_command(block, cmd);
            } else if ((parsed = parse_command_ints(token, "reinforce2", v, 31)) >= 5) {
                add_reinforce_commands(block, SCRIPT_CMD_REINFORCE2, v, parsed);
            } else if ((parsed = parse_command_ints(token, "reinforce", v, 31)) >= 5) {
                add_reinforce_commands(block, SCRIPT_CMD_REINFORCE, v, parsed);
            } else if (sscanf(token, "newtype %d %d %d", &v[0], &v[1], &v[2]) == 3) {
                cmd.type = SCRIPT_CMD_NEWTYPE;
                cmd.a[0] = v[0];
                cmd.a[1] = v[1];
                cmd.a[2] = v[2];
                script_add_command(block, cmd);
                if (block->trigger_x < 0) {
                    block->trigger_x = v[0];
                    block->trigger_y = v[1];
                }
            } else if (sscanf(token, "bail %d %d", &v[0], &v[1]) == 2) {
                cmd.type = SCRIPT_CMD_BAIL;
                cmd.a[0] = v[0];
                cmd.a[1] = v[1];
                script_add_command(block, cmd);
            } else if (sscanf(token, "newrate %d %d %d", &v[0], &v[1], &v[2]) == 3) {
                cmd.type = SCRIPT_CMD_NEWRATE;
                cmd.a[0] = v[0];
                cmd.a[1] = v[1];
                cmd.a[2] = v[2];
                script_add_command(block, cmd);
            } else if (sscanf(token, "setarray %d (c+%d)", &v[0], &v[1]) == 2) {
                cmd.type = SCRIPT_CMD_SETARRAY;
                cmd.a[0] = v[0];
                cmd.a[1] = v[1];
                script_add_command(block, cmd);
            } else if (sscanf(token, "setlifes %d %d", &v[0], &v[1]) == 2) {
                cmd.type = SCRIPT_CMD_SETLIFES;
                cmd.a[0] = v[0];
                cmd.a[1] = v[1];
                script_add_command(block, cmd);
            }
        }
        line = next;
    }
    free(text);
}

static void load_city_slot_data(Mission *mission, const char *map_path) {
    if (!mission || !map_path) return;
    char scn_path[1024];
    replace_extension(scn_path, sizeof(scn_path), map_path, ".SCN");
    FILE *fp = fopen(scn_path, "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    if (size <= 0) { fclose(fp); return; }
    fseek(fp, 0, SEEK_SET);
    char *text = malloc((size_t)size + 1);
    if (!text) { fclose(fp); return; }
    if (fread(text, 1, (size_t)size, fp) != (size_t)size) {
        free(text); fclose(fp); return;
    }
    fclose(fp);
    text[size] = '\0';

    int current_team = -1;
    int team_count = 0;
    bool object_mode = false;
    int trailing_blanks = 0;
    char section[32] = { 0 };
    /* Temporary storage for team data */
    struct { int active; int race; int ai_slot_count; int ai_slots[2][2];
             int city_values[12]; int city_value_count; } teams[8] = { 0 };

    for (char *line = text; line && *line;) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
            char nl = *next; *next++ = '\0';
            if (nl == '\r' && *next == '\n') next++;
        }
        char token[256] = { 0 };
        trim_copy(token, sizeof(token), line);
        if (line == text || (line == text + 1 && token[0] != '\0')) {
            /* Skip header lines */
            line = next;
            continue;
        }
        if (token[0] == '\0') {
            if (team_count >= 8 && !object_mode && ++trailing_blanks >= 2) {
                object_mode = true;
                current_team = -1;
                section[0] = '\0';
            }
            line = next;
            continue;
        }
        trailing_blanks = 0;

        int team = -1, active = 0;
        if (sscanf(token, "TEAM %d %d", &team, &active) >= 1) {
            if (team >= 0 && team < 8) {
                current_team = team;
                teams[team].active = active != 0;
                if (team + 1 > team_count) team_count = team + 1;
            } else {
                current_team = -1;
            }
            object_mode = false;
            section[0] = '\0';
            line = next;
            continue;
        }

        if (token[0] == '%') {
            trim_copy(section, sizeof(section), token);
            line = next;
            continue;
        }

        if (!object_mode && current_team >= 0 && current_team < 8) {
            int values[32] = { 0 };
            int value_count = 0;
            const char *p = token;
            while (*p && value_count < 32) {
                while (isspace((unsigned char)*p)) p++;
                if (*p == '\0') break;
                char *end = NULL;
                long v = strtol(p, &end, 10);
                if (end == p) break;
                values[value_count++] = (int)v;
                p = end;
            }

            if (strcmp(section, "%Race") == 0) {
                if (value_count > 0) teams[current_team].race = values[0];
            } else if (strcmp(section, "%AISlots") == 0) {
                if (value_count >= 2 && teams[current_team].ai_slot_count < 2) {
                    int s = teams[current_team].ai_slot_count++;
                    teams[current_team].ai_slots[s][0] = values[0];
                    teams[current_team].ai_slots[s][1] = values[1];
                }
            } else if (strcmp(section, "%City") == 0) {
                if (teams[current_team].city_value_count == 0) {
                    teams[current_team].city_value_count = value_count > 12 ? 12 : value_count;
                    memcpy(teams[current_team].city_values, values,
                           (size_t)teams[current_team].city_value_count * sizeof(int));
                }
            }
            line = next;
            continue;
        }
        if (object_mode) break;
        line = next;
    }
    free(text);

    /* Extract city slot info for each team */
    mission->city_slot_count = 0;
    for (int t = 0; t < 8; ++t) {
        if (!teams[t].active) continue;
        int anchor_x = 0, anchor_y = 0;
        if (teams[t].ai_slot_count >= 2) {
            anchor_x = teams[t].ai_slots[1][0];
            anchor_y = teams[t].ai_slots[1][1];
        }
        if (anchor_x == 0 && teams[t].ai_slot_count >= 1) {
            anchor_x = teams[t].ai_slots[0][0];
            anchor_y = teams[t].ai_slots[0][1];
        }
        if (anchor_x == 0) continue;
        for (int s = 0; s < 5 && mission->city_slot_count < 32; ++s) {
            if (s < teams[t].city_value_count && teams[t].city_values[s * 2] > 0) {
                CitySlotInfo *info = &mission->city_slots[mission->city_slot_count++];
                info->team = t;
                info->slot = s;
                info->anchor_x = anchor_x;
                info->anchor_y = anchor_y;
                info->race = teams[t].race;
            }
        }
    }
}

void *load_mission(const char *map_path) {
    if (!map_path) return NULL;
    Mission *mission = calloc(1, sizeof(*mission));
    if (!mission) return NULL;
    char msg_path[1024], tro_path[1024];
    replace_extension(msg_path, sizeof(msg_path), map_path, ".MSG");
    replace_extension(tro_path, sizeof(tro_path), map_path, ".TRO");
    parse_messages(mission, msg_path);
    parse_tro(mission, tro_path);
    load_city_slot_data(mission, map_path);
    dropship_animation_from_sprites(map_path, &mission->dropship_animations);
    if (getenv("OPEN_RTS_DEBUG_SCRIPT")) {
        fprintf(stderr, "Dark Colony mission %s: %d messages, %d blocks, %d city slots\n",
                map_path, mission->message_count, mission->block_count, mission->city_slot_count);
    }
    return mission;
}

/* City slot offsets from DC.EXE - same as in w_map.c */
static const struct { int x; int z; } dc_city_slot_offsets[] = {
    { -64, 15 }, { 0, 0 }, { 32, 64 }, { 64, 10 }, { -32, 65 }, { 0, 32 }, { 0, 0 },
};

static fvec2_t city_slot_cell_center(const CitySlotInfo *slot_info) {
    if (!slot_info) return (fvec2_t){ 0.0f, 0.0f };
    int sx = 0, sz = 0;
    if (slot_info->slot >= 0 && slot_info->slot < 7) {
        sx = dc_city_slot_offsets[slot_info->slot].x;
        sz = dc_city_slot_offsets[slot_info->slot].z;
    }
    float cell_x = (float)slot_info->anchor_x + (float)sx * 8.0f / 256.0f;
    float cell_y = (float)slot_info->anchor_y + (float)sz * 8.0f / 256.0f;
    return fvec2_cell_center((ivec2_t){ (int)cell_x, (int)cell_y });
}

static bool building_alive_at_slot(const Mission *mission, int team, int slot,
                                    const mobj_t *units, int unit_count) {
    fvec2_t expected = (fvec2_t){ 0.0f, 0.0f };
    bool found_slot = false;
    for (int i = 0; i < mission->city_slot_count; ++i) {
        const CitySlotInfo *info = &mission->city_slots[i];
        if (info->team == team && info->slot == slot) {
            expected = city_slot_cell_center(info);
            found_slot = true;
            break;
        }
    }
    if (!found_slot) return false;
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *unit = &units[i];
        if (unit->remove || unit->hp <= 0) continue;
        if (unit->owner != (uint8_t)(team == 0 ? 0 : 1)) continue;
        fvec2_t pos = fixedvec3_xy_to_fvec2(unit->core.position);
        if (fvec2_near(pos, expected, 1.5f)) return true;
    }
    return false;
}

static bool evaluate_condition(const Mission *mission, const ScriptBlock *block,
                                const mobj_t *units, int unit_count) {
    switch (block->condition_kind) {
    case COND_ALL_BUILDINGS_DESTROYED: {
        int team = block->building_cond.team;
        for (int i = 0; i < block->building_cond.slot_count; ++i) {
            if (building_alive_at_slot(mission, team, block->building_cond.slots[i],
                                       units, unit_count))
                return false;
        }
        return block->building_cond.slot_count > 0;
    }
    case COND_UNIT_TYPE_EXISTS: {
        /* Check if any unit of the specified type exists for the team */
        int team = block->unit_state_cond.team;
        int type = block->unit_state_cond.type;
        (void)type;
        for (int i = 0; i < unit_count; ++i) {
            const mobj_t *unit = &units[i];
            if (unit->remove || unit->hp <= 0) continue;
            if (team == 0 && unit->owner != 0) continue;
            if (team != 0 && unit->owner == 0) continue;
            if (unit->native_type_id == (uint16_t)type) return true;
        }
        return false;
    }
    case COND_COUNTER_GT:
    case COND_TRIP_PLAYER_NEAR:
    case COND_COUNTER_GT_STATE:
        return true; /* handled by caller */
    }
    return false;
}

static MissionState mission_state_from_bail(int result, int code) {
    if (result == 0 && code == 1) return MISSION_WON;
    if (result == 1 && code == 2) return MISSION_LOST;
    if (result == 1 && code == 3) return MISSION_ALLY_LOST;
    return MISSION_ACTIVE;
}

void update_mission(void *ptr, level_t *map, mobj_t *units, int *unit_count,
                                effect_t *effects, int max_effects,
                                const gameinfo_t *game_info, hudtext_t *hud, float dt) {
    Mission *mission = ptr;
    if (!mission || !units || !unit_count) return;
    if (mission->state != MISSION_ACTIVE) return;
    mission->elapsed_ms += (int)(dt * 1000.0f);
    update_ai(mission, map, units, *unit_count, (int)(dt * 1000.0f));
    bool debug_script = getenv("OPEN_RTS_DEBUG_SCRIPT") != NULL;
    for (int i = 0; i < mission->block_count; ++i) {
        ScriptBlock *block = &mission->blocks[i];
        if (block->fired) continue;
        if (mission->state != MISSION_ACTIVE) break;
        bool fire = false;
        if (block->trip) {
            fire = block->trigger_x >= 0 &&
                   player_near(map, units, *unit_count, block->trigger_x, block->trigger_y);
        } else if (block->condition_kind == COND_ALL_BUILDINGS_DESTROYED) {
            fire = evaluate_condition(mission, block, units, *unit_count);
        } else if (block->condition_kind == COND_UNIT_TYPE_EXISTS) {
            fire = evaluate_condition(mission, block, units, *unit_count);
        } else if (block->c_gt >= 0) {
            fire = mission->elapsed_ms > block->c_gt * SCRIPT_COUNTER_MS;
            if (fire && block->condition_kind == COND_COUNTER_GT_STATE) {
                /* c > s(team,type,slot): check if counter exceeds some value */
                fire = true; /* simplified: fire on counter alone */
            }
        }
        if (block->condition_negated) fire = !fire;
        if (fire) {
            if (debug_script) {
                fprintf(stderr, "Dark Colony script block %d fired (%d commands)\n",
                        block->id, block->command_count);
            }
            execute_script_block(mission, block, map, units, unit_count,
                                             effects, max_effects, game_info, hud);
        }
    }
    for (int i = 0; i < (int)(sizeof(mission->dropships) / sizeof(mission->dropships[0])); ++i) {
        Dropship *ship = &mission->dropships[i];
        if (!ship->active) continue;
        update_dropship(mission, ship, map, units, unit_count,
                                    effects, max_effects, game_info,
                                    (int)(dt * 1000.0f));
    }
}

MissionState mission_get_state(const void *mission) {
    if (!mission) return MISSION_ACTIVE;
    return ((const Mission *)mission)->state;
}

void destroy_mission(void *mission) {
    free(mission);
}
