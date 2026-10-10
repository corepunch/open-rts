#define _DEFAULT_SOURCE
#include "engine.h"
#ifdef RTS_MODULE_WARCRAFT_2
#include "warcraft-2.h"
#endif
#ifdef RTS_MODULE_DARK_COLONY
#include "dark-colony.h"
#endif
#include "info.h"

bool P_IsAlly(const mobj_t *a, const mobj_t *b) {
    if (!a || !b) return false;
#ifdef RTS_MODULE_DARK_COLONY
    if (level.native_data && a->team < 8 && b->team < 8 && level.peace[a->team]) {
        if (a->allegiance == ALLEGIANCE_NEUTRAL || b->allegiance == ALLEGIANCE_NEUTRAL) return false;
        return (level.peace[a->team] & (UINT32_C(0x40000000) >> b->team)) != 0;
    }
#endif
    if (!netgame && !level.player_teams) return P_AreAllegiancesAllied(a->allegiance, b->allegiance);
    if (a->allegiance == ALLEGIANCE_NEUTRAL || b->allegiance == ALLEGIANCE_NEUTRAL)
        return false;
    const uint32_t *allies = level.sight.allies;
#ifdef RTS_MODULE_DARK_COLONY
    allies = level.peace;
#endif
    return a->owner == b->owner || (a->team < 8 && b->team < 8 &&
        (allies[a->team] & (UINT32_C(0x40000000) >> b->team)));
}

mobj_t *P_MobjById(uint32_t id) {
    if (!id || !thinkercap.next) return NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (th->function == P_MobjThinker && u->id == id && !u->remove && u->hp > 0) return u;
    }
    return NULL;
}

bool P_VentOpenTo(const level_t *map, const resourcevent_t *vent, const mobj_t *unit) {
    (void)map;
    if (!vent || !vent->active || vent->amount <= 0) return false;
#ifdef RTS_MODULE_WARCRAFT_2
    if (!unit || vent->resource_type < 0 || vent->resource_type >= 3 ||
        !mobjinfo[unit->type_id].w2.gather[vent->resource_type].capacity) return false;
#endif
    if (!vent->source_id) return true;
    const mobj_t *source = P_MobjById(vent->source_id);
#ifdef RTS_MODULE_WARCRAFT_2
    if (source && source->type_id == MT_GOLD_MINE) return true;
    if (source && (source->type_id == MT_OIL_PATCH || W2_UnderConstruction(source))) return false;
#endif
    return source && unit && (source->allegiance == ALLEGIANCE_NEUTRAL || P_IsAlly(source, unit));
}

/* A deposit structure's vent covers a 3x3 footprint centred on it so that a
 * harvester counts as docked once it touches the building. */
static void open_deposit_vent(resourcevent_t *vent, const mobj_t *source) {
    fixed2_t at = fixed3_xy(source->core.position);
    *vent = (resourcevent_t){
        .cell = { fixed_floor_int(at.x) - 1, fixed_floor_int(at.y) - 1 },
        .attachment = at,
        .footprint = { 3, 3 },
        .amount = source->info->deposit.amount,
        .rate = source->info->deposit.rate,
        .active = true,
        .resource_type = source->info->deposit.resource_type,
        .source_id = source->id,
    };
}

void P_SyncDepositStructures(level_t *map) {
    if (!map || !thinkercap.next) return;
    /* Close vents whose structure is gone. Slots are never removed because
     * harvesters hold vent indices; a closed slot is reused below. */
    for (int v = 0; v < map->resource_vent_count; ++v) {
        resourcevent_t *vent = &map->resource_vents[v];
        if (vent->source_id && !P_MobjById(vent->source_id)) {
            vent->active = false;
            vent->amount = 0;
        }
    }
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (th->function != P_MobjThinker || u->remove || u->hp <= 0 ||
            !(u->traits & MF_RESOURCE_SOURCE) || !u->info ||
            u->info->deposit.amount <= 0 || u->info->deposit.rate <= 0) continue;
        int slot = -1, free_slot = -1;
        for (int v = 0; v < map->resource_vent_count && slot < 0; ++v) {
            const resourcevent_t *vent = &map->resource_vents[v];
            if (vent->source_id == u->id) slot = v;
            else if (free_slot < 0 && vent->source_id && !vent->active) free_slot = v;
        }
        if (slot >= 0) continue;
        if (free_slot < 0) {
            resourcevent_t *vents = realloc(map->resource_vents,
                (size_t)(map->resource_vent_count + 1) * sizeof(*vents));
            if (!vents) return;
            map->resource_vents = vents;
            free_slot = map->resource_vent_count++;
        }
        open_deposit_vent(&map->resource_vents[free_slot], u);
    }
}

enum {
    RTS_HARVEST_INTERVAL_MS = 1000,
    RTS_TURN_STEP_MS = 75,
};

static const state_t *state_at(const gameinfo_t *game_info, int state_id) {
    if (!game_info || !game_info->states || state_id < 0 || state_id >= game_info->state_count)
        return NULL;
    return &game_info->states[state_id];
}

const weapondef_t *P_MobjWeapon(const mobj_t *attacker, const mobj_t *target) {
    if (!attacker || !attacker->info) return NULL;
    if (target && (target->traits & MF_FLY) && attacker->info->air_attack.damage)
        return &attacker->info->air_attack;
    return &attacker->info->attack;
}

/* Reach against target; with none yet, the longer of the two weapons. */
static fixed_t mobj_attack_range(const mobj_t *unit, const mobj_t *target) {
#ifdef RTS_MODULE_WARCRAFT_2
    (void)target;
    return W2_AttackRange(unit);
#endif
#ifdef RTS_MODULE_DARK_COLONY
    if (unit && (unit->type_id == MT_THUNDERBOLT || unit->type_id == MT_ATRIL))
        return unit->info->attack.range + DC_WeaponLevel(unit) * 2 * FIXED_ONE;
#endif
    if (!unit || !unit->info) return 0;
    const weapondef_t *air = &unit->info->air_attack,
        *weapon = target ? P_MobjWeapon(unit, target) :
                  air->damage && air->range > unit->info->attack.range ? air : &unit->info->attack;
    return weapon->range + (gameinfo && gameinfo->range_bonus ? gameinfo->range_bonus(unit, weapon) : 0);
}

static int weapon_damage(const mobj_t *unit, const weapondef_t *weapon) {
#ifdef RTS_MODULE_DARK_COLONY
    int tier = DC_WeaponLevel(unit);
    if (tier && weapon->upgrade_damage[tier - 1]) return weapon->upgrade_damage[tier - 1];
#else
    (void)unit;
#endif
    return weapon->damage;
}

static bool mobj_armed(const mobj_t *unit) {
    return unit && unit->info &&
        (weapon_damage(unit, &unit->info->attack) != 0 || unit->info->air_attack.damage != 0);
}

static int weapon_cooldown_ms(const mobj_t *unit, const weapondef_t *weapon) {
#ifdef RTS_MODULE_DARK_COLONY
    if (unit && unit->type_id == MT_ATRIL && DC_WeaponLevel(unit)) return 150 * 66;
#else
    (void)unit;
#endif
    return weapon->cooldown_ms;
}

static void start_attack_cooldown(mobj_t *attacker, const weapondef_t *weapon) {
    if (weapon_cooldown_ms(attacker, weapon) > 0)
        attacker->attack.cooldown_left_ms = weapon_cooldown_ms(attacker, weapon);
    if (weapon->shots > 0 && ++attacker->attack.shots >= weapon->shots) {
        attacker->attack.shots = 0;
        attacker->attack.cooldown_left_ms = weapon->reload_ms;
    }
}

static int mobj_harvest_state(const mobj_t *unit) {
    return unit && unit->info ? unit->info->harvest.state_id : 0;
}

static int mobj_harvest_capacity(const mobj_t *unit) {
    return unit && unit->info && unit->harvest.resource_type >= 0 &&
           unit->harvest.resource_type < RTS_MAX_RESOURCES ?
        unit->info->harvest.resources[unit->harvest.resource_type].capacity : 0;
}

int P_ScaleIncome(const level_t *map, int owner, int amount) {
    if (!map || owner < 0 || owner >= 8 || map->income_scale[owner] == 0) return amount;
    /* DC.EXE 0x412daa..0x412dbd: credits * scale >> 8, rounding toward zero. */
    return (int)(((int64_t)amount * map->income_scale[owner]) / 256);
}

bool P_HarvesterDocked(const mobj_t *unit) {
    if (!unit || !unit->info || !unit->info->harvest.unload_state_id) return false;
    return unit->harvest.phase == HARVEST_PHASE_TURNING ||
           unit->harvest.phase == HARVEST_PHASE_MINING ||
           unit->harvest.phase == HARVEST_PHASE_UNLOAD_TURNING ||
           unit->harvest.phase == HARVEST_PHASE_UNLOADING;
}

