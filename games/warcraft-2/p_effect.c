#include "engine.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

/* Wargus missiles.lua, native MAINDAT GRPs 324..351. Directional GRPs
 * contain five stored facings per temporal frame; the loader supplies mirrors.
 * Speeds are pixels per 30 Hz cycle, not unit movement speeds. */
const w2_effect_t w2_effects[W2_FX_COUNT] = {
    {"missile-lightning", 6, 1, 16, 1, 1, -1, 0, true},
    {"missile-griffon-hammer", 3, 1, 16, 2, 2, W2_FX_EXPLOSION, 3, true},
    {"missile-dragon-breath", 1, 1, 16, 2, 2, W2_FX_EXPLOSION, 3, true},
    {"missile-fireball", 1, 1, 16, 2, 2, W2_FX_EXPLOSION, 5, true},
    {"missile-flame-shield", 6, 1, 0, 1, 1, -1, 0, false},
    {"missile-blizzard", 4, 1, 16, 1, 1, -1, 0, false},
    {"missile-death-and-decay", 8, 1, 0, 1, 1, -1, 0, false},
    {"missile-big-cannon", 4, 1, 16, 2, 4, W2_FX_TOWER_EXPLOSION, 0, true},
    {"missile-exorcism", 6, 1, 0, 1, 1, -1, 0, false},
    {"missile-heal-effect", 6, 1, 0, 1, 1, -1, 0, false},
    {"missile-touch-of-death", 6, 1, 16, 1, 1, -1, 0, true},
    {"missile-rune", 4, 5, 0, 1, 1, W2_FX_EXPLOSION, 0, false},
    {"missile-whirlwind", 4, 1, 0, 2, 1, -1, 0, false},
    {"missile-catapult-rock", 3, 1, 8, 2, 4, W2_FX_IMPACT, 0, true},
    {"missile-ballista-bolt", 1, 1, 8, 2, 4, W2_FX_IMPACT, 0, true},
    {"missile-arrow", 1, 1, 32, 0, 1, -1, 0, true},
    {"missile-axe", 3, 1, 32, 0, 1, -1, 0, true},
    {"missile-submarine-missile", 1, 1, 16, 1, 1, W2_FX_IMPACT, 0, true},
    {"missile-turtle-missile", 1, 1, 16, 1, 1, W2_FX_IMPACT, 0, true},
    {"missile-small-fire", 6, 2, 0, 0, 1, -1, 0, false},
    {"missile-big-fire", 10, 2, 0, 0, 1, -1, 0, false},
    {"missile-impact", 6, 1, 0, 0, 1, -1, 0, false},
    {"missile-normal-spell", 6, 5, 0, 0, 1, -1, 0, false},
    {"missile-explosion", 16, 1, 0, 0, 1, -1, 0, false},
    {"missile-small-cannon", 3, 1, 22, 2, 3, W2_FX_TOWER_EXPLOSION, 0, true},
    {"missile-cannon-explosion", 4, 2, 0, 0, 1, -1, 0, false},
    {"missile-cannon-tower-explosion", 4, 2, 0, 0, 1, -1, 0, false},
    {"missile-daemon-fire", 3, 1, 16, 1, 1, -1, 0, true},
};

static void pose(mobj_t *effect) {
    int kind = effect->w2.fx.kind;
    const w2_effect_t *def = &w2_effects[kind];
    effect->core.sprite_id = W2_EFFECT_SPRITE + kind;
    bool with_hit = kind == W2_FX_LIGHTNING || kind == W2_FX_TOUCH || kind == W2_FX_BLIZZARD;
    effect->core.frame = with_hit ? effect->w2.fx.clock % def->frames :
        (effect->w2.fx.age / def->sleep) % def->frames;
    snprintf(effect->core.sprite_name, sizeof(effect->core.sprite_name), "%s", def->name);
}

