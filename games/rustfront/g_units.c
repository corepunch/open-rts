#include "engine.h"
#include "info.h"
#include "rustfront.h"

#include <string.h>

/* Each faction's special units and powers. The three factions take after three classic RTS
 * games, in their mechanics rather than their looks (rustfront.h lists them):
 *
 *   Directorate   Dark Reign: transports, field repair, phasing, gunships, deployable guns.
 *   Scrap Clans   KKnD: a mobile derrick, mines, a monster truck, an autogyro.
 *   Lumen Concord 7th Legion: power cards, a bounty on kills, spider mechs, priests.
 *
 * Everything here is simulation state (the rf_ fields in mobj_data.h, and the Rites below, which
 * the save and the lockstep hash carry) and every order comes through a ticcmd or the
 * deterministic AI, so every peer agrees. */

#define TIC_MS RTS_TICK_MS
#define SECONDS(s) ((int)((s) * RTS_TICRATE))

static bool live(const mobj_t *u) {
    return u && !u->remove && u->hp > 0;
}

/* A unit of the world: not an effect or a shot. */
static bool unit_of_world(const mobj_t *u) {
    return live(u) && u->type_id > MT_NULL && u->type_id < MT_FX_HIT;
}

static int64_t dist2(const mobj_t *a, fixed2_t at) {
    return fixed2_distance_squared64(fixed3_xy(a->core.position), at);
}

static bool near(const mobj_t *a, fixed2_t at, fixed_t reach) {
    return dist2(a, at) <= (int64_t)reach * reach;
}

/* A structure is reached at its footprint's edge, anything else at its centre. */
static fixed_t reach_to(const mobj_t *u, fixed_t reach) {
    return RF_IsStructure(u) ? reach + u->info->footprint.w * (FIXED_ONE / 2) : reach;
}

static mobj_t *spawn_fx(uint16_t type, fixed2_t at, fixed_t height) {
    mobj_t *fx = P_SpawnMobj(fixed3_from_fixed2(at, height), type);
    if (!fx) return NULL;
    fx->owner = fx->team = 0;
    fx->allegiance = ALLEGIANCE_NEUTRAL;
    return fx;
}

/* ── anti-air ───────────────────────────────────────────────────────────────
 * Aircraft fly over cliffs, water and walls, and only these weapons reach them: rifles, rockets,
 * machine guns and the lighter beams. Cannons, artillery, flames and rams cannot. */
static const uint16_t ANTI_AIR[] = {
    MT_RIFLEMAN, MT_ROCKET, MT_SCOUT, MT_APC, MT_SENTRY,
    MT_CL_RAIDER, MT_CL_TRIKE, MT_CL_TRUCK, MT_CL_TOWER, MT_CL_RIG,
    MT_LC_WARDEN, MT_LC_LANCER, MT_LC_SKIMMER, MT_LC_TOWER, MT_LC_AEGIS, MT_LC_SERAPH,
};

bool RF_AntiAir(uint16_t type) {
    for (size_t i = 0; i < sizeof(ANTI_AIR) / sizeof(*ANTI_AIR); ++i)
        if (ANTI_AIR[i] == type) return true;
    return false;
}

bool RF_Phased(const mobj_t *unit) {
    return unit && unit->rf_phase > 0;
}

/* gameinfo_t.can_target: nobody shoots a soldier inside an APC or a phased Phantom, and only
 * anti-air weapons shoot at aircraft. */
static bool can_target(const mobj_t *attacker, const mobj_t *victim) {
    if (victim->rf_boarded || RF_Phased(victim)) return false;
    if ((victim->traits & MF_FLY) && !RF_AntiAir(attacker->type_id)) return false;
    return true;
}

/* gameinfo_t.attack_blocked: a Phantom out of phase cannot fire. */
static bool attack_blocked(const mobj_t *attacker, const mobj_t *target) {
    (void)target;
    return RF_Phased(attacker);
}

/* The hooks are set on the game's info at load (info.c's game_info is const, so g_game.c names
 * these); see RF_CanTargetHook below. */
bool RF_CanTargetHook(const mobj_t *attacker, const mobj_t *victim) { return can_target(attacker, victim); }
bool RF_AttackBlockedHook(const mobj_t *attacker, const mobj_t *target) { return attack_blocked(attacker, target); }

/* A unit the outside world must not see: a soldier aboard. */
bool G_ViewerSees(const mobj_t *target, int owner, int team) {
    (void)owner; (void)team;
    return !target->rf_boarded;
}

/* ── transports: the Directorate APC (Dark Reign's, C&C's) ──────────────────
 * It carries five soldiers. Told to board, a soldier walks to the APC and climbs in: it is
 * hidden, takes no orders and cannot be hurt. Unloaded, they step out round the APC; if the
 * APC is destroyed they bail out, hurt. */