static bool send_harvester_home(level_t *map, mobj_t *unit) {
    if (!map || !unit) return false;
    fixed2_t unit_pos = fixed3_xy(unit->core.position);
    mobj_t *best = NULL;
    fixed2_t best_position = {0};
    int64_t best_d2 = INT64_MAX;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *base = (mobj_t *)th;
        if (base->remove || base->hp <= 0) continue;
        if (!P_IsAlly(base, unit) || (base->traits & MF_RESOURCE_BASE) == 0) continue;
        fixed2_t base_position = fixed3_xy(base->core.position);
        if ((!gameinfo || !gameinfo->harvest_dropoff_matches) && base->info &&
            base->info->footprint.w > 0) {
            irect_t bounds = P_MobjCells(base);
            if (!P_ApproachFootprint(unit, (ivec2_t){bounds.x, bounds.y},
                                    (isize2_t){bounds.w, bounds.h}, &base_position)) continue;
        }
        if (gameinfo && gameinfo->harvest_dropoff_matches &&
            !gameinfo->harvest_dropoff_matches(unit, unit->harvest.resource_type,
                                              base, &base_position)) continue;
        if (gameinfo && gameinfo->harvest_dropoff_matches &&
            !P_CheckPosition(map, unit, base_position)) continue;
        int64_t d2 = fixed2_distance_squared64(unit_pos, base_position);
        if (d2 < best_d2) {
            best_d2 = d2;
            best = base;
            best_position = base_position;
        }
    }
    if (!best) return false;
    if (!P_MoveUnitTo(map, unit, best_position)) return false;
    unit->movement.order_id = 0;
    unit->harvest.base = best;
    unit->harvest.return_position = unit->movement.goal;
    unit->harvest.phase = HARVEST_PHASE_TO_BASE;
    return true;
}

static bool send_harvester_to_vent(level_t *map, mobj_t *unit, const resourcevent_t *vent) {
    if (!map || !unit || !vent) return false;
    fixed2_t bay = vent->attachment;
    if (!P_CheckPosition(map, unit, bay) &&
        !P_ApproachFootprint(unit, vent->cell, vent->footprint, &bay)) return false;
    if (!P_MoveUnitTo(map, unit, bay)) return false;
    unit->movement.order_id = 0;
    unit->harvest.phase = HARVEST_PHASE_TO_MINE;
    return true;
}

static bool turn_unit_toward(mobj_t *unit, angle_t desired, int dt_ms) {
    if (R_Policy()->turning == TURN_INSTANT) {
        unit->core.angle = desired;
        unit->movement.turn_timer_ms = 0;
        return true;
    }
    if (unit->info && unit->info->turn_step) {
        angle_t step = unit->info->turn_step;
        int32_t delta = (int32_t)(desired - unit->core.angle);
        if (angle_distance(desired, unit->core.angle) <= step) unit->core.angle = desired;
        else unit->core.angle += delta > 0 ? step : 0u - step;
        return unit->core.angle == desired;
    }
    if (angle_distance(desired, unit->core.angle) < ANG45 / 8u) {
        unit->movement.turn_timer_ms = 0;
        return true;
    }
    unit->movement.turn_timer_ms -= dt_ms;
    if (unit->movement.turn_timer_ms <= 0) {
        int32_t delta = (int32_t)(desired - unit->core.angle);
        unit->core.angle += delta > 0 ? ANG45 / 4u : 0u - ANG45 / 4u;
        unit->movement.turn_timer_ms = RTS_TURN_STEP_MS;
    }
    return false;
}

/* Leave room for a transporter exiting the same bay. The regular movement
 * goal is temporary; harvest.return_position remains the required dock. */
static void yield_harvest_bay(level_t *map, mobj_t *unit) {
    if (!fixed2_near(unit->movement.goal, unit->harvest.return_position, FIXED_LIT(0.001))) return;
    fixed2_t position = fixed3_xy(unit->core.position);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        const mobj_t *other = (const mobj_t *)th;
        if (other == unit || other->remove || other->hp <= 0 ||
            other->harvest.base != unit->harvest.base ||
            other->harvest.phase != HARVEST_PHASE_TO_MINE || other->harvest.cargo) continue;
        fixed2_t from = fixed3_xy(other->core.position);
        fixed_t contact = P_MobjRadius(unit) + P_MobjRadius(other) + FIXED_STEP_PER_TIC(unit->speed);
        if (fixed2_distance_squared64(position, from) > fixed_sq64(contact)) continue;
        fixed2_t direction = fixed2_sub(other->movement.goal, from);
        fixed2_t best = position;
        int64_t best_clearance = 0;
        ivec2_t cell = fixed2_cell(position);
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x) {
                if (!x && !y) continue;
                fixed2_t candidate = fixed2_cell_center(ivec2_add(cell, (ivec2_t){x,y}));
                if (!P_CheckPosition(map, unit, candidate)) continue;
                bool occupied = false;
                for (thinker_t *it = thinkercap.next; it != &thinkercap; it = it->next) {
                    if (it->function != P_MobjThinker) continue;
                    const mobj_t *occupant = (const mobj_t *)it;
                    if (occupant == unit || occupant->remove || occupant->hp <= 0 ||
                        !(occupant->traits & MF_MOBILE)) continue;
                    fixed_t radius = P_MobjRadius(unit) + P_MobjRadius(occupant);
                    if (fixed2_distance_squared64(candidate, fixed3_xy(occupant->core.position)) < fixed_sq64(radius))
                        occupied = true;
                }
                if (occupied) continue;
                fixed2_t delta = fixed2_sub(candidate, from);
                int64_t clearance = (int64_t)direction.x * delta.y - (int64_t)direction.y * delta.x;
                if (clearance < 0) clearance = -clearance;
                if (clearance > best_clearance) {
                    best_clearance = clearance;
                    best = candidate;
                }
            }
        if (best_clearance > 0) P_MoveUnitTo(map, unit, best);
        return;
    }
}

static void apply_state_visuals(const gameinfo_t *game_info, mobjcore_t *mobj,
                                const state_t *state, bool apply_offsets) {
    if (!game_info || !mobj || !state) return;
    mobj->sprite_id = state->sprite;
    mobj->frame = state->frame + mobj->state_frame;
    mobj->render_flags = 0;
    mobj->render_remap = 0;
    mobj->render_intensity = 16;
    if (apply_offsets) mobj->render_offset = (ivec2_t){ 0, 0 };
    if (mobj->sprite_id >= 0 && mobj->sprite_id < game_info->sprite_count &&
        game_info->sprnames && game_info->sprnames[mobj->sprite_id]) {
        snprintf(mobj->sprite_name, sizeof(mobj->sprite_name), "%s",
                 game_info->sprnames[mobj->sprite_id]);
    }
}

bool P_SetMobjStateFrame(mobj_t *unit, int state_id, int frame) {
    const gameinfo_t *game_info = gameinfo;
    if (!game_info || !unit || unit->remove) return false;
    int guard = 0;
    while (guard < game_info->state_count + 1) {
        if (state_id == game_info->null_state || state_id < 0 ||
            state_id >= game_info->state_count) {
            unit->core.state_id = game_info->null_state;
            unit->core.state_frame = 0;
            unit->core.tics = 0;
            unit->core.momentum = fixed3_zero();
            P_RemoveMobj(unit);
            return false;
        }
        const state_t *state = &game_info->states[state_id];
        if (frame >= P_StateFrames(state)) {
            state_id = state->nextstate;
            frame = 0;
            ++guard;
            continue;
        }
        unit->core.state_id = state_id;
        unit->core.state_frame = frame;
        unit->core.tics = P_StateTics(state, frame);
#ifdef RTS_MODULE_WARCRAFT_2
        if (unit->core.tics > 0) {
            if (unit->w2.buffs[W2_BUFF_SLOW]) unit->core.tics *= 2;
            if (unit->w2.buffs[W2_BUFF_HASTE] && unit->core.tics > 1) unit->core.tics /= 2;
        }
#endif
        apply_state_visuals(game_info, &unit->core, state, false);
        debug_effects_log("state unit type=%u state=%d sprite=%d frame=%d tics=%d",
                          unit->type_id, unit->core.state_id, unit->core.sprite_id,
                          unit->core.frame, unit->core.tics);
        if (state->group == 3) {
            const char *sprite_name = "(unknown)";
            if (unit->core.sprite_id >= 0 && unit->core.sprite_id < game_info->sprite_count &&
                game_info->sprnames && game_info->sprnames[unit->core.sprite_id]) {
                sprite_name = game_info->sprnames[unit->core.sprite_id];
            }
            debug_effects_log("shoot state unit_type=%u state=%d sprite=%s frame=%d",
                              unit->type_id, unit->core.state_id,
                              sprite_name, unit->core.frame);
        }
        if (state->action) state->action(unit);
        if (unit->remove || unit->core.state_id != state_id ||
            unit->core.state_frame != frame) return !unit->remove;
        if (unit->core.tics != 0) return true;
        ++frame;
    }
    P_RemoveMobj(unit);
    return false;
}

bool P_SetMobjState(mobj_t *unit, int state_id) {
    return P_SetMobjStateFrame(unit, state_id, 0);
}

bool P_TickMobjState(mobj_t *unit) {
    if (!gameinfo || !unit || unit->remove) return false;
    if (unit->core.state_id <= 0) return false;
    if (unit->core.tics > 0) unit->core.tics--;
    if (unit->core.tics != 0) return true;
    return P_SetMobjStateFrame(unit, unit->core.state_id, unit->core.state_frame + 1);
}

production_t *P_EnsureMobjProduction(mobj_t *unit) {
    if (!unit) return NULL;
    if (!unit->production) unit->production = calloc(1, sizeof(*unit->production));
    return unit->production;
}

void P_FreeMobjProduction(mobj_t *unit) {
    if (!unit) return;
    free(unit->production);
    unit->production = NULL;
}

void P_ApplyActorTypeDefaults(mobj_t *unit, const mobjtype_t *type) {
    if (!unit || !type) return;
    if (unit->info != type) {
        unit->native_type_id = type->native_type_id;
        unit->traits = type->traits;
    }
    unit->info = type;
    unit->type_id = type->id;
    if (unit->speed <= 0) unit->speed = type->speed;
    if (unit->max_hp <= 0) unit->max_hp = type->max_hp;
    if (unit->hp <= 0) unit->hp = unit->max_hp;
    if (unit->core.render_intensity == 0) unit->core.render_intensity = 16;
    if (unit->harvest.target == 0) unit->harvest.target = -1;
    if (unit->core.sprite_name[0] == '\0' && type->sprite_name)
        snprintf(unit->core.sprite_name, sizeof(unit->core.sprite_name), "%s", type->sprite_name);
}