mobj_t *w2_spawn_effect(mobj_t *source, int kind, fixed3_t position) {
    if (kind < 0 || kind >= W2_FX_COUNT) return NULL;
    mobj_t *effect = P_SpawnMobj(position, MT_W2_EFFECT);
    if (!effect) return NULL;
    if (source) {
        effect->target = source;
        effect->owner = source->owner;
        effect->team = source->team;
        effect->allegiance = source->allegiance;
    }
    effect->w2.fx.kind = kind;
    effect->w2.fx.end = position;
    effect->w2.fx.bounces = w2_effects[kind].bounces;
    pose(effect);
    return effect;
}

static int fire_kind(const mobj_t *unit) {
    if (!unit || unit->remove || unit->hp <= 0 || unit->max_hp <= 0) return -1;
    int percent = (int)((int64_t)unit->hp * 100 / unit->max_hp);
    return percent >= 75 ? -1 : percent >= 50 ? W2_FX_SMALL_FIRE : W2_FX_BIG_FIRE;
}

void W2_Burning(mobj_t *unit) {
    if (!(mobjinfo[unit->type_id].w2.flags & W2_STRUCTURE) ||
        W2_UnderConstruction(unit) || fire_kind(unit) < 0 || P_MobjById(unit->w2.fire)) return;
    /* Stratagus HitUnit_Burning: centered one tile above the building.
     * World height gives that exact projection while retaining sort order. */
    fixed3_t position = fixed3_add(unit->core.position, (fixed3_t){0, 0, FIXED_ONE});
    mobj_t *fire = w2_spawn_effect(unit, fire_kind(unit), position);
    if (fire) { fire->w2.fx.subject = unit->id; unit->w2.fire = fire->id; }
}

bool w2_fire_projectile(mobj_t *source, mobj_t *target) {
    const w2_stats_t *stats = &mobjinfo[source->type_id].w2;
    if (stats->projectile == W2_FX_NONE) return false;
    mobj_t *shot = w2_spawn_effect(source, stats->projectile, source->core.position);
    if (!shot) return true; /* An unavailable effect must not turn into a hitscan. */
    shot->w2.fx.subject = target->id;
    shot->w2.fx.end = target->core.position;
    shot->w2.fx.basic = stats->basic_damage;
    shot->w2.fx.piercing = W2_PiercingDamage(source);
    if (source->w2.buffs[W2_BUFF_BLOODLUST]) {
        shot->w2.fx.basic *= 2;
        shot->w2.fx.piercing *= 2;
    }
    shot->w2.fx.mask = stats->target_mask;
    fixed2_t delta = fixed2_sub(fixed3_xy(target->core.position),
                                fixed3_xy(source->core.position));
    shot->core.angle = P_PointToAngle(delta.x, delta.y);
    return true;
}

static void hit(mobj_t *shot, mobj_t *victim, int divisor) {
    if (!victim || victim->remove || victim->hp <= 0 ||
        (victim == shot->target && shot->w2.fx.kind != W2_FX_RUNE) ||
        (victim->traits & MF_NOBLOCKMAP) ||
        (mobjinfo[victim->type_id].w2.attributes & W2_INDESTRUCTIBLE)) return;
    int damage;
    if (shot->w2.cast.spell == W2_SPELL_BLIZZARD || shot->w2.cast.spell == W2_SPELL_DECAY)
        damage = W2_SyncRand() % 10;
    else if (shot->missile.damage) damage = shot->missile.damage;
    else if (shot->target && !shot->target->remove && shot->target->hp > 0)
        damage = W2_AttackDamage(shot->target, victim, W2_SyncRand());
    else {
        damage = shot->w2.fx.basic - W2_Armor(victim);
        if (damage < 1) damage = 1;
        damage += shot->w2.fx.piercing;
        damage -= W2_SyncRand() % ((damage + 2) / 2);
    }
    P_DamageMobj(victim, shot->target, damage / divisor);
}