bool RF_IsTransport(const mobj_t *unit) {
    return unit && unit->type_id == MT_APC;
}

int RF_Passengers(const mobj_t *transport, uint32_t *ids, int max) {
    int n = 0;
    if (!transport) return 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        if (th->function != P_MobjThinker || !live(u) || !u->rf_boarded || u->rf_carrier != transport->id) continue;
        if (ids && n < max) ids[n] = u->id;
        ++n;
    }
    return n;
}

/* Aboard, plus those on their way. */
static int spoken_for(const mobj_t *transport, const mobj_t *except) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        if (th->function == P_MobjThinker && u != except && live(u) && u->rf_carrier == transport->id) ++n;
    }
    return n;
}

bool G_UnitCommandable(const mobj_t *unit) {
    return !unit->rf_boarded;
}

bool RF_BoardOrder(mobj_t *soldier, mobj_t *transport) {
    if (!live(soldier) || !live(transport) || soldier->rf_boarded || !RF_IsInfantry(soldier->type_id) ||
        !RF_IsTransport(transport) || soldier->owner != transport->owner ||
        spoken_for(transport, soldier) >= RF_APC_SEATS) return false;
    P_ClearMove(soldier);
    soldier->attack.target = NULL;
    soldier->movement.order_id = 0;
    soldier->waypoints = (waypoints_t){0};
    soldier->rf_carrier = transport->id;
    P_MoveUnitTo(&level, soldier, fixed3_xy(transport->core.position));
    return true;
}

/* A cell beside the transport a soldier can stand in: rings outward, clear ground, nobody there. */
static bool free_spot(const mobj_t *soldier, const mobj_t *transport, fixed2_t *out) {
    ivec2_t c = {transport->core.position.x >> FIXED_FRAC_BITS, transport->core.position.y >> FIXED_FRAC_BITS};
    for (int r = 1; r <= 3; ++r)
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (dx > -r && dx < r && dy > -r && dy < r) continue;
                ivec2_t cell = {c.x + dx, c.y + dy};
                if (!L_Contains(&level, cell.x, cell.y) || (level.blocked && level.blocked[L_Index(&level, cell.x, cell.y)]))
                    continue;
                fixed2_t at = fixed2_cell_center(cell);
                bool taken = false;
                for (thinker_t *th = thinkercap.next; th && th != &thinkercap && !taken; th = th->next) {
                    const mobj_t *o = (const mobj_t *)th;
                    if (th->function != P_MobjThinker || o == soldier || !unit_of_world(o) || o->rf_boarded ||
                        (o->traits & (MF_FLY | MF_NOBLOCKMAP)) || o->type_id == MT_CL_MINE) continue;
                    taken = near(o, at, FIXED_LIT(0.45));
                }
                if (!taken) { *out = at; return true; }
            }
    return false;
}

static void embark(mobj_t *soldier, const mobj_t *transport) {
    P_ClearMove(soldier);
    soldier->movement.order_id = 0;
    soldier->attack.target = NULL;
    soldier->rf_boarded = true;
    P_MobjSetSelected(soldier, false);
    P_MobjSetHidden(soldier, true);
    soldier->traits &= ~(MF_MOBILE | MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK);
    soldier->traits |= MF_NOBLOCKMAP;
    soldier->core.position = transport->core.position;
}

static void disembark(mobj_t *soldier, fixed2_t at) {
    soldier->rf_boarded = false;
    soldier->rf_carrier = 0;
    soldier->core.position = fixed3_from_fixed2(at, 0);
    soldier->core.momentum = fixed3_zero();
    soldier->traits = soldier->info->traits;
    P_MobjSetHidden(soldier, false);
    P_SetMobjState(soldier, mobjinfo[soldier->type_id].spawnstate);
}

bool RF_Unload(mobj_t *transport) {
    if (!live(transport) || !RF_IsTransport(transport)) return false;
    bool any = false;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (th->function != P_MobjThinker || !live(u) || !u->rf_boarded || u->rf_carrier != transport->id) continue;
        fixed2_t at;
        if (!free_spot(u, transport, &at)) break;
        disembark(u, at);
        any = true;
    }
    return any;
}