static const mobjtype_t *mobj_type(uint16_t type) {
    for (int i = 0; i < num_actor_types; ++i)
        if (actor_types[i].id == type) return &actor_types[i];
    return NULL;
}

mobj_t *P_SpawnMobj(fixed3_t position, uint16_t type) {
    mobj_t *mobj = calloc(1, sizeof(*mobj));
    if (!mobj) return NULL;
    mobj->core.position = position;
    mobj->type_id = type;
    mobj->id = ++level.next_mobj_id;
#ifdef RTS_MODULE_DARK_COLONY
    mobj->ability_charge = 64; /* DC.EXE object +0x0a at creation. */
#endif
    P_ApplyActorTypeDefaults(mobj, mobj_type(type));
    P_InitMobj(gameinfo, mobj);
#ifdef RTS_MODULE_WARCRAFT_2
    if (type < NUMMOBJTYPES) mobj->w2.mana = mobjinfo[type].w2.mana.initial;
#endif
    if (gameinfo && type < gameinfo->mobj_type_count)
        mobj->missile.damage = gameinfo->mobjinfo[type].damage;
    mobj->thinker.function = P_MobjThinker;
    P_AddThinker(&mobj->thinker);
    return mobj;
}

void P_InitMobj(const gameinfo_t *game_info, mobj_t *unit) {
    if (!game_info || !unit || !game_info->mobjinfo ||
        unit->type_id <= 0 || unit->type_id >= game_info->mobj_type_count) {
        return;
    }
    if (unit->core.render_intensity == 0) unit->core.render_intensity = 16;
    const mobjinfo_t *info = &game_info->mobjinfo[unit->type_id];
    if (unit->core.position.z == 0) unit->core.position.z = info->spawnz;
    if (unit->max_hp <= 0) unit->max_hp = info->spawnhealth;
    if (unit->hp <= 0) unit->hp = unit->max_hp;
    if (unit->speed <= 0) unit->speed = info->speed * FIXED_ONE;
    if (unit->radius <= FIXED_LIT(0.05)) {
        unit->radius = info->radius * FIXED_ONE / 32;
        if (unit->radius < FIXED_LIT(0.32)) unit->radius = FIXED_LIT(0.32);
        if (unit->radius > FIXED_LIT(0.90)) unit->radius = FIXED_LIT(0.90);
    }
    if (unit->core.state_id <= 0) {
        const state_t *state = state_at(game_info, info->spawnstate);
        if (!state) return;
        unit->core.state_id = info->spawnstate;
        unit->core.state_frame = 0;
        unit->core.tics = P_StateTics(state, 0);
    }
    apply_state_visuals(game_info, &unit->core,
                        state_at(game_info, unit->core.state_id), false);
}


angle_t P_PointToAngle(fixed_t dx, fixed_t dy) {
    return angle_from_screen_vector_fixed(dx, dy);
}

static angle_t angle_from_map_vector_fixed(const level_t *map, fixed_t dx, fixed_t dy) {
    (void)map;
#if RTS_WORLD_Y_UP
    dy = -dy;
#endif
    return angle_from_screen_vector_fixed(dx, dy);
}

#ifdef RTS_MODULE_WARCRAFT_2
static fixed2_t snap_move_direction_45(fixed2_t direction) {
    const fixed_t diagonal_threshold = FIXED_LIT(0.41421356237);
    const fixed_t diagonal = FIXED_LIT(0.70710678118);
    fixed_t ax = direction.x < 0 ? -direction.x : direction.x;
    fixed_t ay = direction.y < 0 ? -direction.y : direction.y;
    fixed_t sx = direction.x < 0 ? -1 : 1, sy = direction.y < 0 ? -1 : 1;
    if (ax == 0) return (fixed2_t){0, sy * FIXED_ONE};
    if (ay == 0) return (fixed2_t){sx * FIXED_ONE, 0};
    if (fixed_mul32(ax, diagonal_threshold) < ay || fixed_mul32(ay, diagonal_threshold) < ax)
        return ax > ay ? (fixed2_t){sx * FIXED_ONE, 0} : (fixed2_t){0, sy * FIXED_ONE};
    return (fixed2_t){sx * diagonal, sy * diagonal};
}

#endif

void P_AngleToVec(angle_t angle, fixed_t *dx, fixed_t *dy) {
    angle_to_screen_vector_fixed(angle, dx, dy);
}

static bool P_CanDamage(const mobj_t *attacker, const mobj_t *victim) {
#ifdef RTS_MODULE_WARCRAFT_2
    if (!(mobjinfo[attacker->type_id].w2.target_mask & (1 << mobjinfo[victim->type_id].w2.domain)) ||
        (mobjinfo[victim->type_id].w2.attributes & W2_INDESTRUCTIBLE)) return false;
#endif
    if ((attacker->traits & MF_LANDMINE) && (victim->traits & MF_FLY)) return false;
    const weapondef_t *weapon = P_MobjWeapon(attacker, victim);
    uint8_t reach = weapon ? weapon->targets : 0;
    if (reach && !(reach & ((victim->traits & MF_FLY) ? MOBJ_TARGET_AIR : MOBJ_TARGET_GROUND)))
        return false;
    const mobjtype_t *shot = weapon ? mobj_type(weapon->projectile_type) : NULL;
    if (!shot) return true;
    unsigned armor = victim->info ? victim->info->armor_class : 0;
    return !shot->blast.damage_factors || armor >= (unsigned)shot->blast.armor_classes ||
           shot->blast.damage_factors[armor] != 0;
}

bool P_CanTarget(const mobj_t *attacker, const mobj_t *victim) {
    if (!attacker || !victim || attacker == victim || victim->remove || victim->hp <= 0 ||
        !(attacker->traits & (MF_ATTACK | MF_HEAL | MF_REPAIR)) ||
        (victim->traits & (MF_MISSILE | MF_NOBLOCKMAP))) return false;
    if (attacker->traits & (MF_HEAL | MF_REPAIR)) {
        return victim != attacker && P_IsAlly(attacker, victim) &&
            victim->hp < victim->max_hp &&
            (victim->traits & MF_MOBILE) && !(victim->traits & MF_FLY) &&
            ((victim->traits & MF_HUMAN) ? (attacker->traits & MF_HEAL) :
                                         (attacker->traits & MF_REPAIR));
    }
    return !P_IsAlly(attacker, victim) && P_CanDamage(attacker, victim);
}

/* The cells an actor stands on: its footprint around its centre, or the
 * one cell under a point actor. */
irect_t P_MobjCells(const mobj_t *unit) {
    fixed2_t centre = fixed3_xy(unit->core.position);
    isize2_t foot = unit->info ? unit->info->footprint : (isize2_t){0, 0};
    if (foot.w <= 0 || foot.h <= 0) {
        ivec2_t cell = fixed2_cell(centre);
        return (irect_t){cell.x, cell.y, 1, 1};
    }
    if (unit->info->corner_anchor) {
        ivec2_t cell = fixed2_cell(centre);
        return (irect_t){cell.x, cell.y, foot.w, foot.h};
    }
    ivec2_t corner = fixed2_footprint_corner(centre, foot);
    return (irect_t){corner.x, corner.y, foot.w, foot.h};
}

/* Range to a footprint is Warcraft's tile distance: the gap between the two
 * cell rectangles, so every neighbouring cell is one away. Point targets keep
 * the centre distance that the other games tune their weapons by. */
static bool within_attack_range(const mobj_t *attacker, const mobj_t *target, fixed_t range) {
#ifdef RTS_MODULE_WARCRAFT_2
    if (W2_Distance(attacker, target) < mobjinfo[attacker->type_id].w2.min_attack_range) return false;
#endif
    const weapondef_t *weapon = P_MobjWeapon(attacker, target);
    fixed_t least = weapon ? weapon->min_range : 0;
    if (target->info && target->info->footprint.w > 0 && target->info->footprint.h > 0) {
        irect_t a = P_MobjCells(attacker), b = P_MobjCells(target);
        int dx = b.x > a.x + a.w - 1 ? b.x - (a.x + a.w - 1) :
                 a.x > b.x + b.w - 1 ? a.x - (b.x + b.w - 1) : 0;
        int dy = b.y > a.y + a.h - 1 ? b.y - (a.y + a.h - 1) :
                 a.y > b.y + b.h - 1 ? a.y - (b.y + b.h - 1) : 0;
        return FIXED_FROM_INT(dx > dy ? dx : dy) <= range && FIXED_FROM_INT(dx > dy ? dx : dy) >= least;
    }
    int64_t d2 = fixed2_distance_squared64(fixed3_xy(target->core.position),
                                           fixed3_xy(attacker->core.position));
    return d2 <= fixed_sq64(range) && d2 >= fixed_sq64(least);
}

bool P_InAttackRange(const mobj_t *attacker, const mobj_t *target) {
    if (!attacker || !target) return false;
    return within_attack_range(attacker, target, mobj_attack_range(attacker, target));
}

