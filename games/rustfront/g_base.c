#include "engine.h"
#include "info.h"
#include "rustfront.h"

/* Running a base, after C&C: power, repairing and selling structures, rally points, and
 * veterancy. Everything here is simulation state (mobj fields in mobj_data.h, hashed by
 * MOBJ_GAME_CHECKSUM) or derived from it each tic, and every order comes through a ticcmd, so
 * every peer agrees. */

/* ── power ──────────────────────────────────────────────────────────────────
 * Each finished structure makes (+) or draws (−) power. A side drawing more than it makes is
 * on low power: its production and construction run at half speed and its defences fire at
 * half rate until it builds more power or sells something. Sites neither make nor draw. */
static const struct { uint16_t type; int16_t power; } POWER[] = {
    {MT_HQ, 30}, {MT_POWER, 100}, {MT_REFINERY, -30}, {MT_BARRACKS, -20}, {MT_FACTORY, -40},
    {MT_GUN_TURRET, -15},
    {MT_CL_HALL, 30}, {MT_CL_GENERATOR, 80}, {MT_CL_SMELTER, -25}, {MT_CL_CAMP, -15}, {MT_CL_YARD, -35},
    {MT_CL_TOWER, -10},
    {MT_LC_SPIRE, 30}, {MT_LC_FUSION, 150}, {MT_LC_EXTRACTOR, -30}, {MT_LC_ACADEMY, -25},
    {MT_LC_FOUNDRY, -50}, {MT_LC_TOWER, -30},
    {MT_UPLINK, -50}, {MT_CL_SHOP, -40}, {MT_LC_SANCTUM, -60},
};

int RF_PowerOf(uint16_t type) {
    for (size_t i = 0; i < sizeof(POWER) / sizeof(*POWER); ++i)
        if (POWER[i].type == type) return POWER[i].power;
    return 0;
}

static bool live(const mobj_t *unit) {
    return unit && !unit->remove && unit->hp > 0;
}

bool RF_IsStructure(const mobj_t *unit) {
    return unit && unit->info && unit->info->footprint.w > 0 && !(unit->traits & MF_MOBILE) &&
           unit->type_id < MT_FX_HIT;
}

void RF_OwnerPower(int owner, int *made, int *used) {
    int m = 0, u = 0;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        const mobj_t *unit = (const mobj_t *)th;
        if (th->function != P_MobjThinker || unit->owner != owner || !live(unit) || RF_IsSite(unit)) continue;
        int p = RF_PowerOf(unit->type_id);
        if (p > 0) m += p; else u -= p;
    }
    if (made) *made = m;
    if (used) *used = u;
}

bool RF_LowPower(int owner) {
    int made, used;
    RF_OwnerPower(owner, &made, &used);
    return used > made;
}

/* Half speed: give back half of each tic's progress to a low side's queues before the shared
 * ticker counts them down (G_ProductionBegin runs at the top of G_ProductionTicker). */
void G_ProductionBegin(int dt_ms) {
    bool low[MAXPLAYERS] = {0}, known[MAXPLAYERS] = {0};
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next) {
        mobj_t *unit = (mobj_t *)th;
        production_t *queue = unit->production;
        if (th->function != P_MobjThinker || !queue || queue->queue_count <= 0 || queue->release_active ||
            !live(unit) || unit->owner >= MAXPLAYERS) continue;
        if (!known[unit->owner]) { low[unit->owner] = RF_LowPower(unit->owner); known[unit->owner] = true; }
        if (low[unit->owner]) queue->time_left_ms += dt_ms / 2;
    }
}

/* A defence on low power fires at half rate; a veteran reloads a little quicker. */
int G_WeaponCooldown(const mobj_t *unit, const weapondef_t *weapon, int cooldown_ms) {
    (void)weapon;
    if (RF_IsStructure(unit) && RF_LowPower(unit->owner)) cooldown_ms *= 2;
    return cooldown_ms;
}