static void tick_boarding(mobj_t *soldier) {
    mobj_t *transport = P_MobjById(soldier->rf_carrier);
    if (soldier->rf_boarded) {
        if (live(transport)) soldier->core.position = transport->core.position;
        else disembark(soldier, fixed3_xy(soldier->core.position));
        return;
    }
    if (!live(transport) || transport->owner != soldier->owner) { soldier->rf_carrier = 0; return; }
    if (near(soldier, fixed3_xy(transport->core.position), FIXED_LIT(1.1))) {
        if (RF_Passengers(transport, NULL, 0) < RF_APC_SEATS) embark(soldier, transport);
        else { soldier->rf_carrier = 0; P_ClearMove(soldier); }
        return;
    }
    /* The APC may be driving: follow it, a fresh path every half second. */
    if (!P_HasMoveOrder(soldier) || (leveltime + soldier->id) % (RTS_TICRATE / 2) == 0)
        if (!P_MoveUnitTo(&level, soldier, fixed3_xy(transport->core.position)) && !P_HasMoveOrder(soldier))
            soldier->rf_carrier = 0;
}

/* An order that replaces what a soldier was doing calls off its boarding. */
void G_InterruptOrders(const ticcmd_t *cmd, mobj_t *const *units, int count, bool late) {
    if (late || cmd->order == TC_BOARD) return;
    for (int i = 0; i < count; ++i)
        if (units[i] && !units[i]->rf_boarded) units[i]->rf_carrier = 0;
}

/* ── deploying: the Sentry Truck and the Drill Rig ──────────────────────────
 * A Sentry Truck unfolds into a Sentry Post on its cell and packs up again; a Drill Rig sets
 * its derrick down on the 2 x 2 cells it stands over and stays. The ground must be clear and
 * open: no building, wall of rock, water, or other unit on it. */
static uint16_t deploys_to(uint16_t type) {
    switch (type) {
    case MT_SENTRY_TRUCK: return MT_SENTRY;
    case MT_SENTRY: return MT_SENTRY_TRUCK;
    case MT_CL_DRILL: return MT_CL_DERRICK;
    default: return 0;
    }
}

bool RF_CanDeploy(const mobj_t *unit) {
    return unit && deploys_to(unit->type_id) != 0;
}

static bool ground_clear(const mobj_t *self, irect_t cells) {
    for (int y = cells.y; y < cells.y + cells.h; ++y)
        for (int x = cells.x; x < cells.x + cells.w; ++x) {
            if (!L_Contains(&level, x, y) || (level.blocked && level.blocked[L_Index(&level, x, y)])) return false;
            if (level.cell_terrain && RF_TerrainHoldsOre(level.cell_terrain[L_Index(&level, x, y)])) return false;
        }
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *o = (const mobj_t *)th;
        if (th->function != P_MobjThinker || o == self || !unit_of_world(o) || o->rf_boarded ||
            (o->traits & MF_FLY) || o->type_id == MT_CL_MINE) continue;
        irect_t at = RF_IsStructure(o) ? P_MobjCells(o) :
            (irect_t){o->core.position.x >> FIXED_FRAC_BITS, o->core.position.y >> FIXED_FRAC_BITS, 1, 1};
        if (at.x < cells.x + cells.w && cells.x < at.x + at.w && at.y < cells.y + cells.h && cells.y < at.y + at.h)
            return false;
    }
    return true;
}

bool RF_Deploy(mobj_t *unit) {
    uint16_t to = live(unit) ? deploys_to(unit->type_id) : 0;
    const mobjtype_t *type = to ? P_ActorType(to) : NULL;
    if (!type || unit->rf_boarded) return false;
    if (type->footprint.w > 0) {
        ivec2_t corner = fixed2_footprint_corner(fixed3_xy(unit->core.position), type->footprint);
        irect_t cells = {corner.x, corner.y, type->footprint.w, type->footprint.h};
        if (!ground_clear(unit, cells)) return false;
        unit->core.position = fixed3_from_fixed2(
            (fixed2_t){corner.x * FIXED_ONE + type->footprint.w * (FIXED_ONE / 2),
                       corner.y * FIXED_ONE + type->footprint.h * (FIXED_ONE / 2)}, 0);
    }
    unit->core.momentum = fixed3_zero();
    unit->movement.order_id = 0;
    unit->waypoints = (waypoints_t){0};
    if (!P_MorphMobj(unit, to)) return false;
    unit->rf_timer = 0;
    RF_SyncBlocking(&level);
    RF_PlaySound(RF_SND_BUILD_DONE, unit);
    return true;
}

/* ── the Phantom (Dark Reign's phase tank) ──────────────────────────────────
 * The first hit it takes shifts it out of phase for three seconds: it flickers, nothing can
 * target or hurt it, and it cannot fire. Then it needs twenty seconds before it can again. */
enum { PHASE_TICS = SECONDS(3), RECHARGE_TICS = SECONDS(20) };

void RF_UnitHit(mobj_t *victim, const mobj_t *source) {
    (void)source;
    if (!live(victim) || victim->type_id != MT_PHANTOM || victim->rf_phase || victim->rf_recharge) return;
    victim->rf_phase = PHASE_TICS;
    victim->rf_recharge = RECHARGE_TICS;
    victim->attack.target = NULL;
    victim->core.render_flags |= RTS_FRAME_BLINK;
    RF_PlaySound(RF_SND_ZAP, victim);
}

