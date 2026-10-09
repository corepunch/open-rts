#include "engine.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

/* Wargus spells.lua. All orders use the normal actor attack animation's
 * release frame (the reference SpellCast and Attack rows use the same waits). */
const w2_spell_t w2_spells[W2_SPELL_COUNT] = {
    [W2_SPELL_VISION] = {"Holy vision", 70, 0, 106, 0, false, false},
    [W2_SPELL_HEAL] = {"Healing", 6, 6, 107, W2_UPGRADE_HEALING, true, false},
    [W2_SPELL_EXORCISM] = {"Exorcism", 4, 10, 110, W2_UPGRADE_EXORCISM, true, false},
    [W2_SPELL_EYE] = {"Eye of Kilrogg", 70, 6, 111, 0, false, false},
    [W2_SPELL_BLOODLUST] = {"Bloodlust", 50, 6, 112, W2_UPGRADE_BLOODLUST, true, false},
    [W2_SPELL_RUNES] = {"Runes", 200, 10, 97, W2_UPGRADE_RUNES, false, false},
    [W2_SPELL_FIREBALL] = {"Fireball", 100, 8, 101, 0, false, false},
    [W2_SPELL_SLOW] = {"Slow", 50, 10, 94, W2_UPGRADE_SLOW, true, false},
    [W2_SPELL_FLAME_SHIELD] = {"Flame shield", 50, 6, 100, W2_UPGRADE_FLAME_SHIELD, true, false},
    [W2_SPELL_INVISIBILITY] = {"Invisibility", 200, 6, 95, W2_UPGRADE_INVISIBILITY, true, false},
    [W2_SPELL_POLYMORPH] = {"Polymorph", 200, 10, 115, W2_UPGRADE_POLYMORPH, true, false},
    [W2_SPELL_BLIZZARD] = {"Blizzard", 25, 12, 105, W2_UPGRADE_BLIZZARD, false, true},
    [W2_SPELL_DEATH_COIL] = {"Death coil", 100, 10, 103, 0, false, false},
    [W2_SPELL_HASTE] = {"Haste", 50, 6, 96, W2_UPGRADE_HASTE, true, false},
    [W2_SPELL_RAISE_DEAD] = {"Raise dead", 50, 6, 114, W2_UPGRADE_RAISE_DEAD, false, true},
    [W2_SPELL_WHIRLWIND] = {"Whirlwind", 100, 12, 104, W2_UPGRADE_WHIRLWIND, false, false},
    [W2_SPELL_UNHOLY_ARMOR] = {"Unholy armor", 100, 6, 98, W2_UPGRADE_UNHOLY_ARMOR, true, false},
    [W2_SPELL_DECAY] = {"Death and decay", 25, 12, 108, W2_UPGRADE_DEATH_AND_DECAY, false, true},
    [W2_SPELL_DEMOLISH] = {"Demolish", 0, 1, 113, 0, false, false},
};

bool W2_CanCast(const mobj_t *unit, w2_spell_id_t spell) {
    if (!unit || unit->hp <= 0 || unit->remove || spell <= 0 || spell >= W2_SPELL_COUNT) return false;
    const w2_spell_t *def = &w2_spells[spell];
    const w2_stats_t *stats = &mobjinfo[unit->type_id].w2;
    for (int i = 0; i < 6; ++i) {
        if (stats->spells[i] == spell)
            return stats->innate_spells || !def->research || W2_HasResearch(unit->owner, def->research);
    }
    return false;
}