static mobj_t *attack_target_in_range(const mobj_t *attacker) {
#ifdef RTS_MODULE_WARCRAFT_2
    if (attacker->w2.cast.spell || attacker->w2.repair.target || attacker->w2.carrier) return NULL;
#endif
    if (attacker->move_only && P_HasMoveOrder(attacker) && !attacker->attack.target) return NULL;
    if (!(attacker->traits & (MF_ATTACK | MF_HEAL | MF_REPAIR)) ||
        !mobj_armed(attacker))
        return NULL;
    mobj_t *target = attacker->attack.target;
    if (target && !target->remove && target->hp > 0 &&
        P_VisibleTo(attacker, target) &&
        !(target->traits & (MF_NOBLOCKMAP | MF_MISSILE)) &&
        P_CanTarget(attacker, target) &&
        within_attack_range(attacker, target, mobj_attack_range(attacker, target)))
        return target;
    if (attacker->traits & MF_NOAUTOTARGET) return NULL;
    target = NULL;
    int64_t best2 = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        mobj_t *candidate = (mobj_t *)th;
        /* Neutral things (mines, critters) are attacked only on order. */
        if (candidate == attacker || candidate->remove || candidate->hp <= 0 ||
            (candidate->traits & (MF_NOBLOCKMAP | MF_MISSILE)) ||
            candidate->allegiance == ALLEGIANCE_NEUTRAL ||
            !P_CanTarget(attacker, candidate) ||
            !P_VisibleTo(attacker, candidate) ||
            !within_attack_range(attacker, candidate, mobj_attack_range(attacker, candidate))) continue;
        int64_t dist2 = fixed2_distance_squared64(
            fixed3_xy(candidate->core.position),
            fixed3_xy(attacker->core.position));
        if (!target || dist2 < best2) { best2 = dist2; target = candidate; }
    }
    return target;
}

/* Doom's damage source wakes the victim and becomes its target. Keep an
 * existing live enemy so repeated hits do not restart its pursuit. */
static void provoke(mobj_t *target, mobj_t *source) {
    mobj_t *enemy = target->attack.target;
    if ((target->traits & MF_ATTACK) && source && source != target &&
        !source->remove && source->hp > 0 && !P_IsAlly(target, source) &&
        (!enemy || enemy->remove || enemy->hp <= 0 || P_IsAlly(target, enemy)))
        target->attack.target = source;
}

void P_DamageMobj(mobj_t *target, mobj_t *source, int damage) {
    if (!target || target->remove || target->hp <= 0 || damage <= 0) return;
#ifdef RTS_MODULE_WARCRAFT_2
    if (target->w2.buffs[W2_BUFF_ARMOR] ||
        (mobjinfo[target->type_id].w2.attributes & W2_INDESTRUCTIBLE)) return;
#endif
    target->hp -= damage;
#ifdef RTS_MODULE_STARCRAFT
    target->sc.flags = (uint8_t)(target->sc.flags | SC_HIT);
    target->sc.attacker = source && source->owner < 8 ? source->owner : 255;
#endif
    if (target->info && target->info->damage_action) target->info->damage_action(target);
    if (target->owner == consoleplayer && source && source->owner != consoleplayer)
        S_ActorSound(target, SE_ATTACKED);
    if (target->hp > 0) {
        provoke(target, source);
        return;
    }

    target->hp = 0;
    P_MobjSetSelected(target, false);
    target->traits &= ~(MF_SELECTABLE | MF_MOBILE | MF_FLY |
                        MF_ATTACK | MF_HARVESTER | MF_DETECTOR);
    P_ClearMove(target);
    target->movement.order_arrived = false;
    target->harvest.target = -1;
    target->harvest.timer_ms = 0;
    target->harvest.phase = 0;
    target->harvest.cargo = 0;
    target->attack.target = NULL;
    target->attack.cooldown_left_ms = 0;
    target->core.momentum = fixed3_zero();
    S_StopSound(target);
    S_ActorSound(target, SE_DEATH);
    if (gameinfo && target->type_id > 0 &&
        target->type_id < gameinfo->mobj_type_count) {
        int deathstate = gameinfo->mobjinfo[target->type_id].deathstate;
        P_SetMobjState(target, deathstate);
    } else {
        P_RemoveMobj(target);
    }
}

mobj_t *P_SpawnMissile(mobj_t *source, mobj_t *target, uint16_t type) {
    if (!source || !target || source->remove || target->remove || target->hp <= 0 ||
        !gameinfo || !gameinfo->mobjinfo || type == 0 || type >= gameinfo->mobj_type_count)
        return NULL;

    const mobjinfo_t *missile_info = &gameinfo->mobjinfo[type];
    const mobjtype_t *definition = mobj_type(type);
    const missiledef_t *flight = definition ? &definition->missile : NULL;
    fixed2_t offset = { target->core.position.x - source->core.position.x,
                        target->core.position.y - source->core.position.y };
    fixed_t distance = fixed2_length(offset);
    if ((!flight || flight->step <= 0) && missile_info->speed <= 0) return NULL;
    fixed2_t direction = offset;
    if (distance > 0) direction = (fixed2_t){ fixed_div32(offset.x, distance), fixed_div32(offset.y, distance) };

    mobj_t *missile = P_SpawnMobj(source->core.position, type);
    if (!missile) return NULL;
    missile->target = source; /* Doom stores the missile originator here. */
    missile->owner = source->owner;
    missile->team = source->team;
    missile->allegiance = source->allegiance;
    missile->missile.damage = missile_info->damage;
#ifdef RTS_MODULE_DARK_COLONY
    if (type == MT_SCOUT_BOMB && DC_WeaponLevel(source)) {
        missile->missile.damage += 25 * DC_WeaponLevel(source);
        missile->traits |= MF_RENDERABLE;
        P_SetMobjState(missile, S_SPIKE_BULLET1);
    }
#endif
    missile->core.angle = angle_from_map_vector_fixed(&level, direction.x, direction.y);
    if (flight && flight->period_ms > 0 && flight->step > 0) {
#ifdef RTS_MODULE_DARK_COLONY
        DC_AimMissile(missile, target->core.position);
#else
        missile->core.momentum = (fixed3_t){ fixed_mul32(direction.x, flight->step),
                                             fixed_mul32(direction.y, flight->step), 0 };
        missile->missile.duration = (int)(((int64_t)distance * FIXED_ONE) / flight->step);
        if (missile->missile.duration < 1) missile->missile.duration = 1;
#endif
        if (flight->weave && gameinfo->random_table)
            missile->missile.phase = gameinfo->random_table[++level.random_index] % flight->weave_count;
#ifndef RTS_MODULE_DARK_COLONY
        if (!flight->arc)
            missile->core.momentum.z = (target->core.position.z - source->core.position.z) /
                                      missile->missile.duration;
#endif
        return missile;
    }
    fixed_t missile_step = FIXED_STEP_PER_TIC(missile_info->speed * FIXED_ONE);
    missile->core.momentum = (fixed3_t){ fixed_mul32(direction.x, missile_step),
                                         fixed_mul32(direction.y, missile_step), 0 };

    /* Doom's P_CheckMissileSpawn advances half a tic and immediately explodes
     * a missile that starts inside a blocking line. */
    fixed2_t half_step = { missile->core.momentum.x / 2, missile->core.momentum.y / 2 };
    fixed2_t half_position = fixed2_add(fixed3_xy(missile->core.position), half_step);
    if (level.width > 0 &&
        !P_CheckPosition(&level, missile, half_position)) {
        P_ExplodeMissile(missile);
        return missile;
    }
    return missile;
}

void P_ExplodeMissile(mobj_t *missile) {
    if (!missile || missile->remove) return;
    missile->core.momentum = fixed3_zero();
    if (!gameinfo || !gameinfo->mobjinfo || missile->type_id >= gameinfo->mobj_type_count) {
        P_RemoveMobj(missile);
        return;
    }
    missile->traits &= ~MF_MISSILE;
    missile->traits |= MF_NOBLOCKMAP;
    S_ActorSound(missile, SE_EXPLODE);
    if (missile->info && missile->info->blast.size) missile->traits |= MF_RENDERABLE;
    if (!P_SetMobjState(missile, gameinfo->mobjinfo[missile->type_id].deathstate)) return;
}

static int missile_damage(const mobj_t *missile, const mobj_t *victim) {
    const blastdef_t *blast = missile->info ? &missile->info->blast : NULL;
    unsigned armor = victim->info ? victim->info->armor_class : 0;
    int factor = blast && blast->damage_factors && armor < (unsigned)blast->armor_classes
               ? blast->damage_factors[armor] : 256;
    return (missile->missile.damage * factor) >> 8;
}

void A_Explode(mobj_t *actor) {
    if (!actor || !actor->info) return;
    const blastdef_t *blast = &actor->info->blast;
    if (!blast->weights || blast->size <= 0) return;
    ivec2_t center = fixed2_cell(fixed3_xy(actor->core.position));
#ifdef RTS_MODULE_DARK_COLONY
    for (int y = 0; y < blast->size; ++y) {
        for (int x = 0; x < blast->size; ++x) {
            ivec2_t cell = ivec2_add(center, (ivec2_t){x - blast->size/2, y - blast->size/2});
            mobj_t *victim = DC_Occupant(cell, false, false);
            if (!victim) victim = DC_Occupant(cell, false, true);
            if (!victim) continue;
            int weight = blast->weights[y * blast->size + x] * 256 / 100;
            if (victim->owner == actor->owner) weight /= 4;
            int damage = missile_damage(actor, victim) * weight >> 8;
            P_DamageMobj(victim, actor->target, DC_DefendedDamage(victim, damage));
        }
    }
#else
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *victim = (mobj_t *)th;
        if (victim == actor || victim->remove || victim->hp <= 0 ||
            (victim->traits & (MF_NOBLOCKMAP | MF_MISSILE | MF_FLY))) continue;
        ivec2_t position = fixed2_cell(fixed3_xy(victim->core.position));
        ivec2_t cell = ivec2_add(ivec2_sub(position, center),
            (ivec2_t){ blast->size / 2, blast->size / 2 });
        if (cell.x < 0 || cell.y < 0 || cell.x >= blast->size || cell.y >= blast->size) continue;
        int damage = missile_damage(actor, victim);
        int weight = blast->weights[cell.y * blast->size + cell.x] * 256 / 100;
        if (victim->owner == actor->owner) weight /= 4;
        damage = (damage * weight) >> 8;
        P_DamageMobj(victim, actor->target, damage);
    }
