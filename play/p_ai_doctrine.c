#include "p_ai_local.h"

#include <math.h>
#include <string.h>

/* The computer player's doctrine layer: what each actor type is worth, what
 * the team has seen of its enemies, and the purchases and battle calls a
 * faction's AiDoctrine asks for. Games only supply numbers and roles. */

enum {
    AI_INTEL_FADE_MS = 180000,  /* An unseen enemy army fades from memory over this. */
    AI_CLOAK_MEMORY_MS = 180000,
    AI_SUPPLY_WAIT_MS = 6000,   /* Debounce while an ordered supply registers. */
    AI_AIR_DEFENSE_PCT = 30,    /* Enemy air share at which defenses turn anti-air. */
};
#define AI_ENGAGE_RADIUS 10.0f  /* Cells around a wave that count as its fight. */

/* ── unit knowledge ───────────────────────────────────────────────────── */

/* OpenBW calculate_unit_strengths: range in pixels, cooldown in frames,
 * sqrt(range / cooldown * damage + hp * (damage << 11) / cooldown >> 8). */
static int bw_strength(int hp, int damage, int hits, float range_cells, int cooldown_ms) {
    if (damage <= 0) return 0;
    damage *= hits > 0 ? hits : 1;
    int cooldown = cooldown_ms > 0 ? (cooldown_ms * 24 + 500) / 1000 : 24;
    if (cooldown < 1) cooldown = 1;
    double reach = range_cells * 32.0 / cooldown * damage;
    double body = (double)hp * (double)((damage << 11) / cooldown) / 256.0;
    return (int)(sqrt(reach + body) * 7.58);
}

void P_AiUnitInfo(const AiContext *ctx, uint16_t type_id, AiUnitInfo *out) {
    memset(out, 0, sizeof(*out));
    const mobjtype_t *type = P_ActorType(type_id);
    if (type) {
        uint32_t traits = type->traits;
        bool mobile = (traits & MF_MOBILE) != 0;
        out->hp = type->max_hp;
        if (traits & MF_HARVESTER) out->roles |= AI_ROLE_WORKER;
        if (traits & MF_FLY) out->roles |= AI_ROLE_FLYER;
        if (traits & MF_DETECTOR) out->roles |= AI_ROLE_DETECTOR;
        if ((traits & MF_ATTACK) && type->attack.damage > 0) {
            uint8_t reach = type->attack.targets ? type->attack.targets :
                            MOBJ_TARGET_GROUND | MOBJ_TARGET_AIR;
            if (reach & MOBJ_TARGET_GROUND) out->roles |= AI_ROLE_HITS_GROUND;
            if (reach & MOBJ_TARGET_AIR) out->roles |= AI_ROLE_HITS_AIR;
            out->roles |= mobile ? AI_ROLE_FIGHTER : AI_ROLE_DEFENSE;
        }
        if (mobile && (traits & (MF_HEAL | MF_REPAIR)) && !(traits & MF_HARVESTER))
            out->roles |= AI_ROLE_SUPPORT;
    }
    if (ctx && ctx->game && ctx->game->describe) ctx->game->describe(type_id, out);
    if (!type) return;
    int damage = type->attack.damage;
    if (!out->ground_strength && (out->roles & AI_ROLE_HITS_GROUND))
        out->ground_strength = bw_strength(out->hp, damage, out->hits,
                                           type->attack.range, type->attack.cooldown_ms);
    if (!out->air_strength && (out->roles & AI_ROLE_HITS_AIR))
        out->air_strength = bw_strength(out->hp, damage, out->hits,
                                        type->attack.range, type->attack.cooldown_ms);
    /* Brood War quarters workers: they fight, but nobody fears them. */
    if (out->roles & AI_ROLE_WORKER) {
        out->ground_strength /= 4;
        out->air_strength /= 4;
    }
}

/* A live unit's worth against a foe of which air_pct percent flies,
 * scaled by the health it has left. */
static int unit_strength(const AiContext *ctx, const mobj_t *unit, int air_pct, AiUnitInfo *info) {
    AiUnitInfo local;
    if (!info) info = &local;
    P_AiUnitInfo(ctx, unit->type_id, info);
    int value = (info->ground_strength * (100 - air_pct) + info->air_strength * air_pct) / 100;
    if (unit->max_hp > 0 && unit->hp < unit->max_hp) value = value * unit->hp / unit->max_hp;
    return value;
}

