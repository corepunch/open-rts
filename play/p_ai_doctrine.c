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
#define AI_SCOUT_REACH FIXED_LIT(6.0)     /* Cells from a start location at which the scout has seen it. */
#define AI_ENGAGE_RADIUS FIXED_LIT(10.0)  /* Cells around a wave that count as its fight. */

/* ── unit knowledge ───────────────────────────────────────────────────── */

/* OpenBW calculate_unit_strengths: range in pixels, cooldown in frames,
 * sqrt(range / cooldown * damage + hp * (damage << 11) / cooldown >> 8).
 * Damage counts every hit of an attack; a bouncing glaive counts one more,
 * as Brood War's units.dat max hits does for the Mutalisk. */
static int bw_strength(int hp, const weapondef_t *weapon) {
    int damage = weapon->damage * ((weapon->hits ? weapon->hits : 1) + (weapon->bounces ? 1 : 0));
    fixed_t range = weapon->range;
    if (damage <= 0) return 0;
    int cooldown = weapon->cooldown_ms > 0 ? (weapon->cooldown_ms * 24 + 500) / 1000 : 24;
    if (cooldown < 1) cooldown = 1;
    /* Integer 16.16 evaluation: no libm, identical on every peer. */
    int64_t reach = ((int64_t)range * 32 * damage) / cooldown;
    int64_t body = (int64_t)hp * ((damage << 11) / cooldown) * 256; /* x65536 / 256 */
    uint64_t root = fixed_isqrt64((uint64_t)(reach + body) << 16);   /* sqrt * 65536 */
    return (int)((root * 758u) / 100u >> 16);
}

static bool weapon_reaches(const weapondef_t *weapon, uint8_t kind) {
    return weapon->damage > 0 && (!weapon->targets || (weapon->targets & kind));
}