/* ── veterancy ──────────────────────────────────────────────────────────────
 * Kills make a unit a veteran (3) and then elite (6). A veteran does a fifth more damage, an
 * elite two fifths more and mends itself slowly. Chevrons over the unit show the rank. */
int RF_Rank(const mobj_t *unit) {
    if (!unit) return 0;
    return unit->rf_kills >= RF_ELITE_KILLS ? 2 : unit->rf_kills >= RF_VETERAN_KILLS ? 1 : 0;
}

void RF_CreditKill(mobj_t *victim, mobj_t *killer) {
    if (!victim || !killer || killer == victim || killer->remove || killer->owner == victim->owner ||
        victim->type_id >= MT_FX_HIT || killer->type_id >= MT_FX_HIT) return;
    if (killer->owner < MAXPLAYERS) level.player_resources[killer->owner][0] += RF_Tithe(victim, killer);
    if (killer->rf_kills < 60000 && victim->type_id != MT_CL_MINE) killer->rf_kills++;
}

int RF_RankDamage(const mobj_t *attacker, int damage) {
    int rank = RF_Rank(attacker);
    return damage + damage * rank / 5;
}

/* gameinfo_t.hit_damage, for weapons that hit at once; projectiles take the rank when they are
 * fired and the shield where they land (g_combat.c). */
int RF_HitDamage(const mobj_t *attacker, const weapondef_t *weapon, mobj_t *target, int damage, int divisor) {
    (void)weapon;
    if (RF_Phased(target)) return 0;
    damage = RF_ShieldDamage(target, RF_RankDamage(attacker, damage) / (divisor > 0 ? divisor : 1));
    return damage > 0 ? damage : 1;
}

/* ── repair and sell ────────────────────────────────────────────────────────
 * Repairing mends a twentieth of a structure's hit points a second, paying half of what those
 * hit points cost to build, and stops when it is whole or the side runs out of credits. Selling
 * gives back half the structure's price, scaled by its health, and takes it away. */
static int price_of(uint16_t type) {
    const StaticProductDefinition *p = G_ModelProductByClassType(NULL, RTS_PRODUCT_BUILDING, type);
    return p ? p->cost : 0;
}

static void repair_step(mobj_t *unit) {
    if (!unit->rf_repairing) return;
    if (!live(unit) || RF_IsSite(unit) || unit->hp >= unit->max_hp) { unit->rf_repairing = 0; return; }
    int step = unit->max_hp / 40 > 0 ? unit->max_hp / 40 : 1; /* every half second */
    if (step > unit->max_hp - unit->hp) step = unit->max_hp - unit->hp;
    int cost = (int)((int64_t)price_of(unit->type_id) * step / unit->max_hp / 2);
    if (cost < 1) cost = 1;
    int *credits = &level.player_resources[unit->owner][0];
    if (*credits < cost) { unit->rf_repairing = 0; return; }
    *credits -= cost;
    unit->hp += step;
    if (unit->hp >= unit->max_hp) unit->rf_repairing = 0;
}

int RF_SellValue(const mobj_t *unit) {
    if (!RF_IsStructure(unit) || unit->max_hp <= 0) return 0;
    int price = price_of(unit->type_id);
    if (RF_IsSite(unit) && unit->production) price = price * unit->rf_built / 1000; /* what went in */
    return (int)((int64_t)price * unit->hp / unit->max_hp / 2);
}

static void sell(mobj_t *unit) {
    if (!RF_IsStructure(unit) || !live(unit)) return;
    level.player_resources[unit->owner][0] += RF_SellValue(unit);
    G_UnitKilled(unit, NULL); /* it comes down in a cloud of dust and debris */
    unit->hp = 0;
    P_RemoveMobj(unit);
    RF_SyncBlocking(&level);
}