static int army_strength(const AiContext *ctx, mobj_t *const *army, int count, int air_pct) {
    int total = 0;
    for (int i = 0; i < count; ++i) total += unit_strength(ctx, army[i], air_pct, NULL);
    return total;
}

static bool alive(const mobj_t *u) {
    return u->hp > 0 && !u->remove;
}

/* A hostile object worth judging, as `eye` sees it. */
static bool seen_enemy(const level_t *map, const AiTeamState *team, int owner,
                       const mobj_t *eye, const mobj_t *e) {
    return alive(e) && e->owner < AI_MAX_TEAMS && !(e->traits & MF_NOBLOCKMAP) &&
           !P_AiIsAlly(map, team, owner, e) && P_VisibleTo(eye, e);
}

/* ── scouting ─────────────────────────────────────────────────────────── */

void P_AiScout(AiContext *ctx, AiTeamState *team, int owner, const level_t *map,
               mobj_t *const *units, int unit_count, int elapsed_ms) {
    const AiDoctrine *d = &team->plan.doctrine;
    if (!d->roster_count && !d->attack_ratio && !d->retreat_ratio) return;
    const mobj_t *eye = NULL;
    for (int i = 0; i < unit_count && !eye; ++i)
        if (units[i]->owner == owner && alive(units[i])) eye = units[i];
    if (team->enemy_cloak_ms > 0) team->enemy_cloak_ms -= elapsed_ms;
    team->enemy_strength -= (int)((int64_t)team->enemy_strength * elapsed_ms / AI_INTEL_FADE_MS);
    if (!eye) return;

    int seen = 0, air = 0, antiair = 0;
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *e = units[i];
        if (!seen_enemy(map, team, owner, eye, e)) continue;
        AiUnitInfo info;
        int value = unit_strength(ctx, e, 0, &info);
        if (info.roles & AI_ROLE_CLOAKED) team->enemy_cloak_ms = AI_CLOAK_MEMORY_MS;
        if (!(info.roles & AI_ROLE_FIGHTER)) continue;
        if (info.air_strength > info.ground_strength) value = unit_strength(ctx, e, 100, NULL);
        seen += value;
        if (info.roles & AI_ROLE_FLYER) air += value;
        if (info.roles & AI_ROLE_HITS_AIR) antiair += value;
    }
    if (seen <= 0) return;
    if (seen > team->enemy_strength) team->enemy_strength = seen;
    /* Shares move halfway per sighting so one stray flyer does not flip the mix. */
    team->enemy_air_pct = (team->enemy_air_pct + air * 100 / seen) / 2;
    team->enemy_antiair_pct = (team->enemy_antiair_pct + antiair * 100 / seen) / 2;
}

/* ── purchases ────────────────────────────────────────────────────────── */

static bool roster_info(const AiContext *ctx, int product, AiUnitInfo *out) {
    int actor = ctx->game->product_actor(product);
    if (actor <= 0) return false;
    P_AiUnitInfo(ctx, (uint16_t)actor, out);
    return true;
}

/* Alive plus queued of every roster product with any of `roles`. */
static int roster_owned(const AiContext *ctx, const AiDoctrine *d, int owner, uint32_t roles) {
    int count = 0;
    for (int i = 0; i < d->roster_count; ++i) {
        AiUnitInfo info;
        if (roster_info(ctx, d->roster[i].product, &info) && (info.roles & roles))
            count += ctx->game->owned(owner, d->roster[i].product);
    }
    return count;
}

/* Buys the first roster product that fills `role`, those with `prefer`
 * first; a product it must save or research for ends the search. */
static AiTry buy_role(AiContext *ctx, AiTeamState *team, int owner, level_t *map,
                      uint32_t role, uint32_t prefer) {
    const AiDoctrine *d = &team->plan.doctrine;
    for (int pass = prefer ? 0 : 1; pass < 2; ++pass) {
        for (int i = 0; i < d->roster_count; ++i) {
            AiUnitInfo info;
            if (!roster_info(ctx, d->roster[i].product, &info) || !(info.roles & role)) continue;
            if (pass == 0 && !(info.roles & prefer)) continue;
            AiTry result = P_AiTry(ctx, team, owner, map, d->roster[i].product);
            if (result != AI_TRY_SKIP) return result;
        }
    }
    return AI_TRY_SKIP;
}