static void tick_phase(mobj_t *u) {
    if (u->rf_phase > 0 && --u->rf_phase == 0) u->core.render_flags &= ~RTS_FRAME_BLINK;
    if (u->rf_recharge > 0) --u->rf_recharge;
    if (u->rf_ward > 0) --u->rf_ward;
}

/* ── the Field Crane (Dark Reign's mechanic) ────────────────────────────────
 * Each second it welds 30 hit points onto the most damaged vehicle or structure of its side
 * within two cells (60 onto a structure), free of charge. Not soldiers: that is the Cantor's. */
static void tick_crane(mobj_t *crane, mobj_t *const *mobjs, int count) {
    mobj_t *best = NULL;
    for (int i = 0; i < count; ++i) {
        mobj_t *u = mobjs[i];
        if (u == crane || !unit_of_world(u) || u->team != crane->team || u->hp >= u->max_hp || u->rf_boarded ||
            RF_IsInfantry(u->type_id) || u->type_id == MT_CL_MINE || RF_IsSite(u) ||
            !near(u, fixed3_xy(crane->core.position), reach_to(u, FIXED_LIT(2.0)))) continue;
        if (!best || (int64_t)u->hp * best->max_hp < (int64_t)best->hp * u->max_hp) best = u;
    }
    bool working = crane->core.state_id == S_CRANE_WORK || crane->core.state_id == S_CRANE_WORK2;
    if (best) {
        int step = RF_IsStructure(best) ? 60 : 30;
        best->hp = best->hp + step < best->max_hp ? best->hp + step : best->max_hp;
        if (!working && !P_HasMoveOrder(crane)) P_SetMobjState(crane, S_CRANE_WORK);
    } else if (working) {
        P_SetMobjState(crane, S_CRANE);
    }
}

/* ── the Cantor (7th Legion's priest) ───────────────────────────────────────
 * Each second it mends ten hit points on each of the three most hurt soldiers of its side within
 * three cells, itself among them, and its staff glows. */
static void tick_cantor(mobj_t *cantor, mobj_t *const *mobjs, int count) {
    mobj_t *hurt[3] = {0};
    for (int i = 0; i < count; ++i) {
        mobj_t *u = mobjs[i];
        if (!unit_of_world(u) || u->team != cantor->team || !RF_IsInfantry(u->type_id) || u->hp >= u->max_hp ||
            u->rf_boarded || !near(u, fixed3_xy(cantor->core.position), FIXED_LIT(3.0))) continue;
        for (int k = 0; k < 3; ++k) {
            if (hurt[k] && (int64_t)hurt[k]->hp * u->max_hp <= (int64_t)u->hp * hurt[k]->max_hp) continue;
            for (int m = 2; m > k; --m) hurt[m] = hurt[m - 1];
            hurt[k] = u;
            break;
        }
    }
    if (!hurt[0]) return;
    for (int k = 0; k < 3 && hurt[k]; ++k)
        hurt[k]->hp = hurt[k]->hp + 10 < hurt[k]->max_hp ? hurt[k]->hp + 10 : hurt[k]->max_hp;
    if (cantor->core.state_id == S_LC_CANTOR) P_SetMobjState(cantor, S_LC_CANTOR_FIRE);
}

/* ── the Crusher (KKnD's monster truck) ─────────────────────────────────────
 * Driving, it rolls over every enemy soldier under its wheels. */
static void tick_crusher(mobj_t *crusher, mobj_t *const *mobjs, int count) {
    if (!P_HasMoveOrder(crusher) && crusher->core.momentum.x == 0 && crusher->core.momentum.y == 0) return;
    for (int i = 0; i < count; ++i) {
        mobj_t *u = mobjs[i];
        if (!unit_of_world(u) || u->team == crusher->team || !RF_IsInfantry(u->type_id) || u->rf_boarded ||
            u->allegiance == ALLEGIANCE_NEUTRAL || !near(u, fixed3_xy(crusher->core.position), FIXED_LIT(0.6))) continue;
        P_DamageMobj(u, crusher, u->hp);
    }
}

/* ── Sappers and mines (KKnD's, Dark Reign's) ───────────────────────────────
 * A sapper holding its ground lays a mine every six seconds on its cell, none closer than
 * a cell and a half to another, up to sixteen a side. Mines are hidden from the enemy unless a
 * detector (a turret, a headquarters, a Sentry Post, a Cantor) sees them. An enemy vehicle or
 * soldier rolling over one sets it off: 180 to it, 90 to the rest within a cell and a quarter. */