/* ── rally points ───────────────────────────────────────────────────────── */
static bool makes_units(uint16_t type) {
    StaticProductDefinition products[96];
    int count = G_ModelGetProducts(NULL, -1, products, 96);
    for (int i = 0; i < count; ++i)
        if (products[i].product_class == RTS_PRODUCT_UNIT)
            for (int j = 0; j < products[i].maker_count; ++j)
                if (products[i].makers[j] == type) return true;
    return false;
}

bool RF_HasRally(const mobj_t *producer) { return producer && producer->rf_rally_set; }
fixed2_t RF_Rally(const mobj_t *producer) { return producer->rf_rally; }

void RF_UnitReleased(mobj_t *unit, const mobj_t *producer) {
    if (!unit || !RF_HasRally(producer) || !(unit->traits & MF_MOBILE) || (unit->traits & MF_HARVESTER)) return;
    P_MoveUnitTo(&level, unit, producer->rf_rally);
}

/* ── orders ─────────────────────────────────────────────────────────────────
 * A move order given to structures sets the rally point of those that make units; TC_REPAIR
 * carries RF_ORDER_REPAIR (toggle) or RF_ORDER_SELL in its product. */
bool G_GameUnitCommand(int player, const ticcmd_t *cmd, mobj_t *const *units, int count, mobj_t *target) {
    (void)player;
    if (RF_UnitCommand(cmd, units, count, target)) return true; /* board, unload, deploy (g_units.c) */
    if (cmd->order == TC_REPAIR) {
        bool on = false;
        for (int i = 0; i < count; ++i) on |= RF_IsStructure(units[i]) && !units[i]->rf_repairing &&
                                               units[i]->hp < units[i]->max_hp;
        for (int i = 0; i < count; ++i) {
            if (!RF_IsStructure(units[i])) continue;
            if (cmd->product == RF_ORDER_SELL) sell(units[i]);
            else units[i]->rf_repairing = on && units[i]->hp < units[i]->max_hp && !RF_IsSite(units[i]);
        }
        return true;
    }
    if (cmd->order != TC_ORDER && cmd->order != TC_MOVE && cmd->order != TC_SET_RALLY) return false;
    bool structures_only = true;
    for (int i = 0; i < count; ++i) structures_only &= RF_IsStructure(units[i]);
    if (!structures_only) return false;
    fixed2_t at = fixed3_xy(cmd->position);
    if (!L_Contains(&level, at.x >> FIXED_FRAC_BITS, at.y >> FIXED_FRAC_BITS)) return true;
    for (int i = 0; i < count; ++i)
        if (makes_units(units[i]->type_id)) {
            units[i]->rf_rally = at;
            units[i]->rf_rally_set = true;
        }
    return true;
}

bool RF_StructureOrder(int product) {
    mobjlist_t list = P_ListMobjs();
    ticcmd_t cmd = {.order = TC_REPAIR, .product = product};
    for (int i = 0; i < list.count && cmd.count < MAXCOMMANDUNITS; ++i) {
        const mobj_t *unit = list.items[i];
        if (unit->owner == consoleplayer && live(unit) && P_MobjIsSelected(unit) && RF_IsStructure(unit))
            cmd.units[cmd.count++] = unit->id;
    }
    P_FreeMobjList(&list);
    return cmd.count > 0 && G_QueueTiccmd(&cmd);
}

/* ── each tic ───────────────────────────────────────────────────────────── */
void RF_BaseTicker(mobj_t *const *mobjs, int count) {
    bool half_second = leveltime % (RTS_TICRATE / 2) == 0, second = leveltime % RTS_TICRATE == 0;
    for (int i = 0; i < count; ++i) {
        mobj_t *unit = mobjs[i];
        if (!live(unit) || unit->type_id >= MT_FX_HIT) continue;
        if (half_second) repair_step(unit);
        /* Elite units mend a hundredth of their hit points a second. */
        if (second && RF_Rank(unit) == 2 && unit->hp < unit->max_hp) {
            unit->hp += unit->max_hp / 100 > 0 ? unit->max_hp / 100 : 1;
            if (unit->hp > unit->max_hp) unit->hp = unit->max_hp;
        }
    }
}