static bool valid_target(const mobj_t *unit, int spell, const mobj_t *target) {
    if (!w2_spells[spell].unit_target) return true;
    if (!target || target->remove || target->hp <= 0 || target->type_id > W2_TYPE_COUNT ||
        (target->traits & MF_NOBLOCKMAP) || !P_VisibleTo(unit, target)) return false;
    const w2_stats_t *stats = &mobjinfo[target->type_id].w2;
    if (stats->flags & W2_STRUCTURE) return false;
    switch (spell) {
    case W2_SPELL_HEAL: return (stats->attributes & W2_ORGANIC) && target->hp < target->max_hp;
    case W2_SPELL_EXORCISM: return stats->attributes & W2_UNDEAD;
    case W2_SPELL_POLYMORPH: return stats->attributes & W2_ORGANIC;
    case W2_SPELL_BLOODLUST: return (stats->attributes & W2_ORGANIC) && !target->w2.buffs[W2_BUFF_BLOODLUST];
    case W2_SPELL_HASTE: return !target->w2.buffs[W2_BUFF_HASTE];
    case W2_SPELL_SLOW: return !target->w2.buffs[W2_BUFF_SLOW];
    case W2_SPELL_INVISIBILITY: return target->w2.buffs[W2_BUFF_INVISIBLE] <= 10;
    case W2_SPELL_UNHOLY_ARMOR: return target->w2.buffs[W2_BUFF_ARMOR] <= 10;
    case W2_SPELL_FLAME_SHIELD: return !(stats->flags & W2_AIR);
    default: return true;
    }
}

bool W2_CastOrder(mobj_t *unit, w2_spell_id_t spell, mobj_t *target, fixed3_t position) {
    if (!W2_CanCast(unit, spell) || unit->w2.mana < w2_spells[spell].mana ||
        !valid_target(unit, spell, target)) return false;
    if (target) position = target->core.position;
    if (!L_Contains(&level, position.x >> FIXED_FRAC_BITS, position.y >> FIXED_FRAC_BITS)) return false;
    W2_InterruptRepair(unit);
    unit->w2.carrier = 0;
    P_ClearMove(unit);
    unit->attack.target = NULL;
    unit->waypoints = (waypoints_t){0};
    unit->w2.stand_ground = false;
    unit->w2.cast.spell = spell;
    unit->w2.cast.target = target ? target->id : 0;
    unit->w2.cast.position = position;
    P_SetMobjState(unit, mobjinfo[unit->type_id].spawnstate);
    return true;
}

static bool in_range(const mobj_t *unit) {
    int range = w2_spells[unit->w2.cast.spell].range;
    if (!range) return true;
    const mobj_t *target = P_MobjById(unit->w2.cast.target);
    if (target) return W2_Distance(unit, target) <= range;
    ivec2_t from = fvec2_cell(fixed3_xy_to_fvec2(unit->core.position));
    ivec2_t to = fvec2_cell(fixed3_xy_to_fvec2(unit->w2.cast.position));
    return abs(from.x - to.x) <= range && abs(from.y - to.y) <= range;
}

static mobj_t *summon(mobj_t *unit, int type, fixed3_t at, int ttl) {
    mobj_t *spawn = P_SpawnMobj(at, type);
    if (!spawn) return NULL;
    spawn->owner = unit->owner; spawn->team = unit->team; spawn->allegiance = unit->allegiance;
    spawn->w2.ttl = ttl;
    W2_EnsureUnitSprite(type - 1);
    return spawn;
}

static mobj_t *spell_effect(mobj_t *unit, int kind, fixed3_t at, int damage, int ttl) {
    mobj_t *effect = w2_spawn_effect(unit, kind, at);
    if (effect) {
        effect->missile.damage = damage;
        effect->w2.ttl = ttl;
        effect->w2.cast.spell = unit->w2.cast.spell;
        effect->w2.fx.mask = W2_TARGET_LAND | W2_TARGET_SEA | W2_TARGET_AIR;
    }
    return effect;
}