static void impact(mobj_t *shot) {
    const w2_effect_t *def = &w2_effects[shot->w2.fx.kind];
    int kind = shot->w2.fx.kind;
    int sound = kind == W2_FX_ARROW ? 67 : kind == W2_FX_HAMMER || kind == W2_FX_DRAGON ||
        kind == W2_FX_FIREBALL || kind == W2_FX_BLIZZARD ? 64 :
        kind == W2_FX_ROCK || kind == W2_FX_BOLT || kind == W2_FX_CANNON || kind == W2_FX_BIG_CANNON ? 31 : 0;
    if (sound) S_StartSoundAt(fixed3_xy(shot->core.position), sound);
    if (!def->range) hit(shot, P_MobjById(shot->w2.fx.subject), 1);
    else for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *victim = (mobj_t *)th;
        if (victim->remove || victim->hp <= 0 || victim->type_id == MT_W2_EFFECT) continue;
        const w2_stats_t *stats = &mobjinfo[victim->type_id].w2;
        if (!(shot->w2.fx.mask & (1 << stats->domain))) continue;
        int distance = W2_Distance(shot, victim);
        if (distance < def->range) hit(shot, victim, distance ? distance * def->splash : 1);
    }
    if (def->impact >= 0) w2_spawn_effect(shot->target, def->impact, shot->core.position);
}