enum { MINE_TICS = SECONDS(6) };

int RF_MineCount(int owner) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        if (th->function == P_MobjThinker && live(u) && u->type_id == MT_CL_MINE && u->owner == owner) ++n;
    }
    return n;
}

static void tick_sapper(mobj_t *sapper, mobj_t *const *mobjs, int count) {
    if (P_HasMoveOrder(sapper) || sapper->attack.target) { sapper->rf_timer = 0; return; }
    if (++sapper->rf_timer < MINE_TICS) return;
    sapper->rf_timer = 0;
    ivec2_t cell = {sapper->core.position.x >> FIXED_FRAC_BITS, sapper->core.position.y >> FIXED_FRAC_BITS};
    fixed2_t at = fixed2_cell_center(cell);
    if (!L_Contains(&level, cell.x, cell.y) || RF_MineCount(sapper->owner) >= RF_MAX_MINES) return;
    for (int i = 0; i < count; ++i)
        if (live(mobjs[i]) && mobjs[i]->type_id == MT_CL_MINE && near(mobjs[i], at, FIXED_LIT(1.5))) return;
    mobj_t *mine = P_SpawnMobj(fixed3_from_fixed2(at, 0), MT_CL_MINE);
    if (!mine) return;
    mine->owner = sapper->owner;
    mine->team = sapper->team;
    mine->allegiance = sapper->allegiance;
    if (sapper->core.state_id == mobjinfo[sapper->type_id].spawnstate)
        P_SetMobjState(sapper, mobjinfo[sapper->type_id].missilestate + 1); /* a crouch to set it */
}

static void blast_at(mobj_t *source, fixed2_t at, fixed_t radius, int damage, int team, const mobj_t *spare) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        mobj_t *o = (mobj_t *)th;
        if (th->function != P_MobjThinker || o == spare || o == source || !unit_of_world(o) || o->team == team ||
            o->rf_boarded || (o->traits & MF_FLY) || o->allegiance == ALLEGIANCE_NEUTRAL) continue;
        if (near(o, at, reach_to(o, radius))) P_DamageMobj(o, source, RF_ShieldDamage(o, damage));
    }
}

static void tick_mine(mobj_t *mine, mobj_t *const *mobjs, int count) {
    fixed2_t at = fixed3_xy(mine->core.position);
    for (int i = 0; i < count; ++i) {
        mobj_t *u = mobjs[i];
        if (!unit_of_world(u) || u->team == mine->team || !(u->traits & MF_MOBILE) || (u->traits & MF_FLY) ||
            u->rf_boarded || u->allegiance == ALLEGIANCE_NEUTRAL || !near(u, at, FIXED_LIT(0.55))) continue;
        P_DamageMobj(u, mine, RF_ShieldDamage(u, 180));
        blast_at(mine, at, FIXED_LIT(1.25), 90, mine->team, u);
        mobj_t *boom = spawn_fx(MT_FX_BOOM, at, 0);
        if (boom) RF_PlaySound(RF_SND_BOOM_SMALL, boom);
        mine->hp = 0;
        P_RemoveMobj(mine);
        return;
    }
}

/* ── tithe (7th Legion's war chest) ─────────────────────────────────────────
 * The Concord takes a fifth of the price of everything it destroys. */
static int price_of(uint16_t type) {
    if (type == MT_SENTRY) type = MT_SENTRY_TRUCK;
    if (type == MT_CL_DERRICK) type = MT_CL_DRILL;
    const StaticProductDefinition *p = G_ModelProductByClassType(NULL, RTS_PRODUCT_UNIT, type);
    if (!p) p = G_ModelProductByClassType(NULL, RTS_PRODUCT_BUILDING, type);
    return p ? p->cost : 0;
}

int RF_Tithe(const mobj_t *victim, const mobj_t *killer) {
    if (!victim || !killer || killer->owner == victim->owner || killer->team == victim->team ||
        killer->owner >= MAXPLAYERS || RF_FactionOfType(killer->type_id) != RF_CONCORD ||
        victim->type_id >= MT_FX_HIT || victim->type_id == MT_CL_MINE) return 0;
    return price_of(victim->type_id) / 5;
}

/* ── the Rites (7th Legion's power cards) ───────────────────────────────────
 * A Concord side with a Spire draws a card every forty seconds while it holds fewer than three.
 * Played from the sidebar:
 *   Lumen Strike  a pillar of light lands where it is called a second and a half later: 350 to
 *                 every enemy within a cell and a half (aircraft too).
 *   Mending       every unit of its side within three cells gets back half its hit points,
 *                 a structure a quarter.
 *   Windfall      750 credits.
 *   Warding       its side's units within three cells take no damage for eight seconds. */