void A_W2_Cast(mobj_t *unit) {
    int spell = unit->w2.cast.spell;
    mobj_t *target = P_MobjById(unit->w2.cast.target);
    if (!W2_CanCast(unit, spell) || unit->w2.mana < w2_spells[spell].mana ||
        !valid_target(unit, spell, target) || !in_range(unit)) { unit->w2.cast.spell = 0; return; }
    fixed3_t at = target ? target->core.position : unit->w2.cast.position;
    const w2_spell_t *def = &w2_spells[spell];
    int cost = def->mana;
    bool success = true;
    unit->w2.buffs[W2_BUFF_INVISIBLE] = 0;
    switch (spell) {
    case W2_SPELL_HEAL: case W2_SPELL_EXORCISM: {
        int amount = spell == W2_SPELL_HEAL ? target->max_hp - target->hp : target->hp;
        if (amount > unit->w2.mana / cost) amount = unit->w2.mana / cost;
        cost *= amount;
        if (spell == W2_SPELL_HEAL) target->hp += amount;
        else P_DamageMobj(target, unit, amount);
        spell_effect(unit, spell == W2_SPELL_HEAL ? W2_FX_HEAL : W2_FX_EXORCISM, at, 0, 0);
        break;
    }
    case W2_SPELL_HASTE: target->w2.buffs[W2_BUFF_HASTE] = 1000; target->w2.buffs[W2_BUFF_SLOW] = 0; break;
    case W2_SPELL_SLOW: target->w2.buffs[W2_BUFF_SLOW] = 1000; target->w2.buffs[W2_BUFF_HASTE] = 0; break;
    case W2_SPELL_BLOODLUST: target->w2.buffs[W2_BUFF_BLOODLUST] = 1000; break;
    case W2_SPELL_INVISIBILITY: target->w2.buffs[W2_BUFF_INVISIBLE] = 2000; break;
    case W2_SPELL_UNHOLY_ARMOR:
        P_DamageMobj(target, NULL, mobjinfo[target->type_id].w2.attributes & W2_VOLATILE ? target->hp :
                                     target->hp > 1 ? target->hp / 2 : 1);
        if (target->hp > 0) target->w2.buffs[W2_BUFF_ARMOR] = 500;
        break;
    case W2_SPELL_POLYMORPH:
        success = W2_TransformUnit(target, MT_CRITTER);
        if (success) {
            target->owner = target->team = 15; target->allegiance = ALLEGIANCE_NEUTRAL;
            target->w2.cast.spell = 0; target->attack.target = NULL;
            memset(target->w2.buffs, 0, sizeof(target->w2.buffs));
            target->waypoints = (waypoints_t){0};
            P_ClearMove(target);
        }
        break;
    case W2_SPELL_VISION: {
        mobj_t *effect = spell_effect(unit, W2_FX_SPELL, at, 0, 25);
        success = effect != NULL;
        break;
    }
    case W2_SPELL_EYE: success = summon(unit, MT_EYE_OF_KILROGG, at, 765) != NULL; break;
    case W2_SPELL_RAISE_DEAD: {
        success = false;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *corpse = (mobj_t *)th;
            if (th->function != P_MobjThinker || corpse->remove || corpse->hp > 0 ||
                corpse->type_id > W2_TYPE_COUNT || (mobjinfo[corpse->type_id].w2.flags & W2_STRUCTURE) ||
                W2_Distance(unit, corpse) > def->range) continue;
            ivec2_t a = fvec2_cell(fixed3_xy_to_fvec2(corpse->core.position));
            ivec2_t b = fvec2_cell(fixed3_xy_to_fvec2(at));
            if (abs(a.x - b.x) > 1 || abs(a.y - b.y) > 1) continue;
            if (summon(unit, MT_SKELETON, corpse->core.position, 3600)) {
                P_RemoveMobj(corpse);
                success = true;
            }
            break;
        }
        break;
    }
    case W2_SPELL_FIREBALL: {
        mobj_t *shot = spell_effect(unit, W2_FX_FIREBALL, unit->core.position, 20, 0);
        success = shot != NULL;
        if (shot) {
            shot->w2.fx.end = at;
            shot->w2.fx.subject = target ? target->id : 0;
            fvec2_t delta = fvec2_sub(fixed3_xy_to_fvec2(at), fixed3_xy_to_fvec2(unit->core.position));
            shot->core.angle = P_PointToAngle(delta.x, delta.y);
        }
        break;
    }
    case W2_SPELL_DEATH_COIL: {
        /* Stratagus splits 50 damage over organic enemies in a 5x5 area,
         * nearest the caster first; the last target receives the remainder. */
        mobjlist_t list = P_ListMobjs();
        int count = 0;
        ivec2_t goal = fvec2_cell(fixed3_xy_to_fvec2(at));
        for (int i = 0; i < list.count; ++i) {
            mobj_t *victim = list.items[i];
            ivec2_t cell = fvec2_cell(fixed3_xy_to_fvec2(victim->core.position));
            if (victim->hp <= 0 || victim->remove || victim->type_id > W2_TYPE_COUNT ||
                (victim->traits & MF_NOBLOCKMAP) || P_IsAlly(unit, victim) ||
                victim->allegiance == ALLEGIANCE_NEUTRAL ||
                !(mobjinfo[victim->type_id].w2.attributes & W2_ORGANIC) ||
                abs(cell.x - goal.x) > 2 || abs(cell.y - goal.y) > 2) continue;
            list.items[count++] = victim;
        }
        success = count > 0;
        int left = 50;
        for (int i = 0; i < count && left > 0; ++i) {
            int nearest = i;
            for (int j = i + 1; j < count; ++j)
                if (W2_Distance(unit, list.items[j]) < W2_Distance(unit, list.items[nearest])) nearest = j;
            mobj_t *victim = list.items[nearest];
            list.items[nearest] = list.items[i];
            int damage = i + 1 == count || victim->hp > left ? left : victim->hp;
            mobj_t *shot = spell_effect(unit, W2_FX_TOUCH, unit->core.position, damage, 0);
            if (shot) {
                shot->w2.fx.subject = victim->id;
                shot->w2.fx.end = victim->core.position;
                fvec2_t delta = fvec2_sub(fixed3_xy_to_fvec2(victim->core.position), fixed3_xy_to_fvec2(unit->core.position));
                shot->core.angle = P_PointToAngle(delta.x, delta.y);
            }
            left -= damage;
        }
        P_FreeMobjList(&list);
        break;
    }
    case W2_SPELL_FLAME_SHIELD:
        for (int i = 0; i < 5; ++i) {
            mobj_t *effect = spell_effect(unit, W2_FX_FLAME_SHIELD, at, 1, 600 + i * 7);
            if (effect) effect->w2.fx.subject = target->id;
        }
        break;
    case W2_SPELL_RUNES: {
        static const ivec2_t offsets[] = {{0, 0}, {1, 0}, {0, 1}, {-1, 0}, {0, -1}};
        for (int i = 0; i < 5; ++i) {
            fixed3_t pos = fixed3_add_planar(at, fixed3_planar_delta((fvec2_t){offsets[i].x, offsets[i].y}));
            if (L_Contains(&level, pos.x >> FIXED_FRAC_BITS, pos.y >> FIXED_FRAC_BITS))
                spell_effect(unit, W2_FX_RUNE, pos, 50, 2000);
        }
        break;
    }
    case W2_SPELL_BLIZZARD: case W2_SPELL_DECAY:
        for (int field = 0; field < 5; ++field) {
            ivec2_t cell = fvec2_cell(fixed3_xy_to_fvec2(at)), dest;
            do { dest = (ivec2_t){cell.x + (int)(W2_SyncRand() % 5) - 2, cell.y + (int)(W2_SyncRand() % 5) - 2}; }
            while (!L_Contains(&level, dest.x, dest.y));
            fixed3_t end = fixed3_from_fvec2(fvec2_cell_center(dest), 0);
            for (int shard = 0; shard < 11; ++shard) {
                fixed3_t start = spell == W2_SPELL_BLIZZARD ?
                    fixed3_add_planar(end, fixed3_planar_delta((fvec2_t){-4, -4})) : end;
                mobj_t *effect = spell_effect(unit, spell == W2_SPELL_BLIZZARD ? W2_FX_BLIZZARD : W2_FX_DECAY, start, 0, 0);
                if (effect) {
                    effect->w2.fx.end = end;
                    effect->missile.wait = shard * (spell == W2_SPELL_BLIZZARD ? 16 : 8);
                    if (effect->missile.wait) effect->traits &= ~MF_RENDERABLE;
                }
            }
        }
        break;
    case W2_SPELL_WHIRLWIND: spell_effect(unit, W2_FX_WHIRLWIND, at, 3, 800); break;
    case W2_SPELL_DEMOLISH:
        spell_effect(unit, W2_FX_EXPLOSION, unit->core.position, 0, 0);
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *victim = (mobj_t *)th;
            if (th->function == P_MobjThinker && victim->type_id <= W2_TYPE_COUNT &&
                !(victim->traits & MF_FLY) && W2_Distance(unit, victim) <= 3) P_DamageMobj(victim, unit, 400);
        }
        for (int i = 0; i < level.resource_vent_count; ++i) {
            resourcevent_t *vent = &level.resource_vents[i];
            ivec2_t cell = fvec2_cell(fixed3_xy_to_fvec2(unit->core.position));
            int dx = cell.x - vent->cell.x, dy = cell.y - vent->cell.y;
            if (vent->resource_type == 1 && vent->active && dx * dx + dy * dy <= 9) {
                vent->amount = 0; vent->active = false;
                w2_remove_tree(vent->cell);
            }
        }
        break;
    default: success = false; break;
    }
    if (success) {
        if (w2_spell_sounds[spell]) S_StartSound(unit, w2_spell_sounds[spell]);
        unit->w2.mana -= cost;
        if (def->unit_target && spell != W2_SPELL_HEAL && spell != W2_SPELL_EXORCISM)
            spell_effect(unit, W2_FX_SPELL, at, 0, 0);
    }
    if (!success || !def->repeat || unit->w2.mana < cost) unit->w2.cast.spell = 0;
}