bool P_AiBuySupply(AiContext *ctx, AiTeamState *team, int owner, level_t *map, int elapsed_ms) {
    const AiDoctrine *d = &team->plan.doctrine;
    if (!d->supply_buffer || !ctx->game->supply || !ctx->game->product_actor) return false;
    if (team->supply_wait_ms > 0) {
        team->supply_wait_ms -= elapsed_ms;
        return false;
    }
    int used = 0, cap = 0;
    if (!ctx->game->supply(owner, &used, &cap) || cap - used >= d->supply_buffer) return false;
    AiTry result = buy_role(ctx, team, owner, map, AI_ROLE_SUPPLY, 0);
    if (result == AI_TRY_BOUGHT) team->supply_wait_ms = AI_SUPPLY_WAIT_MS;
    return result == AI_TRY_SAVING;
}

static int towns(const AiTeamState *team) {
    return team->towns > 0 ? team->towns : 1;
}

static AiTry need_workers(AiContext *ctx, AiTeamState *team, int owner, level_t *map) {
    const AiDoctrine *d = &team->plan.doctrine;
    if (!d->workers || roster_owned(ctx, d, owner, AI_ROLE_WORKER) >= d->workers * towns(team))
        return AI_TRY_SKIP;
    return buy_role(ctx, team, owner, map, AI_ROLE_WORKER, 0);
}

/* Something cloaked was seen and nothing of ours can see it. */
static AiTry need_detection(AiContext *ctx, AiTeamState *team, int owner, level_t *map) {
    const AiDoctrine *d = &team->plan.doctrine;
    if (team->enemy_cloak_ms <= 0 || roster_owned(ctx, d, owner, AI_ROLE_DETECTOR) > 0)
        return AI_TRY_SKIP;
    return buy_role(ctx, team, owner, map, AI_ROLE_DETECTOR, AI_ROLE_FIGHTER);
}

static AiTry need_defense(AiContext *ctx, AiTeamState *team, int owner, level_t *map) {
    const AiDoctrine *d = &team->plan.doctrine;
    if (!d->defenses || roster_owned(ctx, d, owner, AI_ROLE_DEFENSE) >= d->defenses * towns(team))
        return AI_TRY_SKIP;
    uint32_t prefer = team->enemy_air_pct >= AI_AIR_DEFENSE_PCT ? AI_ROLE_HITS_AIR : AI_ROLE_HITS_GROUND;
    return buy_role(ctx, team, owner, map, AI_ROLE_DEFENSE, prefer);
}

/* A roster weight bent by `counter` toward what was scouted: anti-air
 * rises with enemy air, ground-only units sink with it, and flyers sink
 * against an army that shoots up. */
static int mix_weight(const AiDoctrine *d, const AiTeamState *team, const AiUnitInfo *info, int weight) {
    int w = weight * 100, c = d->counter;
    if (info->roles & AI_ROLE_HITS_AIR) w += w * c * team->enemy_air_pct / 10000;
    else if (info->roles & AI_ROLE_FIGHTER) w -= w * c * team->enemy_air_pct / 20000;
    if (info->roles & AI_ROLE_FLYER) w -= w * c * team->enemy_antiair_pct / 20000;
    return w > 1 ? w : 1;
}

/* The weighted product furthest below its share of the army. */
static AiTry need_army(AiContext *ctx, AiTeamState *team, int owner, level_t *map) {
    const AiDoctrine *d = &team->plan.doctrine;
    if (d->army_cap && roster_owned(ctx, d, owner, AI_ROLE_FIGHTER) >= d->army_cap)
        return AI_TRY_SKIP;
    int best = -1;
    int64_t best_score = 0;
    for (int i = 0; i < d->roster_count; ++i) {
        const AiChoice *choice = &d->roster[i];
        AiUnitInfo info;
        if (choice->weight <= 0 || !roster_info(ctx, choice->product, &info)) continue;
        int status = ctx->game->can_purchase(map, owner, choice->product);
        if (status != AI_BUY_OK && status != AI_BUY_NEED_CREDITS) continue;
        int64_t score = (int64_t)mix_weight(d, team, &info, choice->weight) * 1000 /
                        (ctx->game->owned(owner, choice->product) + 1);
        if (best < 0 || score > best_score) { best = i; best_score = score; }
    }
    return best < 0 ? AI_TRY_SKIP : P_AiTry(ctx, team, owner, map, d->roster[best].product);
}