#endif
}

/* A type that stands still by nature (a sieged tank) may deploy back; a
 * mobile one only while it still moves, so a deploy in progress holds. */
bool P_Deploy(mobj_t *actor) {
    if (!actor || actor->remove || actor->hp <= 0 || !actor->info ||
        !actor->info->deploy.state || !actor->info->deploy.type ||
        actor->core.state_id == actor->info->deploy.state ||
        !(actor->traits & MF_MOBILE) != !(actor->info->traits & MF_MOBILE)) return false;
    P_ClearMove(actor);
    actor->movement.order_id = 0;
    actor->traits &= ~MF_MOBILE;
    actor->attack.target = NULL;
    actor->core.momentum = fixed3_zero();
    S_ActorSound(actor, SE_DEPLOY);
    return P_SetMobjState(actor, actor->info->deploy.state);
}

void A_Deploy(mobj_t *actor) {
    const mobjtype_t *type = mobj_type(actor->info->deploy.type);
    if (!type) return;
    unsigned selected = actor->traits & MF_SELECTED;
    actor->speed = type->speed;
    actor->core.sprite_name[0] = '\0';
    P_ApplyActorTypeDefaults(actor, type);
    actor->traits |= selected;
    P_SetMobjState(actor, gameinfo->mobjinfo[type->id].spawnstate);
}

/* One hit of weapon on victim, divided for splash and bounces: the versus
 * table, then the game's rules (upgrades, armor, damage type, shields). */
static int weapon_hit(mobj_t *attacker, const weapondef_t *weapon, mobj_t *victim, int divisor) {
    int damage = weapon_damage(attacker, weapon);
    unsigned armor = victim->info ? victim->info->armor_class : 0;
    if (armor < 3 && weapon->versus[armor]) damage = weapon->versus[armor];
    if (gameinfo->hit_damage && damage > 0) damage = gameinfo->hit_damage(attacker, weapon, victim, damage, divisor);
    else damage /= divisor;
#ifdef RTS_MODULE_DARK_COLONY
    /* The immediate fallback stands in for a native direct-impact shot. */
    damage = DC_DefendedDamage(victim, damage);
    if (damage > 0) damage = DC_DaylightDamage(attacker, damage);
#endif
    return damage;
}

/* Every hit of one attack. A hit the game took entirely on shields still
 * provokes the victim. */
static void strike(mobj_t *attacker, const weapondef_t *weapon, mobj_t *victim, int divisor) {
    for (int i = 0; i < (weapon->hits ? weapon->hits : 1) && !victim->remove && victim->hp > 0; ++i) {
        int damage = weapon_hit(attacker, weapon, victim, divisor);
        if (damage > 0) P_DamageMobj(victim, attacker, damage);
        else if (gameinfo->hit_damage) provoke(victim, attacker);
    }
}

/* StarCraft's splash: every other unit on the target's layer (flyers only
 * for air splash) takes full, half or a quarter of the damage by how far
 * its edge lies from where the target stood. */
static void splash(mobj_t *attacker, const weapondef_t *weapon, const mobj_t *target, fixed2_t at, bool flying) {
    if (weapon->splash == SPLASH_NONE) return;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *victim = (mobj_t *)th;
        if (th->function != P_MobjThinker || victim == target || victim == attacker || victim->remove ||
            victim->hp <= 0 || (victim->traits & (MF_NOBLOCKMAP | MF_MISSILE))) continue;
        bool flyer = (victim->traits & MF_FLY) != 0;
        if (weapon->splash == SPLASH_AIR ? !flyer : flyer != flying) continue;
        if (weapon->splash == SPLASH_ENEMY && P_IsAlly(attacker, victim)) continue;
        if (!P_CanDamage(attacker, victim)) continue;
        fixed_t d = fixed2_length(fixed2_sub(at, fixed3_xy(victim->core.position))) - P_MobjRadius(victim);
        int divisor = d <= weapon->radius[0] ? 1 : d <= weapon->radius[1] ? 2 : d <= weapon->radius[2] ? 4 : 0;
        if (divisor) strike(attacker, weapon, victim, divisor);
    }
}

/* A bouncing shot (the Mutalisk's glaive) jumps to the nearest enemy the
 * attacker could target, other than the last two it hit (OpenBW keeps the
 * previous bounce target), each hit a third of the one before. */
static void bounce(mobj_t *attacker, const weapondef_t *weapon, const mobj_t *from) {
    const mobj_t *previous = NULL;
    for (int b = 0, divisor = 3; b < weapon->bounces; ++b, divisor *= 3) {
        mobj_t *next = NULL;
        int64_t best = fixed_sq64(WEAPON_BOUNCE_RANGE);
        fixed2_t at = fixed3_xy(from->core.position);
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *victim = (mobj_t *)th;
            if (th->function != P_MobjThinker || victim == from || victim == previous ||
                victim->allegiance == ALLEGIANCE_NEUTRAL ||
                !P_CanTarget(attacker, victim) || !P_VisibleTo(attacker, victim)) continue;
            int64_t d = fixed2_distance_squared64(at, fixed3_xy(victim->core.position));
            if (d <= best) { best = d; next = victim; }
        }
        if (!next) return;
        strike(attacker, weapon, next, divisor);
        previous = from;
        from = next;
    }
}

bool P_Attack(mobj_t *attacker) {
    if (!attacker || !(attacker->traits & (MF_ATTACK | MF_HEAL | MF_REPAIR)) ||
        ((attacker->traits & (MF_HEAL | MF_REPAIR)) &&
         attacker->attack.cooldown_left_ms > 0)) return false;
    /* State actions must recheck range: the target can move during windup. */
    attacker->attack.target = attack_target_in_range(attacker);
    mobj_t *target = attacker->attack.target;
    if (!target) return false;
    attacker->attack.target = target;
    const char *sprite_name = "(unknown)";
    if (gameinfo && attacker->core.sprite_id >= 0 &&
        attacker->core.sprite_id < gameinfo->sprite_count &&
        gameinfo->sprnames && gameinfo->sprnames[attacker->core.sprite_id]) {
        sprite_name = gameinfo->sprnames[attacker->core.sprite_id];
    }
    debug_effects_log("shoot fire unit_type=%u state=%d facing_code=%d dir_slot=%d sprite=%s frame=%d target=%d",
                      attacker->type_id, attacker->core.state_id,
                      angle_to_direction(attacker->core.angle, 32, ANG90, true),
                      0, sprite_name, attacker->core.frame, target->id);

    const weapondef_t *weapon = P_MobjWeapon(attacker, target);
    if (gameinfo->attack && gameinfo->attack(attacker, weapon, target)) {
        S_ActorSound(attacker, SE_ATTACK);
        start_attack_cooldown(attacker, weapon);
        return true;
    }
    if (weapon->projectile_type != 0) {
#ifdef RTS_MODULE_DARK_COLONY
        mobj_t *missile = DC_FireMissiles(attacker, target, weapon->projectile_type);
#else
        mobj_t *missile = P_SpawnMissile(attacker, target, weapon->projectile_type);
#endif
        if (!missile) return false;
        S_ActorSound(attacker, SE_ATTACK);
        if (weapon->health_cost > 0) {
            attacker->hp -= weapon->health_cost;
            if (attacker->hp < 0) attacker->hp = 1;
        }
        start_attack_cooldown(attacker, weapon);
        debug_effects_log("missile launch source=%d type=%u target=%d speed=%d",
                          attacker->id, missile->type_id, target->id,
                          gameinfo->mobjinfo[missile->type_id].speed);
        return true;
    }

    S_ActorSound(attacker, SE_ATTACK);
    if (attacker->traits & (MF_HEAL | MF_REPAIR)) {
        int damage = weapon_hit(attacker, weapon, target, 1);
        if (damage < 0) {
            int amount = -damage;
            if (amount > target->max_hp - target->hp) amount = target->max_hp - target->hp;
            target->hp += amount;
        } else P_DamageMobj(target, attacker, damage);
    } else {
        fixed2_t at = fixed3_xy(target->core.position);
        bool flying = (target->traits & MF_FLY) != 0;
        strike(attacker, weapon, target, 1);
        splash(attacker, weapon, target, at, flying);
        bounce(attacker, weapon, target);
    }
    start_attack_cooldown(attacker, weapon);
    debug_effects_log("state attack attacker_type=%u target=%d damage=%d hp=%d/%d",
                      attacker->type_id, target->id, weapon_damage(attacker, weapon),
                      target->hp, target->max_hp);
    return true;
}

void A_Attack(mobj_t *unit) {
    (void)P_Attack(unit);
}

void A_Look(mobj_t *unit) {
#ifdef RTS_MODULE_WARCRAFT_2
    if (unit && (unit->w2.cast.spell || unit->w2.repair.target)) return;
#endif
    if (!unit || unit->hp <= 0 || !(unit->traits & (MF_ATTACK | MF_HEAL | MF_REPAIR)) ||
        unit->attack.cooldown_left_ms > 0 || !gameinfo ||
        unit->type_id >= gameinfo->mobj_type_count) return;
    mobj_t *target = attack_target_in_range(unit);
    if (!target) return;
    unit->attack.target = target;
    fixed2_t delta = fixed2_sub(fixed3_xy(target->core.position),
                                fixed3_xy(unit->core.position));
    angle_t desired = angle_from_map_vector_fixed(&level, delta.x, delta.y);
    if ((unit->traits & MF_TURRET) || unit->info->turn_step) {
        if (!turn_unit_toward(unit, desired, 1000 / RTS_TICRATE)) return;
    } else unit->core.angle = desired;
    int attack_state = gameinfo->mobjinfo[unit->type_id].missilestate;
    if (attack_state != gameinfo->null_state) P_SetMobjState(unit, attack_state);
    else if ((unit->traits & MF_FLY) && unit->info->attack.projectile_type)
        P_Attack(unit); /* Native bombers have no FIRE animation. */
}