enum { STRIKE_TICS = 3 * RTS_TICRATE / 2, WARD_TICS = SECONDS(8), MAX_STRIKES = 8 };
typedef struct { uint8_t hand[RF_HAND]; uint16_t draws; int32_t timer_ms; } rites_t;
typedef struct { uint8_t used, owner, team; int16_t tics; fixed2_t at; } strike_t;
static rites_t rites[MAXPLAYERS];
static strike_t strikes[MAX_STRIKES];

void RF_UnitsReset(void) {
    memset(rites, 0, sizeof(rites));
    memset(strikes, 0, sizeof(strikes));
}

static bool has_spire(int owner) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        if (th->function == P_MobjThinker && live(u) && u->owner == owner && u->type_id == MT_LC_SPIRE && !RF_IsSite(u))
            return true;
    }
    return false;
}

rf_rite_t RF_HandCard(int owner, int slot) {
    return owner >= 0 && owner < MAXPLAYERS && slot >= 0 && slot < RF_HAND ? (rf_rite_t)rites[owner].hand[slot] : RF_RITE_NONE;
}

int RF_NextDrawMs(int owner) {
    if (owner < 0 || owner >= MAXPLAYERS || !has_spire(owner)) return -1;
    return RF_RITE_DRAW_MS - rites[owner].timer_ms;
}

bool RF_RiteTargeted(rf_rite_t rite) {
    return rite == RF_RITE_STRIKE || rite == RF_RITE_MENDING || rite == RF_RITE_WARD;
}

const char *RF_RiteName(rf_rite_t rite) {
    static const char *const NAMES[RF_RITE_COUNT] = {"", "STRIKE", "MENDING", "WINDFALL", "WARDING"};
    return rite > RF_RITE_NONE && rite < RF_RITE_COUNT ? NAMES[rite] : "";
}

fixed_t RF_RiteRadius(rf_rite_t rite) {
    return rite == RF_RITE_STRIKE ? FIXED_LIT(1.5) : RF_RiteTargeted(rite) ? FIXED_LIT(3.0) : 0;
}

/* The deck: the same cards, in the same order, on every peer. */
static rf_rite_t deal(int owner, int draw) {
    uint32_t h = (uint32_t)(owner + 1) * 2654435761u ^ (uint32_t)draw * 40503u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    return (rf_rite_t)(RF_RITE_STRIKE + h % (RF_RITE_COUNT - 1));
}

static void tick_rites(void) {
    for (int owner = 0; owner < MAXPLAYERS; ++owner) {
        rites_t *r = &rites[owner];
        int slot = -1;
        for (int i = 0; i < RF_HAND && slot < 0; ++i) if (!r->hand[i]) slot = i;
        if (slot < 0 || !has_spire(owner)) continue;
        r->timer_ms += TIC_MS;
        if (r->timer_ms < RF_RITE_DRAW_MS) continue;
        r->timer_ms = 0;
        r->hand[slot] = (uint8_t)deal(owner, r->draws++);
    }
}

static int team_of(int owner) {
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        if (th->function == P_MobjThinker && live(u) && u->owner == owner && u->type_id < MT_FX_HIT) return u->team;
    }
    return owner;
}

bool RF_PlayRite(int owner, int slot, fixed2_t at) {
    rf_rite_t rite = RF_HandCard(owner, slot);
    if (!rite) return false;
    if (RF_RiteTargeted(rite) &&
        !L_Contains(&level, at.x >> FIXED_FRAC_BITS, at.y >> FIXED_FRAC_BITS)) return false;
    int team = team_of(owner);
    fixed_t radius = RF_RiteRadius(rite);
    switch (rite) {
    case RF_RITE_STRIKE: {
        int free_slot = -1;
        for (int i = 0; i < MAX_STRIKES && free_slot < 0; ++i) if (!strikes[i].used) free_slot = i;
        if (free_slot < 0) return false;
        strikes[free_slot] = (strike_t){1, (uint8_t)owner, (uint8_t)team, STRIKE_TICS, at};
        mobj_t *fx = spawn_fx(MT_FX_ZAP, at, 0);
        if (fx) RF_PlaySound(RF_SND_ZAP_HEAVY, fx);
        break;
    }
    case RF_RITE_MENDING:
    case RF_RITE_WARD:
        for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
            mobj_t *u = (mobj_t *)th;
            if (th->function != P_MobjThinker || !unit_of_world(u) || u->team != team || u->rf_boarded ||
                u->type_id == MT_CL_MINE || !near(u, at, reach_to(u, radius))) continue;
            if (rite == RF_RITE_WARD) {
                if (!RF_IsStructure(u)) u->rf_ward = WARD_TICS;
            } else {
                int gain = RF_IsStructure(u) ? u->max_hp / 4 : u->max_hp / 2;
                u->hp = u->hp + gain < u->max_hp ? u->hp + gain : u->max_hp;
            }
            spawn_fx(MT_FX_ZAP, fixed3_xy(u->core.position), FIXED_LIT(0.3));
        }
        {
            mobj_t *fx = spawn_fx(MT_FX_ZAP, at, 0);
            if (fx) RF_PlaySound(RF_SND_ZAP, fx);
        }
        break;
    case RF_RITE_WINDFALL:
        level.player_resources[owner][0] += 750;
        if (owner == consoleplayer) RF_PlaySound(RF_SND_UNIT_READY, NULL);
        break;
    default:
        return false;
    }
    rites[owner].hand[slot] = RF_RITE_NONE;
    return true;
}