int P_AiBuyDoctrine(AiContext *ctx, AiTeamState *team, int owner, level_t *map, int budget) {
    static AiTry (*const needs[])(AiContext *, AiTeamState *, int, level_t *) = {
        need_workers, need_detection, need_defense, need_army,
    };
    if (!ctx->game->product_actor || team->plan.doctrine.roster_count <= 0) return 0;
    int bought = 0;
    while (bought < budget) {
        AiTry result = AI_TRY_SKIP;
        for (size_t i = 0; i < sizeof(needs) / sizeof(*needs) && result == AI_TRY_SKIP; ++i)
            result = needs[i](ctx, team, owner, map);
        if (result != AI_TRY_BOUGHT) break;
        ++bought;
    }
    return bought;
}

/* ── battle calls ─────────────────────────────────────────────────────── */

bool P_AiArmyReady(const AiContext *ctx, AiTeamState *team,
                   mobj_t *const *army, int army_count, int max_size) {
    const AiDoctrine *d = &team->plan.doctrine;
    if (!d->attack_ratio || army_count >= max_size) return true;
    int strength = army_strength(ctx, army, army_count, team->enemy_air_pct);
    if ((int64_t)strength * 100 >= (int64_t)team->enemy_strength * d->attack_ratio) return true;
    team->stats.holds++;
    return false;
}

void P_AiTrackWave(AiTeamState *team, mobj_t *const *sent, int sent_count) {
    team->wave_count = sent_count < AI_MAX_WAVE_TRACK ? sent_count : AI_MAX_WAVE_TRACK;
    for (int i = 0; i < team->wave_count; ++i) team->wave[i] = sent[i]->id;
}

static bool in_wave(const AiTeamState *team, uint32_t id) {
    for (int i = 0; i < team->wave_count; ++i)
        if (team->wave[i] == id) return true;
    return false;
}

void P_AiCheckRetreat(AiContext *ctx, AiTeamState *team, int owner, level_t *map,
                      mobj_t *const *units, int unit_count) {
    const AiDoctrine *d = &team->plan.doctrine;
    if (!d->retreat_ratio || team->wave_count <= 0) return;
    mobj_t *members[AI_MAX_WAVE_TRACK];
    int count = 0, flying = 0;
    fvec2_t centre = {0, 0};
    for (int i = 0; i < unit_count && count < AI_MAX_WAVE_TRACK; ++i) {
        mobj_t *u = units[i];
        if (u->owner != owner || !alive(u) || !in_wave(team, u->id)) continue;
        members[count++] = u;
        if (u->traits & MF_FLY) ++flying;
        fvec2_t at = fixed3_xy_to_fvec2(u->core.position);
        centre.x += at.x;
        centre.y += at.y;
    }
    P_AiTrackWave(team, members, count);
    if (count == 0) return;
    centre.x /= count;
    centre.y /= count;

    int our_air_pct = flying * 100 / count, enemy = 0;
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *e = units[i];
        if (!seen_enemy(map, team, owner, members[0], e)) continue;
        if (fvec2_distance_squared(fixed3_xy_to_fvec2(e->core.position), centre) >
            AI_ENGAGE_RADIUS * AI_ENGAGE_RADIUS) continue;
        AiUnitInfo info;
        int value = unit_strength(ctx, e, our_air_pct, &info);
        if (info.roles & (AI_ROLE_FIGHTER | AI_ROLE_DEFENSE)) enemy += value;
    }
    int ours = army_strength(ctx, members, count, team->enemy_air_pct);
    if (enemy <= 0 || (int64_t)ours * 100 >= (int64_t)enemy * d->retreat_ratio) return;

    /* What chased the wave off is the enemy army now, whatever was remembered. */
    if (enemy > team->enemy_strength) team->enemy_strength = enemy;
    for (int i = 0; i < count; ++i) members[i]->attack.target = NULL;
    P_MoveUnitsAt(map, members, count, team->base_position);
    team->wave_count = 0;
    team->stats.retreats++;
    P_AiEmit(ctx, AI_EVENT_RETREAT, owner, count);
}