void A_Chase(mobj_t *unit) {
    /* Movement runs in P_Ticker; state entry checks for an attack. */
    A_Look(unit);
}

static bool move_unit_if_walkable_fixed(mobj_t *unit, fixed2_t displacement) {
    if (!unit) return false;
    fixed3_t momentum = { displacement.x, displacement.y, 0 };
    fixed3_t candidate = fixed3_add_planar(unit->core.position, momentum);
    if (P_TryMove(unit, candidate)) return true;
    momentum.y = 0;
    candidate = fixed3_add_planar(unit->core.position, momentum);
    if (momentum.x != 0 && P_TryMove(unit, candidate)) return true;
    momentum = (fixed3_t){ 0, displacement.y, 0 };
    candidate = fixed3_add_planar(unit->core.position, momentum);
    if (momentum.y != 0 && P_TryMove(unit, candidate)) return true;
    unit->core.momentum = fixed3_zero();
    return false;
}

/* `along_out` is the closest-approach position along start..end, 16.16 in [0, 1]. */
static bool missile_hits_mobj(const mobj_t *missile, const mobj_t *candidate,
                              fixed2_t start, fixed2_t end, fixed_t *along_out) {
    if (!missile || !candidate || candidate == missile || candidate == missile->target ||
        candidate->remove || candidate->hp <= 0 ||
        (candidate->traits & (MF_NOBLOCKMAP | MF_MISSILE)) ||
        P_IsAlly(missile, candidate)) return false;

    fixed2_t target_position = fixed3_xy(candidate->core.position);
    fixed2_t path = fixed2_sub(end, start);
    int64_t path_length2 = fixed2_length_squared64(path); /* 32.32 */
    fixed_t along = 0;
    if (path_length2 > FIXED_LIT_64(0.0001)) {
        fixed2_t to_target = fixed2_sub(target_position, start);
        int64_t dot = (int64_t)to_target.x * path.x + (int64_t)to_target.y * path.y; /* 32.32 */
        int64_t ratio = dot / (path_length2 >> FIXED_FRAC_BITS);
        along = ratio < 0 ? 0 : ratio > FIXED_ONE ? FIXED_ONE : (fixed_t)ratio;
    }
    fixed2_t closest = fixed2_add(start, fixed2_scale(path, along));
    fixed_t radius = P_MobjRadius(missile) + P_MobjRadius(candidate);
    if (fixed2_distance_squared64(closest, target_position) > fixed_sq64(radius)) return false;
    if (along_out) *along_out = along;
    return true;
}

static mobj_t *missile_collision(const mobj_t *missile, fixed2_t start, fixed2_t end) {
    mobj_t *hit = NULL;
    fixed_t nearest = 2 * FIXED_ONE;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *candidate = (mobj_t *)th;
        fixed_t along = 0;
        if (missile_hits_mobj(missile, candidate, start, end, &along) && along < nearest) {
            nearest = along;
            hit = candidate;
        }
    }
    return hit;
}

static void tick_missile(mobj_t *missile) {
    if (!missile || missile->remove) return;

    const missiledef_t *flight = missile->info ? &missile->info->missile : NULL;
    if (flight && flight->period_ms > 0) {
        missile->missile.clock += 1000;
        while (missile->missile.clock >= flight->period_ms * RTS_TICRATE) {
            missile->missile.clock -= flight->period_ms * RTS_TICRATE;
            if (missile->missile.wait > 0) { missile->missile.wait--; continue; }
            missile->traits |= missile->info->traits & MF_RENDERABLE;
            fixed2_t start = fixed3_xy(missile->core.position);
            missile->core.position = fixed3_add(missile->core.position, missile->core.momentum);
            if (flight->weave && flight->weave_count > 0) {
                int index = (missile->missile.age * flight->weave_step + missile->missile.phase) % flight->weave_count;
                int amount = flight->weave[index];
                fixed3_t momentum = missile->core.momentum;
                fixed3_t offset = { momentum.y * amount / 100,
                                    -momentum.x * amount / 100,
                                    momentum.z * amount / 100 };
                missile->core.position = fixed3_add(missile->core.position, offset);
                if (flight->trail_type) {
                    mobj_t *trail = P_SpawnMobj(missile->core.position, flight->trail_type);
                    if (trail) { trail->owner = missile->owner; trail->team = missile->team; }
                }
            }
            int remaining = missile->missile.duration - missile->missile.age;
            if (flight->arc && remaining >= 0 && missile->missile.duration > 0) {
                int index = remaining * (flight->arc_count - 1) / missile->missile.duration;
                missile->core.position.z = missile->missile.duration * flight->arc[index];
#ifdef RTS_MODULE_DARK_COLONY
                /* Native 0x43ec31 truncates height to 8.8 before storing. */
                missile->core.position.z = missile->core.position.z / 256 * 256;
#endif
            }
            missile->missile.age++;
            if (flight->timed) {
                if (missile->missile.age >= missile->missile.duration) {
                    missile->core.position.z = 0;
                    P_ExplodeMissile(missile);
                    return;
                }
            } else {
                mobj_t *hit = missile_collision(missile, start,
                    fixed3_xy(missile->core.position));
                if (hit) {
                    int damage = missile_damage(missile, hit);
#ifdef RTS_MODULE_DARK_COLONY
                    damage = DC_DaylightDamage(missile->target,
                                               DC_DefendedDamage(hit, damage));
#endif
                    P_DamageMobj(hit, missile->target, damage);
                    P_ExplodeMissile(missile);
                    return;
                }
            }
            if (flight->lifetime > 0 && missile->missile.age > flight->lifetime) {
                P_RemoveMobj(missile);
                return;
            }
        }
        if (missile->missile.age) P_TickMobjState(missile);
        return;
    }

    fixed2_t start = fixed3_xy(missile->core.position);
    fixed2_t step = fixed3_xy(missile->core.momentum);
    fixed2_t end = fixed2_add(start, step);
    fixed_t distance = fixed2_length(step);
    if (distance > FIXED_LIT(0.0001)) {
        if (level.width > 0 && !P_CheckPosition(&level, missile, end)) {
            P_ExplodeMissile(missile);
            return;
        }
        mobj_t *hit = missile_collision(missile, start, end);
        if (hit) {
            int damage = gameinfo->mobjinfo[missile->type_id].damage;
            P_DamageMobj(hit, missile->target, damage);
            debug_effects_log("missile impact missile=%d target=%d damage=%d hp=%d/%d",
                              missile->id, hit->id, damage, hit->hp, hit->max_hp);
            P_ExplodeMissile(missile);
            return;
        }
        missile->core.position = fixed3_add_planar(missile->core.position,
                                                   missile->core.momentum);
    }
    P_TickMobjState(missile);
}

bool P_HasMoveOrder(const mobj_t *unit) {
    if (!unit || unit->movement.order_arrived) return false;
    return unit->movement.path.count || unit->movement.plan_pending ||
           ((unit->traits & MF_FLY) && unit->movement.order_id);
}

static bool final_goal_reaches_arrived_order_cluster(const mobj_t *unit,
                                                     fixed2_t target, fixed_t dist_to_goal) {
    if (unit->movement.order_id == 0) return false;
    fixed_t radius = P_MobjRadius(unit);

    /* A group settles as a blob: touching an arrived unit only ends the march once
     * we are inside the blob's footprint, so arrivals cannot chain into a queue. */
    int group = 1;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *member = (mobj_t *)th;
        group += member != unit && !member->remove && member->hp > 0 &&
                 member->movement.order_id == unit->movement.order_id;
    }
    /* Discs pack at roughly 75% density, so the blob radius is radius * sqrt(n / 0.75). */
    fixed_t ratio = (fixed_t)(((int64_t)group * 4 * FIXED_ONE) / 3);
    fixed_t footprint = FIXED_LIT(0.3) + fixed_mul32(radius, fixed_sqrt32(ratio));
    if (dist_to_goal > footprint) return false;

    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *other = (mobj_t *)th;
        if (other == unit) continue;
        if (other->remove || other->hp <= 0 || other->movement.order_id != unit->movement.order_id) {
            continue;
        }
        if (!other->movement.order_arrived) continue;

        fixed_t min_dist = radius + P_MobjRadius(other);
        fixed2_t other_position = { other->core.position.x, other->core.position.y };
        int64_t goal_dist2 = fixed2_length_squared64(fixed2_sub(other_position, target));
        int64_t unit_dist2 = fixed2_length_squared64(fixed2_sub(other_position,
                (fixed2_t){ unit->core.position.x, unit->core.position.y }));
        fixed_t contact_dist = min_dist + FIXED_LIT(0.20);
        if (goal_dist2 < (int64_t)min_dist * min_dist &&
            dist_to_goal <= contact_dist) {
            return true;
        }
        /* Queue behind arrived units that are nearer the goal; never stop for one behind us. */
        if (unit_dist2 <= (int64_t)contact_dist * contact_dist &&
            goal_dist2 <= (int64_t)dist_to_goal * dist_to_goal) {
            return true;
        }
    }
    return false;
}