bool RF_RiteOrder(int slot, fixed2_t at) {
    if (!RF_HandCard(consoleplayer, slot)) return false;
    ticcmd_t cmd = {.order = TC_SPELL, .product = slot, .position = fixed3_from_fixed2(at, 0)};
    return G_QueueTiccmd(&cmd);
}

/* TC_SPELL with no units: the player plays a card from its hand. */
bool G_GameCommand(int player, const ticcmd_t *cmd) {
    if (cmd->order != TC_SPELL || cmd->count != 0) return false;
    if (cmd->product >= 0 && cmd->product < RF_HAND) RF_PlayRite(player, cmd->product, fixed3_xy(cmd->position));
    return true;
}

static void tick_strikes(void) {
    for (int i = 0; i < MAX_STRIKES; ++i) {
        strike_t *s = &strikes[i];
        if (!s->used) continue;
        if (--s->tics > 0) {
            if (s->tics % 6 == 0) spawn_fx(MT_FX_ZAP, s->at, 0);
            continue;
        }
        s->used = 0;
        for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
            mobj_t *o = (mobj_t *)th;
            if (th->function != P_MobjThinker || !unit_of_world(o) || o->team == s->team || o->rf_boarded ||
                o->allegiance == ALLEGIANCE_NEUTRAL || !near(o, s->at, reach_to(o, FIXED_LIT(1.5)))) continue;
            P_DamageMobj(o, NULL, RF_ShieldDamage(o, 350));
        }
        mobj_t *boom = spawn_fx(MT_FX_BIGBOOM, s->at, 0);
        if (boom) RF_PlaySound(RF_SND_BOOM_LARGE, boom);
    }
}

/* ── the save and the lockstep hash ─────────────────────────────────────── */
size_t RF_UnitsSaveSize(void) {
    return sizeof(rites) + sizeof(strikes);
}

void RF_UnitsSave(void *out) {
    memcpy(out, rites, sizeof(rites));
    memcpy((uint8_t *)out + sizeof(rites), strikes, sizeof(strikes));
}

bool RF_UnitsLoad(const void *data, size_t size) {
    if (size < RF_UnitsSaveSize()) return false;
    memcpy(rites, data, sizeof(rites));
    memcpy(strikes, (const uint8_t *)data + sizeof(rites), sizeof(strikes));
    return true;
}

uint32_t RF_UnitsHash(uint32_t hash) {
    for (int o = 0; o < MAXPLAYERS; ++o) {
        hash = G_HashValue(hash, rites[o].hand[0] | rites[o].hand[1] << 8 | rites[o].hand[2] << 16);
        hash = G_HashValue(hash, rites[o].draws);
        hash = G_HashValue(hash, (uint32_t)rites[o].timer_ms);
    }
    for (int i = 0; i < MAX_STRIKES; ++i) {
        if (!strikes[i].used) continue;
        hash = G_HashValue(hash, (uint32_t)strikes[i].tics | (uint32_t)strikes[i].owner << 16);
        hash = G_HashValue(hash, (uint32_t)strikes[i].at.x);
        hash = G_HashValue(hash, (uint32_t)strikes[i].at.y);
    }
    return hash;
}

/* ── each tic ───────────────────────────────────────────────────────────── */
void RF_UnitsTicker(level_t *map, mobj_t *const *mobjs, int count) {
    (void)map;
    bool second = leveltime % RTS_TICRATE == 0;
    for (int i = 0; i < count; ++i) {
        mobj_t *u = mobjs[i];
        if (!unit_of_world(u)) continue;
        if (u->rf_carrier) tick_boarding(u);
        if (u->rf_boarded) continue;
        tick_phase(u);
        /* Aircraft draw over everything on the ground (the renderer sorts by height first). */
        if (u->traits & MF_FLY) u->core.position.z = FIXED_ONE / 64;
        switch (u->type_id) {
        case MT_CRANE: if (second) tick_crane(u, mobjs, count); break;
        case MT_LC_CANTOR: if (second) tick_cantor(u, mobjs, count); break;
        case MT_CL_CRUSHER: tick_crusher(u, mobjs, count); break;
        case MT_CL_SAPPER: tick_sapper(u, mobjs, count); break;
        case MT_CL_MINE: tick_mine(u, mobjs, count); break;
        default: break;
        }
    }
    tick_rites();
    tick_strikes();
}