void W2_TickSpells(mobj_t *unit) {
    if (unit->hp <= 0) return;
    bool speed_changed = unit->w2.buffs[W2_BUFF_HASTE] || unit->w2.buffs[W2_BUFF_SLOW];
    for (int i = 0; i < W2_BUFF_COUNT; ++i) if (unit->w2.buffs[i]) --unit->w2.buffs[i];
    if (unit->w2.ttl && --unit->w2.ttl == 0) {
        unit->w2.buffs[W2_BUFF_ARMOR] = 0;
        P_DamageMobj(unit, NULL, unit->hp); return;
    }
    const w2_stats_t *stats = &mobjinfo[unit->type_id].w2;
    if (leveltime % RTS_TICRATE == 0 && unit->w2.mana < stats->mana.max) {
        unit->w2.mana += stats->mana.increase;
        if (unit->w2.mana > stats->mana.max) unit->w2.mana = stats->mana.max;
    }
    if (speed_changed)
        unit->speed = unit->info->speed * (unit->w2.buffs[W2_BUFF_HASTE] ? 2 : unit->w2.buffs[W2_BUFF_SLOW] ? 0.5f : 1);
    if (!unit->w2.cast.spell || states[unit->core.state_id].group == W2_GROUP_ATTACK) return;
    mobj_t *target = P_MobjById(unit->w2.cast.target);
    if (!W2_CanCast(unit, unit->w2.cast.spell) ||
        unit->w2.mana < w2_spells[unit->w2.cast.spell].mana || !valid_target(unit, unit->w2.cast.spell, target)) {
        unit->w2.cast.spell = 0; P_ClearMove(unit); return;
    }
    if (target) unit->w2.cast.position = target->core.position;
    if (in_range(unit)) {
        P_ClearMove(unit);
        fvec2_t delta = fvec2_sub(fixed3_xy_to_fvec2(unit->w2.cast.position), fixed3_xy_to_fvec2(unit->core.position));
        unit->core.angle = P_PointToAngle(delta.x, delta.y);
        P_SetMobjState(unit, mobjinfo[unit->type_id].missilestate);
    } else if (!P_HasMoveOrder(unit) && !P_MoveUnitTo(&level, unit, fixed3_xy_to_fvec2(unit->w2.cast.position))) {
        unit->w2.cast.spell = 0;
    }
}

bool W2_VisibleTo(const mobj_t *unit, int owner) {
    if (!unit || owner < 0 || owner >= 8) return false;
    if (unit->owner == owner || (unit->team < 8 &&
        (level.sight.allies[owner] & (UINT32_C(0x40000000) >> unit->team)))) return true;
    if (unit->w2.buffs[W2_BUFF_INVISIBLE]) return false;
    if (!(mobjinfo[unit->type_id].w2.attributes & W2_PERMANENT_CLOAK)) return true;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *detector = (const mobj_t *)th;
        if (th->function != P_MobjThinker || detector->remove || detector->hp <= 0 ||
            (detector->owner != owner && (detector->team >= 8 ||
             !(level.sight.allies[owner] & (UINT32_C(0x40000000) >> detector->team))))) continue;
        if (((mobjinfo[detector->type_id].w2.attributes & W2_DETECT_CLOAK) ||
             (detector->type_id == MT_W2_EFFECT && detector->w2.cast.spell == W2_SPELL_VISION)) &&
            W2_Distance(detector, unit) <= W2_SightRange(detector)) return true;
    }
    return false;
}