static fixed_t unit_harvest_interaction_radius_cells(const mobj_t *unit) {
    fixed_t radius = P_MobjRadius(unit) + FIXED_LIT(0.55);
    if (radius < FIXED_LIT(0.75)) radius = FIXED_LIT(0.75);
    if (radius > FIXED_LIT(1.10)) radius = FIXED_LIT(1.10);
    return radius;
}

static bool update_unit_harvest(level_t *map,
                                mobj_t *unit, int dt_ms, const gameinfo_t *game_info) {
#ifdef RTS_MODULE_WARCRAFT_2
    return W2_TickRepair(unit) || W2_TickHarvest(unit) || W2_TickBuild(unit);
#endif
    if (!map || !unit || (unit->traits & MF_HARVESTER) == 0 ||
        unit->harvest.phase == HARVEST_PHASE_NONE || unit->harvest.target < 0) {
        return false;
    }
    if (unit->harvest.target >= map->resource_vent_count || !map->resource_vents) {
        unit->harvest.phase = HARVEST_PHASE_NONE;
        return false;
    }

    resourcevent_t *vent = &map->resource_vents[unit->harvest.target];
    bool animated_transfer = unit->info && unit->info->harvest.unload_state_id > 0;
    if (unit->harvest.phase == HARVEST_PHASE_TO_BASE ||
        unit->harvest.phase == HARVEST_PHASE_UNLOAD_TURNING ||
        unit->harvest.phase == HARVEST_PHASE_UNLOADING) {
        mobj_t *base = unit->harvest.base;
        fixed2_t bay = unit->harvest.return_position;
        if (!base || base->remove || base->hp <= 0 || !P_IsAlly(unit, base) ||
            (game_info->harvest_dropoff_matches &&
             (!game_info->harvest_dropoff_matches(unit, unit->harvest.resource_type, base, &bay) ||
              !fixed2_near(bay, unit->harvest.return_position, FIXED_LIT(0.001)) ||
              !P_CheckPosition(map, unit, bay)))) {
            P_ClearMove(unit);
            unit->movement.order_arrived = false;
            unit->harvest.base = NULL;
            unit->harvest.phase = HARVEST_PHASE_TO_BASE;
            P_SetMobjState(unit, game_info->mobjinfo[unit->type_id].spawnstate);
            send_harvester_home(map, unit);
            return true;
        }
        if (!unit->movement.order_arrived) {
            if (animated_transfer) yield_harvest_bay(map, unit);
            if (!P_HasMoveOrder(unit)) send_harvester_home(map, unit);
            return false;
        }
        if (!fixed2_near(fixed3_xy(unit->core.position),
                         unit->harvest.return_position, FIXED_LIT(0.001))) {
            send_harvester_home(map, unit);
            return false;
        }
        if (animated_transfer) {
            if (unit->harvest.phase == HARVEST_PHASE_TO_BASE)
                unit->harvest.phase = HARVEST_PHASE_UNLOAD_TURNING;
            if (unit->harvest.phase == HARVEST_PHASE_UNLOAD_TURNING) {
                if (turn_unit_toward(unit, unit->info->harvest.dock_angle, dt_ms)) {
                    unit->harvest.phase = HARVEST_PHASE_UNLOADING;
                    P_SetMobjState(unit, unit->info->harvest.unload_state_id);
                }
                return true;
            }
            const state_t *state = state_at(game_info, unit->core.state_id);
            if (state && state->group == 5) return true;
        }
        int owner = unit->owner < 8 ? unit->owner : 0;
        int rtype = unit->harvest.resource_type;
        int amount = unit->harvest.cargo;
        int unload = unit->info->harvest.resources[rtype].unload_amount;
        if (unload > 0 && amount > unload) amount = unload;
        map->player_resources[owner][rtype] += P_ScaleIncome(map, owner, amount);
        unit->harvest.cargo -= amount;
        if (unit->harvest.cargo > 0) {
            if (animated_transfer) P_SetMobjState(unit, unit->info->harvest.unload_state_id);
            return true;
        }
        if (!vent->active || vent->rate <= 0 || vent->amount <= 0 ||
            !P_VentOpenTo(map, vent, unit)) {
            unit->harvest.target = -1;
            unit->harvest.timer_ms = 0;
            unit->harvest.phase = HARVEST_PHASE_NONE;
            return false;
        }
        unit->harvest.resource_type = vent->resource_type;
        unit->harvest.phase = HARVEST_PHASE_TO_MINE;
        if (!send_harvester_to_vent(map, unit, vent)) return false;
        return false;
    }
    if (unit->harvest.phase == HARVEST_PHASE_TURNING) {
        fixed2_t att_delta = fixed2_sub(
            vent->attachment, fixed3_xy(unit->core.position));
        if (animated_transfer) {
            if (!turn_unit_toward(unit, unit->info->harvest.dock_angle, dt_ms)) return true;
        } else if (fixed2_length_squared64(att_delta) > FIXED_LIT_64(0.000001)) {
            angle_t desired = angle_from_map_vector_fixed(map, att_delta.x, att_delta.y);
            if (!turn_unit_toward(unit, desired, dt_ms)) return false;
            unit->core.angle = desired;
        }
        unit->movement.turn_timer_ms = 0;
        unit->harvest.base = NULL;
        unit->harvest.phase = HARVEST_PHASE_MINING;
        if (game_info && mobj_harvest_state(unit) > 0 &&
            mobj_harvest_state(unit) < game_info->state_count) {
            P_SetMobjState(unit, mobj_harvest_state(unit));
        }
        return false;
    }
    if (animated_transfer) {
        int capacity = mobj_harvest_capacity(unit);
        if (unit->harvest.cargo > 0 && (unit->harvest.cargo >= capacity ||
            unit->harvest.resource_type != vent->resource_type || !vent->active || vent->amount <= 0)) {
            unit->harvest.phase = HARVEST_PHASE_TO_BASE;
            send_harvester_home(map, unit);
            return true;
        }
        if (unit->harvest.phase == HARVEST_PHASE_MINING) {
            const state_t *state = state_at(game_info, unit->core.state_id);
            if (state && state->group == 5) return true;
            int take = unit->info->harvest.resources[unit->harvest.resource_type].load_amount;
            if (take > capacity - unit->harvest.cargo) take = capacity - unit->harvest.cargo;
            if (take > vent->amount) take = vent->amount;
            if (take < 0) take = 0;
            unit->harvest.cargo += take;
            vent->amount -= take;
            if (vent->amount <= 0) vent->active = false;
            if (unit->harvest.cargo >= capacity || !vent->active) {
                unit->harvest.phase = HARVEST_PHASE_TO_BASE;
                send_harvester_home(map, unit);
            } else P_SetMobjState(unit, mobj_harvest_state(unit));
            return true;
        }
        if (!P_HasMoveOrder(unit) && !unit->movement.order_arrived)
            send_harvester_to_vent(map, unit, vent);
        if (!unit->movement.order_arrived ||
            !fixed2_near(fixed3_xy(unit->core.position), vent->attachment, FIXED_LIT(0.001))) return false;
        unit->harvest.phase = HARVEST_PHASE_TURNING;
        return true;
    }
    if (!vent->active || vent->rate <= 0 || vent->amount <= 0 ||
        !P_VentOpenTo(map, vent, unit)) {
        if (mobj_harvest_capacity(unit) > 0 && unit->harvest.cargo > 0 &&
            send_harvester_home(map, unit))
            return false;
        unit->harvest.target = -1;
        unit->harvest.timer_ms = 0;
        unit->harvest.phase = HARVEST_PHASE_NONE;
        return false;
    }

    fixed2_t attachment_delta = fixed2_sub(
        vent->attachment, fixed3_xy(unit->core.position));
    fixed_t interaction_radius = unit_harvest_interaction_radius_cells(unit);
    fixed_t vent_radius = P_ResourceVentRadius(vent);
    if (vent_radius > interaction_radius) interaction_radius = vent_radius;
    if (P_HasMoveOrder(unit)) return false;
    if (vent->source_id && vent->footprint.w > 0 && vent->footprint.h > 0) {
        /* P_ApproachFootprint selects a cell on this same perimeter. */
        irect_t perimeter = {vent->cell.x - 1, vent->cell.y - 1,
                            vent->footprint.w + 2, vent->footprint.h + 2};
        if (!unit->movement.order_arrived ||
            !irect_contains(perimeter, fixed2_cell(fixed3_xy(unit->core.position)))) return false;
    } else if (fixed2_length_squared64(attachment_delta) > fixed_sq64(interaction_radius))
        return false;

    P_ClearMove(unit);
    unit->movement.order_arrived = true;
    unit->core.momentum = fixed3_zero();
    unit->attack.target = NULL;
    if (P_CheckPosition(map, unit, vent->attachment))
        unit->core.position = fixed3_from_fixed2(vent->attachment, unit->core.position.z);
    if (unit->harvest.phase != HARVEST_PHASE_MINING &&
        unit->harvest.phase != HARVEST_PHASE_TURNING) {
        unit->harvest.phase = HARVEST_PHASE_TURNING;
        unit->movement.turn_timer_ms = RTS_TURN_STEP_MS;
    }
    unit->harvest.timer_ms += dt_ms;
    while (unit->harvest.timer_ms >= RTS_HARVEST_INTERVAL_MS && vent->amount > 0) {
        unit->harvest.timer_ms -= RTS_HARVEST_INTERVAL_MS;
        int take = vent->rate;
        int capacity = mobj_harvest_capacity(unit);
        if (capacity > 0 && take > capacity - unit->harvest.cargo)
            take = capacity - unit->harvest.cargo;
        if (take > vent->amount) take = vent->amount;
        int owner = unit->owner < 8 ? unit->owner : 0;
        int rtype = vent->resource_type < RTS_MAX_RESOURCES ? vent->resource_type : 0;
        vent->amount -= take;
        if (vent->amount <= 0) {
            vent->active = false;
            if (vent->exhausts_source) P_RemoveMobj(P_MobjById(vent->source_id));
        }
        if (mobj_harvest_capacity(unit) > 0)
            unit->harvest.cargo += take;
        else
            map->player_resources[owner][rtype] += P_ScaleIncome(map, owner, take);
        if (mobj_harvest_capacity(unit) > 0 &&
            unit->harvest.cargo >= mobj_harvest_capacity(unit)) {
            if (send_harvester_home(map, unit)) break;
        }
        if (vent->amount <= 0) {
            vent->active = false;
            if (mobj_harvest_capacity(unit) > 0 && unit->harvest.cargo > 0) {
                send_harvester_home(map, unit);
            } else {
                unit->harvest.target = -1;
                unit->harvest.timer_ms = 0;
                unit->harvest.phase = HARVEST_PHASE_NONE;
                if (game_info && mobj_harvest_state(unit) > 0)
                    P_SetMobjState(unit, game_info->mobjinfo[unit->type_id].spawnstate);
            }
            break;
        }
    }
    return true;
}