void A_W2_Effect(mobj_t *effect) {
    int kind = effect->w2.fx.kind;
    if (kind < 0 || kind >= W2_FX_COUNT) { P_RemoveMobj(effect); return; }
    const w2_effect_t *def = &w2_effects[kind];
    if (effect->missile.wait > 0) { --effect->missile.wait; pose(effect); return; }
    effect->traits |= MF_RENDERABLE;
    if (effect->w2.ttl && --effect->w2.ttl == 0) { P_RemoveMobj(effect); return; }
    ++effect->w2.fx.age;
    if (effect->w2.cast.spell == W2_SPELL_VISION) {
        if (effect->w2.fx.age >= def->frames * def->sleep) effect->traits &= ~MF_RENDERABLE;
        pose(effect);
        return;
    }
    if (kind == W2_FX_RUNE) {
        if (effect->w2.fx.age % def->sleep) { pose(effect); return; }
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *unit = (mobj_t *)th;
            if (th->function == P_MobjThinker && unit->hp > 0 && !unit->remove &&
                !(unit->traits & (MF_FLY | MF_NOBLOCKMAP)) && W2_Distance(effect, unit) == 0) {
                impact(effect); P_RemoveMobj(effect); return;
            }
        }
        pose(effect);
        return;
    }
    if (kind == W2_FX_FLAME_SHIELD) {
        mobj_t *unit = P_MobjById(effect->w2.fx.subject);
        if (!unit) { P_RemoveMobj(effect); return; }
        /* Authored 36-position circle in Stratagus MissileFlameShield. */
        static const ivec2_t orbit[] = {
            {0,32},{5,31},{10,30},{16,27},{20,24},{24,20},{27,15},{30,10},{31,5},
            {32,0},{31,-5},{30,-10},{27,-16},{24,-20},{20,-24},{15,-27},{10,-30},{5,-31},
            {0,-32},{-5,-31},{-10,-30},{-16,-27},{-20,-24},{-24,-20},{-27,-15},{-30,-10},{-31,-5},
            {-32,0},{-31,5},{-30,10},{-27,16},{-24,20},{-20,24},{-15,27},{-10,30},{-5,31},
        };
        ivec2_t offset = orbit[effect->w2.ttl % 36];
        effect->core.position = fixed3_add(unit->core.position,
            fixed3_from_fixed2((fixed2_t){offset.x * FIXED_ONE / TILE_W, offset.y * FIXED_ONE / TILE_H}, FIXED_ONE / 4));
        if (!(effect->w2.ttl & 7)) for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *victim = (mobj_t *)th;
            if (th->function == P_MobjThinker && victim != unit && !(victim->traits & MF_NOBLOCKMAP) &&
                W2_Distance(unit, victim) <= 1) P_DamageMobj(victim, effect->target, effect->missile.damage);
        }
        pose(effect);
        return;
    }
    if (kind == W2_FX_WHIRLWIND) {
        if ((effect->w2.ttl % RTS_TICRATE) / 10 == 0) impact(effect);
        if (effect->w2.ttl % 100 == 0) {
            ivec2_t cell = fixed2_cell(fixed3_xy(effect->core.position)), goal;
            do { goal = (ivec2_t){cell.x + (int)(W2_SyncRand() % 5) - 2, cell.y + (int)(W2_SyncRand() % 5) - 2}; }
            while (!L_Contains(&level, goal.x, goal.y));
            effect->w2.fx.end = fixed3_from_fixed2(fixed2_cell_center(goal), 0);
        }
        if (effect->w2.fx.age % 8 == 0) {
            fixed2_t delta = fixed2_sub(fixed3_xy(effect->w2.fx.end), fixed3_xy(effect->core.position));
            fixed_t distance = fixed2_length(delta);
            if (distance > 0) effect->core.position = fixed3_add_planar(effect->core.position,
                fixed3_planar_delta(fixed2_rescale(delta, distance, fixed_min(distance, 2 * FIXED_ONE / TILE_W))));
        }
        pose(effect);
        return;
    }
    if (kind == W2_FX_SMALL_FIRE || kind == W2_FX_BIG_FIRE) {
        mobj_t *building = P_MobjById(effect->w2.fx.subject);
        if (!building) { P_RemoveMobj(effect); return; }
        if (effect->w2.fx.age % (def->frames * def->sleep) == 0) {
            int next = fire_kind(building);
            if (next < 0) { building->w2.fire = 0; P_RemoveMobj(effect); return; }
            effect->w2.fx.kind = next;
            effect->w2.fx.age = 0;
        }
    } else if (def->speed) {
        fixed2_t delta = fixed2_sub(fixed3_xy(effect->w2.fx.end),
                                    fixed3_xy(effect->core.position));
        fixed_t distance = fixed2_length(delta);
        fixed_t step = def->speed * FIXED_ONE / TILE_W;
        if (distance <= step) {
            effect->core.position = effect->w2.fx.end;
            if ((kind == W2_FX_LIGHTNING || kind == W2_FX_TOUCH || kind == W2_FX_BLIZZARD) &&
                ++effect->w2.fx.clock < def->frames) { pose(effect); return; }
            if (effect->w2.cast.spell == W2_SPELL_DEATH_COIL) {
                mobj_t *victim = P_MobjById(effect->w2.fx.subject);
                if (victim && (mobjinfo[victim->type_id].w2.attributes & W2_ORGANIC)) {
                    P_DamageMobj(victim, effect->target, effect->missile.damage);
                    if (effect->target && effect->target->hp > 0) {
                        effect->target->hp += effect->missile.damage;
                        if (effect->target->hp > effect->target->max_hp) effect->target->hp = effect->target->max_hp;
                    }
                }
                P_RemoveMobj(effect); return;
            }
            impact(effect);
            if (effect->w2.fx.bounces > 1 && distance > 0) {
                --effect->w2.fx.bounces;
                /* Stratagus bounce advances (tile width + height) * 3 / 4. */
                effect->w2.fx.end = fixed3_add_planar(effect->w2.fx.end,
                    fixed3_planar_delta(fixed2_rescale(delta, distance, FIXED_LIT(1.5))));
            } else { P_RemoveMobj(effect); return; }
        } else {
            effect->core.momentum = fixed3_planar_delta(fixed2_rescale(delta, distance, step));
            effect->core.position = fixed3_add_planar(effect->core.position, effect->core.momentum);
        }
    } else if (effect->w2.fx.age >= def->frames * def->sleep) {
        if (kind == W2_FX_DECAY) impact(effect);
        P_RemoveMobj(effect);
        return;
    }
    pose(effect);
}