/* An APC's passengers bail out of the wreck, hurt; with nowhere to stand, they die in it. */
void RF_UnitLost(mobj_t *victim) {
    if (!RF_IsTransport(victim)) return;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (th->function != P_MobjThinker || !live(u) || !u->rf_boarded || u->rf_carrier != victim->id) continue;
        fixed2_t at;
        bool room = free_spot(u, victim, &at);
        disembark(u, room ? at : fixed3_xy(victim->core.position));
        if (room) u->hp = u->hp / 2 > 0 ? u->hp / 2 : 1;
        else P_DamageMobj(u, NULL, u->hp);
    }
}

/* ── orders: board, unload, deploy ──────────────────────────────────────── */
bool RF_UnitCommand(const ticcmd_t *cmd, mobj_t *const *units, int count, mobj_t *target) {
    switch (cmd->order) {
    case TC_BOARD:
        for (int i = 0; i < count; ++i) RF_BoardOrder(units[i], target);
        return true;
    case TC_UNLOAD:
        for (int i = 0; i < count; ++i) RF_Unload(units[i]);
        return true;
    case TC_DEPLOY:
        for (int i = 0; i < count; ++i) RF_Deploy(units[i]);
        return true;
    default:
        return false;
    }
}

/* ── the computer player ────────────────────────────────────────────────── */

/* Enemies of `team` within `reach` of `at`, structures counting double. */
static int crowd(mobj_t *const *units, int count, fixed2_t at, fixed_t reach, int team) {
    int n = 0;
    for (int i = 0; i < count; ++i) {
        const mobj_t *o = units[i];
        if (!unit_of_world(o) || o->team == team || o->rf_boarded || o->allegiance == ALLEGIANCE_NEUTRAL ||
            o->type_id == MT_CL_MINE || !near(o, at, reach_to(o, reach))) continue;
        n += RF_IsStructure(o) ? 2 : 1;
    }
    return n;
}

/* Deploy idle trucks and rigs where they stand; play the cards: Windfall at once, Strike on the
 * thickest enemy crowd near its own units, Mending and Warding where its own are hurting. */
void RF_AiUnits(int owner, mobj_t *const *units, int count) {
    int team = team_of(owner);
    for (int i = 0; i < count; ++i) {
        mobj_t *u = units[i];
        if (!unit_of_world(u) || u->owner != owner || P_HasMoveOrder(u)) continue;
        if (u->type_id == MT_SENTRY_TRUCK || u->type_id == MT_CL_DRILL) RF_Deploy(u);
    }
    for (int slot = 0; slot < RF_HAND; ++slot) {
        rf_rite_t rite = RF_HandCard(owner, slot);
        if (rite == RF_RITE_WINDFALL) { RF_PlayRite(owner, slot, (fixed2_t){0}); continue; }
        if (!rite) continue;
        fixed2_t best = {0};
        int best_score = 0;
        for (int i = 0; i < count; ++i) {
            const mobj_t *u = units[i];
            if (!unit_of_world(u) || u->rf_boarded || u->type_id == MT_CL_MINE) continue;
            fixed2_t at = fixed3_xy(u->core.position);
            int score = 0;
            if (rite == RF_RITE_STRIKE) {
                if (u->team == team || u->allegiance == ALLEGIANCE_NEUTRAL) continue;
                /* within sight of its own army, as a player would see it */
                if (crowd(units, count, at, FIXED_LIT(6), u->team) == 0) continue;
                score = crowd(units, count, at, FIXED_LIT(1.5), team);
                if (score < 4) continue;
            } else {
                if (u->owner != owner || RF_IsStructure(u)) continue;
                int missing = 0, threatened = crowd(units, count, at, FIXED_LIT(5), team);
                for (int j = 0; j < count; ++j) {
                    const mobj_t *f = units[j];
                    if (unit_of_world(f) && f->team == team && !RF_IsStructure(f) && !f->rf_boarded &&
                        f->type_id != MT_CL_MINE && near(f, at, FIXED_LIT(3))) missing += f->max_hp - f->hp;
                }
                score = rite == RF_RITE_MENDING ? (missing >= 700 ? missing : 0)
                                                : (threatened >= 5 && missing >= 300 ? threatened : 0);
            }
            if (score > best_score) { best_score = score; best = at; }
        }
        if (best_score > 0) RF_PlayRite(owner, slot, best);
    }
}