static void tick_actor(mobj_t *u) {
    level_t *map = &level;
    const gameinfo_t *game_info = gameinfo;
    int dt_ms = RTS_TICK_MS;
    u->core.momentum = fixed3_zero();
    if (u->remove) return;
#ifdef RTS_MODULE_WARCRAFT_2
    W2_UpgradeUnit(u);
    W2_TickSpells(u);
    if (u->hp > 0 && W2_TickTransport(u)) return;
    if (u->hp > 0 && u->hp < u->max_hp && leveltime % (2 * RTS_TICRATE) == 0 &&
        (u->type_id == MT_AXETHROWER || u->type_id == MT_BERSERKER) &&
        W2_HasResearch(u->owner, W2_UPGRADE_REGENERATION)) ++u->hp;
#endif
    if (u->core.state_id <= 0) P_InitMobj(game_info, u);
    P_TickMobjState(u);
    if (u->remove || u->hp <= 0) return;
    if (game_info && game_info->mobj_ticker) {
        game_info->mobj_ticker(u);
        if (u->remove || u->hp <= 0) return;
    }
    P_TickWaypoints(u);

    if (u->attack.cooldown_left_ms > 0) {
        u->attack.cooldown_left_ms -= dt_ms;
        if (u->attack.cooldown_left_ms < 0) u->attack.cooldown_left_ms = 0;
    }
    const state_t *active_state = state_at(game_info, u->core.state_id);
    if (((u->traits & (MF_TURRET | MF_LANDMINE | MF_HEAL | MF_REPAIR)) ||
         ((u->traits & MF_FLY) && u->info && u->info->attack.projectile_type)) && active_state &&
        active_state->group != 3) A_Look(u);

    bool moving = P_HasMoveOrder(u);
    {
        const state_t *s = state_at(game_info, u->core.state_id);
        bool in_attack = s && s->group == 3;
        mobj_t *enemy = u->attack.target;
#ifdef RTS_MODULE_WARCRAFT_2
        if (u->w2.stand_ground || u->w2.cast.spell || u->w2.carrier) enemy = NULL;
#endif
        if (!in_attack && (u->traits & (MF_ATTACK | MF_MOBILE)) == (MF_ATTACK | MF_MOBILE) &&
            enemy && !enemy->remove && enemy->hp > 0 && !P_IsAlly(u, enemy) &&
            P_VisibleTo(u, enemy)) {
            fixed2_t goal = fixed3_xy(enemy->core.position);
            if (!within_attack_range(u, enemy, mobj_attack_range(u, enemy)) &&
                (!moving || !ivec2_equal(fixed2_cell(u->movement.goal), fixed2_cell(goal))))
                moving = P_MoveUnitTo(map, u, goal);
        }
        if (in_attack) moving = false;
        if (moving && !in_attack) {
            mobj_t *target = attack_target_in_range(u);
            if (target) {
                u->attack.target = target;
                fixed2_t target_delta = fixed2_sub(
                    fixed3_xy(target->core.position),
                    fixed3_xy(u->core.position));
                u->core.angle = angle_from_map_vector_fixed(map,
                                                 target_delta.x,
                                                 target_delta.y);
                P_ClearMove(u);
                u->movement.order_id = 0;
                u->movement.order_arrived = false;
                moving = false;
            }
        }
    }
    fixed2_t move_target = u->movement.goal;
    bool final = true;
    if (u->movement.plan_pending) moving = false; /* Waiting in the planning queue. */
    if (moving && !(u->traits & MF_FLY) && !P_SteerTarget(map, u, &move_target, &final)) {
        P_ClearMove(u);
        u->movement.order_arrived = false;
        moving = false;
    }
    if (moving) {
        /* Small bends are taken while moving; large ones turn in place first. */
        fixed2_t delta = fixed2_sub(move_target, fixed3_xy(u->core.position));
        if (fixed2_length_squared64(delta) >= FIXED_LIT_64(0.001 * 0.001)) {
            angle_t desired = angle_from_map_vector_fixed(map, delta.x, delta.y);
            if (angle_distance(desired, u->core.angle) > ANG45 / 2u &&
                !turn_unit_toward(u, desired, dt_ms)) moving = false;
        }
    }
    if (moving) {
        fixed2_t position = fixed3_xy(u->core.position);
        fixed2_t target = move_target;
        fixed2_t delta = fixed2_sub(target, position);
        fixed_t dist = fixed2_length(delta);
        if (final && !(u->traits & MF_FLY) &&
            final_goal_reaches_arrived_order_cluster(u, target, dist)) {
            u->movement.goal = fixed3_xy(u->core.position);
            P_ClearMove(u);
            u->movement.order_arrived = true;
            moving = false;
        } else {
            bool flying = u->traits & MF_FLY;
            fixed_t step = FIXED_STEP_PER_TIC(u->speed);
            if (!flying && P_MobjMoveClass(u)) { /* Terrain slows or speeds the class (swamp 25%, road 200%). */
                ivec2_t under = fixed2_cell(fixed3_xy(u->core.position));
                int percent = L_MoveSpeed(map, P_MobjMoveClass(u), under.x, under.y);
                step = (fixed_t)(((int64_t)step * (percent > 0 ? percent : 100)) / 100);
            }
            if (dist <= step || dist < FIXED_LIT(0.001)) {
                if (dist >= FIXED_LIT(0.001))
                    u->core.angle = angle_from_map_vector_fixed(map, delta.x, delta.y);
                if (move_unit_if_walkable_fixed(u, delta)) {
                    if (final) {
                        P_ClearMove(u);
                        u->movement.order_arrived = true;
                        moving = false;
                    }
                } else if (flying || !P_SteerProgress(map, u, false)) {
                    P_ClearMove(u);
                    u->movement.order_arrived = false;
                    moving = false;
                }
            } else {
                fixed2_t direction = { fixed_div32(delta.x, dist), fixed_div32(delta.y, dist) };
                if (!flying) direction = P_SteerAvoid(u, direction, step);
#ifdef RTS_MODULE_WARCRAFT_2
                direction = snap_move_direction_45(direction);
#endif
                u->core.angle = angle_from_map_vector_fixed(map, direction.x, direction.y);
                fixed3_t before = u->core.position;
                bool moved = move_unit_if_walkable_fixed(u, fixed2_scale(direction, step));
                if (moved && !flying) {
                    fixed3_t travelled = fixed3_planar_displacement(before, u->core.position);
                    fixed_t made = fixed_hypot32(travelled.x, travelled.y);
                    moved = made > fixed_mul32(step, FIXED_LIT(0.1)); /* Sliding along a wall is not progress. */
                }
                if (flying ? !moved : !P_SteerProgress(map, u, moved)) {
                    /* Given up beside a crowded goal: settle there. */
                    bool settled = !flying && dist <= FIXED_LIT(1.5) && u->movement.order_id == 0;
                    P_ClearMove(u);
                    u->movement.order_arrived = settled;
                    moving = false;
                }
            }
        }
    }

    if (update_unit_harvest(map, u, dt_ms, game_info)) {
        moving = false;
        u->core.momentum = fixed3_zero();
    }

    if (game_info && (u->traits & MF_MOBILE) && u->type_id > 0 &&
        u->type_id < game_info->mobj_type_count) {
        const mobjinfo_t *mi = &game_info->mobjinfo[u->type_id];
        const state_t *state = state_at(game_info, u->core.state_id);
        int group = state ? state->group : 0;
        if (group != 3) {
            if (moving && group != 2) {
                P_SetMobjState(u, mi->seestate);
            } else if (!moving && group == 2 &&
                       !((u->traits & MF_FLY) && P_HasMoveOrder(u))) {
                P_SetMobjState(u, mi->spawnstate);
            } else {
                apply_state_visuals(game_info, &u->core,
                                    state_at(game_info, u->core.state_id), false);
            }
        }
    }
    if ((!gameinfo || !gameinfo->states) && u->attack.cooldown_left_ms <= 0)
        P_Attack(u);
#ifdef RTS_MODULE_WARCRAFT_2
    W2_WorkerPose(u);
#endif
}

void P_MobjThinker(mobj_t *mobj) {
    if (mobj->remove) { P_RemoveMobj(mobj); return; }
#ifdef RTS_MODULE_WARCRAFT_2
    if (mobj->type_id == MT_W2_EFFECT) { P_TickMobjState(mobj); return; }
#endif
    if (mobj->traits & MF_MISSILE) {
        tick_missile(mobj);
        return;
    }
    tick_actor(mobj);
}