void P_AiUnitInfo(const AiContext *ctx, uint16_t type_id, AiUnitInfo *out) {
    (void)ctx;
    memset(out, 0, sizeof(*out));
    const mobjtype_t *type = P_ActorType(type_id);
    /* The weapons it turns on ground and air targets (see P_MobjWeapon). */
    const weapondef_t *ground = type ? &type->attack : NULL,
                      *air = type && type->air_attack.damage ? &type->air_attack : ground;
    if (type) {
        uint32_t traits = type->traits;
        bool mobile = (traits & MF_MOBILE) != 0;
        out->hp = type->max_hp;
        if (traits & MF_HARVESTER) out->roles |= AI_ROLE_WORKER;
        if (traits & MF_FLY) out->roles |= AI_ROLE_FLYER;
        if (traits & MF_DETECTOR) out->roles |= AI_ROLE_DETECTOR;
        if (traits & MF_CLOAKED) out->roles |= AI_ROLE_CLOAKED;
        if (traits & MF_ATTACK) {
            if (weapon_reaches(ground, MOBJ_TARGET_GROUND)) out->roles |= AI_ROLE_HITS_GROUND;
            if (weapon_reaches(air, MOBJ_TARGET_AIR)) out->roles |= AI_ROLE_HITS_AIR;
            if (out->roles & (AI_ROLE_HITS_GROUND | AI_ROLE_HITS_AIR))
                out->roles |= mobile ? AI_ROLE_FIGHTER : AI_ROLE_DEFENSE;
        }
        if (mobile && (traits & (MF_HEAL | MF_REPAIR)) && !(traits & MF_HARVESTER))
            out->roles |= AI_ROLE_SUPPORT;
    }
    /* The ruleset adds what the actor type cannot tell: supply, cloaking,
     * casters, shields and multi-hit weapons. A caster that fights is no support. */
    if (g_ruleset.actors && type_id < g_ruleset.actor_count) {
        const actorrole_t *role = &g_ruleset.actors[type_id];
        uint32_t roles = role->roles;
        if ((roles & AI_ROLE_SUPPORT) && (out->roles & AI_ROLE_FIGHTER)) roles &= ~(uint32_t)AI_ROLE_SUPPORT;
        out->roles |= roles;
        out->roles &= ~role->not_roles;
        out->hp += role->extra_hp;
    }
    if (!type) return;
    if (!out->ground_strength && (out->roles & AI_ROLE_HITS_GROUND))
        out->ground_strength = bw_strength(out->hp, ground);
    if (!out->air_strength && (out->roles & AI_ROLE_HITS_AIR))
        out->air_strength = bw_strength(out->hp, air);
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
    if (ctx && ctx->game && ctx->game->describe_unit) ctx->game->describe_unit(unit, info);
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
        if (!(e->traits & MF_MOBILE) && !team->found_ms) team->found_ms = ctx->clock_ms;
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

/* The scout: the fastest of the idle unarmed flyers (an Overlord) and the
 * workers not carrying, as a drone outruns an Overlord to a far base. */
static mobj_t *pick_scout(const AiContext *ctx, int owner, mobj_t *const *units, int unit_count) {
    mobj_t *best = NULL;
    for (int i = 0; i < unit_count; ++i) {
        mobj_t *u = units[i];
        if (u->owner != owner || !alive(u) || !(u->traits & MF_MOBILE) || P_AiIsBusy(ctx, u)) continue;
        bool flyer = (u->traits & MF_FLY) && !(u->traits & MF_ATTACK) && !P_HasMoveOrder(u),
             worker = (u->traits & MF_HARVESTER) && !u->harvest.cargo && u->harvest.phase != HARVEST_PHASE_TO_BASE;
        if ((flyer || worker) && (!best || (u->info ? u->info->speed : 0) > (best->info ? best->info->speed : 0))) best = u;
    }
    return best;
}

void P_AiSendScout(AiContext *ctx, AiTeamState *team, int owner, level_t *map,
                   mobj_t *const *units, int unit_count) {
    const AiDoctrine *d = &team->plan.doctrine;
    if (!d->scout || !ctx->game->starts || !team->has_base) return;
    mobj_t *scout = team->scout ? P_MobjById(team->scout) : NULL;
    if (scout && (!alive(scout) || scout->owner != owner)) scout = NULL;
    fixed2_t starts[32];
    int count = ctx->game->starts(map, starts, 32);
    for (int i = 0; i < count; ++i) {
        int64_t home = fixed2_distance_squared64(starts[i], team->base_position),
                reach = fixed_sq64(AI_SCOUT_REACH);
        bool seen = home < fixed_sq64(AI_TOWN_RADIUS) ||
            (scout && fixed2_distance_squared64(starts[i], fixed3_xy(scout->core.position)) < reach);
        if (seen) team->starts_seen |= 1u << i;
    }
    /* Pick the nearest start not yet seen. */
    int next = -1;
    fixed2_t from = scout ? fixed3_xy(scout->core.position) : team->base_position;
    for (int i = 0; i < count; ++i)
        if (!(team->starts_seen & (1u << i)) &&
            (next < 0 || fixed2_distance_squared64(from, starts[i]) < fixed2_distance_squared64(from, starts[next])))
            next = i;
    if (team->found_ms || next < 0) {
        /* Seen enough: back home, where a worker goes back to mining. */
        if (scout) P_MoveUnitTo(map, scout, team->base_position);
        team->scout = 0;
        return;
    }
    if (!scout) {
        if (P_AiOwned(ctx, owner, d->scout) <= 0 || !(scout = pick_scout(ctx, owner, units, unit_count))) return;
        scout->harvest.phase = HARVEST_PHASE_NONE;
        scout->harvest.target = -1;
        scout->attack.target = NULL;
        team->scout = scout->id;
        team->stats.scouts++;
        P_AiEmit(ctx, AI_EVENT_SCOUT, owner, (int)scout->id);
    }
    if (P_HasMoveOrder(scout) && fixed2_near(scout->movement.goal, starts[next], FIXED_ONE / 2)) return;
    if (!P_MoveUnitTo(map, scout, starts[next])) team->starts_seen |= 1u << next;
}

/* ── purchases ────────────────────────────────────────────────────────── */

static bool roster_info(const AiContext *ctx, int product, AiUnitInfo *out) {
    int actor = P_AiProductActor(ctx, product);
    if (actor <= 0) return false;
    P_AiUnitInfo(ctx, (uint16_t)actor, out);
    return true;
}

/* Alive plus queued of every roster product with any of `roles` and,
 * when `armed` is set, a weapon too. */
static int roster_owned_armed(const AiContext *ctx, const AiDoctrine *d, int owner,
                              uint32_t roles, uint32_t armed) {
    int count = 0;
    for (int i = 0; i < d->roster_count; ++i) {
        AiUnitInfo info;
        if (roster_info(ctx, d->roster[i].product, &info) && (info.roles & roles) &&
            (!armed || (info.roles & armed)))
            count += P_AiOwned(ctx, owner, d->roster[i].product);
    }
    return count;
}

static int roster_owned(const AiContext *ctx, const AiDoctrine *d, int owner, uint32_t roles) {
    return roster_owned_armed(ctx, d, owner, roles, 0);
}

/* Buys the first roster product with every bit of `role`, those with
 * `prefer` first; a product it must save or research for ends the search. */
static AiTry buy_role(AiContext *ctx, AiTeamState *team, int owner, level_t *map,
                      uint32_t role, uint32_t prefer) {
    const AiDoctrine *d = &team->plan.doctrine;
    for (int pass = prefer ? 0 : 1; pass < 2; ++pass) {
        for (int i = 0; i < d->roster_count; ++i) {
            AiUnitInfo info;
            if (!roster_info(ctx, d->roster[i].product, &info) || (info.roles & role) != role) continue;
            if (pass == 0 && !(info.roles & prefer)) continue;
            AiTry result = P_AiTry(ctx, team, owner, map, d->roster[i].product);
            if (result != AI_TRY_SKIP) return result;
        }
    }
    return AI_TRY_SKIP;
}

bool P_AiBuySupply(AiContext *ctx, AiTeamState *team, int owner, level_t *map, int elapsed_ms) {
    const AiDoctrine *d = &team->plan.doctrine;
    if (!d->supply_buffer || !ctx->game->supply) return false;
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

bool P_AiExpand(AiContext *ctx, AiTeamState *team, int owner, level_t *map) {
    const AiDoctrine *d = &team->plan.doctrine;
    if (!d->expand_workers || !ctx->game->expand || team->towns >= AI_MAX_TOWNS ||
        (d->max_towns && team->towns >= d->max_towns) ||
        (d->expand_after && P_AiOwned(ctx, owner, d->expand_after) <= 0) ||
        roster_owned(ctx, d, owner, AI_ROLE_WORKER) < d->expand_workers * towns(team))
        return false;
    int status = ctx->game->expand(map, owner);
    if (status == AI_BUY_OK) {
        team->stats.expansions++;
        team->stats.purchases++;
        P_AiEmit(ctx, AI_EVENT_EXPAND, owner, team->towns);
    }
    return status == AI_BUY_NEED_CREDITS;
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

/* Armed defenses first: a game whose towers grow out of an unarmed site
 * (Warcraft's Watch Tower, a Zerg Creep Colony) arms the sites standing,
 * and raises another site only when every one is armed or on the way. */
static AiTry need_defense(AiContext *ctx, AiTeamState *team, int owner, level_t *map) {
    const AiDoctrine *d = &team->plan.doctrine;
    const uint32_t weapon = AI_ROLE_HITS_GROUND | AI_ROLE_HITS_AIR;
    int want = d->defenses * towns(team);
    if (!d->defenses || roster_owned_armed(ctx, d, owner, AI_ROLE_DEFENSE, weapon) >= want)
        return AI_TRY_SKIP;
    uint32_t prefer = team->enemy_air_pct >= AI_AIR_DEFENSE_PCT ? AI_ROLE_HITS_AIR : AI_ROLE_HITS_GROUND;
    AiTry result = buy_role(ctx, team, owner, map, AI_ROLE_DEFENSE | prefer, 0);
    if (result == AI_TRY_SKIP)
        result = buy_role(ctx, team, owner, map, AI_ROLE_DEFENSE | (weapon & ~prefer), 0);
    if (result != AI_TRY_SKIP || roster_owned(ctx, d, owner, AI_ROLE_DEFENSE) >= want) return result;
    return buy_role(ctx, team, owner, map, AI_ROLE_DEFENSE, 0);
}

/* Keeps upgrading the army it fields: the most numerous roster units
 * first, one purchase per 100/research fighters, so tech never runs
 * ahead of an army to carry it. */
static AiTry need_research(AiContext *ctx, AiTeamState *team, int owner, level_t *map) {
    const AiDoctrine *d = &team->plan.doctrine;
    if (!d->research || !ctx->game->advance) return AI_TRY_SKIP;
    if ((int64_t)roster_owned(ctx, d, owner, AI_ROLE_FIGHTER) * d->research <
        (int64_t)(team->stats.upgrades + 1) * 100) return AI_TRY_SKIP;
    int count[AI_MAX_ROSTER];
    bool tried[AI_MAX_ROSTER] = { false };
    for (int i = 0; i < d->roster_count; ++i) count[i] = P_AiOwned(ctx, owner, d->roster[i].product);
    for (int n = 0; n < d->roster_count; ++n) {
        int best = -1;
        for (int i = 0; i < d->roster_count; ++i)
            if (!tried[i] && d->roster[i].weight > 0 && (best < 0 || count[i] > count[best])) best = i;
        if (best < 0) break;
        tried[best] = true;
        int product = ctx->game->advance(map, owner, d->roster[best].product);
        AiTry result = product ? P_AiTry(ctx, team, owner, map, product) : AI_TRY_SKIP;
        if (result == AI_TRY_BOUGHT) team->stats.upgrades++;
        if (result != AI_TRY_SKIP) return result;
    }
    return AI_TRY_SKIP;
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
        int status = P_AiCanPurchase(ctx, map, owner, choice->product);
        if (status != AI_BUY_OK && status != AI_BUY_NEED_CREDITS) continue;
        int64_t score = (int64_t)mix_weight(d, team, &info, choice->weight) * 1000 /
                        (P_AiOwned(ctx, owner, choice->product) + 1);
        if (best < 0 || score > best_score) { best = i; best_score = score; }
    }
    return best < 0 ? AI_TRY_SKIP : P_AiTry(ctx, team, owner, map, d->roster[best].product);
}

int P_AiBuyDoctrine(AiContext *ctx, AiTeamState *team, int owner, level_t *map, int budget) {
    static AiTry (*const needs[])(AiContext *, AiTeamState *, int, level_t *) = {
        need_workers, need_detection, need_defense, need_research, need_army,
    };
    if (team->plan.doctrine.roster_count <= 0) return 0;
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
    int64_t sum_x = 0, sum_y = 0;
    for (int i = 0; i < unit_count && count < AI_MAX_WAVE_TRACK; ++i) {
        mobj_t *u = units[i];
        if (u->owner != owner || !alive(u) || !in_wave(team, u->id)) continue;
        members[count++] = u;
        if (u->traits & MF_FLY) ++flying;
        sum_x += u->core.position.x;
        sum_y += u->core.position.y;
    }
    P_AiTrackWave(team, members, count);
    if (count == 0) return;
    fixed2_t centre = { (fixed_t)(sum_x / count), (fixed_t)(sum_y / count) };

    int our_air_pct = flying * 100 / count, enemy = 0;
    for (int i = 0; i < unit_count; ++i) {
        const mobj_t *e = units[i];
        if (!seen_enemy(map, team, owner, members[0], e)) continue;
        if (fixed2_distance_squared64(fixed3_xy(e->core.position), centre) >
            fixed_sq64(AI_ENGAGE_RADIUS)) continue;
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
