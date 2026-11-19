#include "engine.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

/* Melee and missile hits, Stratagus' damage roll, research bonuses, and
 * what happens to the ground when a building falls. */

static uint32_t w2_rng = 0x9E3779B9u;

/* The gameplay roll must match on every peer, so it is a plain xorshift
 * seeded with the level, never the presentation RNG. */
void W2_SeedCombat(uint32_t seed) {
    w2_rng = seed ? seed : 0x9E3779B9u;
}

uint32_t W2_SyncRand(void) {
    uint32_t x = w2_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    w2_rng = x;
    return x;
}

static int research_level(const mobj_t *unit, bool armor) {
    if (!unit || unit->owner >= 8 || unit->type_id == 0 || unit->type_id >= NUMMOBJTYPES) return 0;
    return armor ? level.upgrades[unit->type_id][unit->owner].armor :
                   level.upgrades[unit->type_id][unit->owner].weapon;
}

/* Sum of the bonuses of every researched tier that applies to the type. */
static int research_bonus(const mobj_t *unit, bool armor) {
    int tier = research_level(unit, armor), bonus = 0;
    for (int id = 1; id < W2_UPGRADE_COUNT; ++id) {
        const w2_upgrade_t *upgrade = W2_Upgrade(id);
        if (!upgrade || upgrade->armor != armor || upgrade->tier > tier) continue;
        for (size_t i = 0; i < sizeof(upgrade->units) / sizeof(*upgrade->units) && upgrade->units[i]; ++i)
            if (upgrade->units[i] == unit->type_id) { bonus += upgrade->bonus; break; }
    }
    return bonus;
}

int W2_PiercingDamage(const mobj_t *unit) {
    if (!unit || unit->type_id == 0 || unit->type_id >= NUMMOBJTYPES) return 0;
    int bonus = (unit->type_id == MT_ARCHER || unit->type_id == MT_RANGER) &&
                W2_HasResearch(unit->owner, W2_UPGRADE_MARKSMANSHIP) ? 3 : 0;
    return mobjinfo[unit->type_id].w2.piercing_damage + research_bonus(unit, false) + bonus;
}

int W2_Armor(const mobj_t *unit) {
    if (!unit || unit->type_id == 0 || unit->type_id >= NUMMOBJTYPES) return 0;
    return mobjinfo[unit->type_id].w2.armor + research_bonus(unit, true);
}

/* Stratagus CalculateDamageStats: armor eats basic damage down to one,
 * piercing damage always lands, and the roll takes back up to half. */
int W2_AttackDamage(const mobj_t *attacker, const mobj_t *target, uint32_t roll) {
    if (!attacker || !target || attacker->type_id == 0 || attacker->type_id >= NUMMOBJTYPES) return 0;
    int basic = mobjinfo[attacker->type_id].w2.basic_damage;
    int piercing = W2_PiercingDamage(attacker);
    if (attacker->w2.buffs[W2_BUFF_BLOODLUST]) { basic *= 2; piercing *= 2; }
    int damage = basic - W2_Armor(target);
    if (damage < 1) damage = 1;
    damage += piercing;
    damage -= (int)(roll % (uint32_t)((damage + 2) / 2));
    return damage < 1 ? 1 : damage;
}

/* Siege, ships and towers whose weapon splashes: Wargus lists them for
 * attack-ground. A melee blow or an arrow has no point to land on. */
bool W2_CanAttackGround(const mobj_t *unit) {
    if (!unit || unit->remove || unit->hp <= 0 || !(unit->traits & MF_ATTACK) ||
        unit->type_id == 0 || unit->type_id >= NUMMOBJTYPES || W2_UnderConstruction(unit)) return false;
    int shot = mobjinfo[unit->type_id].w2.projectile;
    return shot > W2_FX_NONE && shot < W2_FX_COUNT && w2_effects[shot].range > 0;
}

/* A wall tile is attackable by any fighter (Stratagus DoRightButton_Attack). */
static bool can_ground(const mobj_t *unit, fixed3_t point) {
    if (W2_CanAttackGround(unit)) return true;
    return unit && !unit->remove && unit->hp > 0 && (unit->traits & MF_ATTACK) &&
           unit->type_id > 0 && unit->type_id < NUMMOBJTYPES && !W2_UnderConstruction(unit) &&
           W2_WallAt(fixed2_cell(fixed3_xy(point)));
}

/* Reach is whole tiles, as the unit's attack range and minimum are. */
static bool ground_in_reach(const mobj_t *unit, fixed3_t point) {
    ivec2_t from = fixed2_cell(fixed3_xy(unit->core.position));
    ivec2_t to = fixed2_cell(fixed3_xy(point));
    int dx = abs(from.x - to.x), dy = abs(from.y - to.y);
    int distance = dx > dy ? dx : dy;
    return distance <= fixed_floor_int(W2_AttackRange(unit)) &&
           distance >= mobjinfo[unit->type_id].w2.min_attack_range;
}

/* A new order drops whatever the unit was doing, as TC_ATTACK does, and
 * stops it chasing or picking up units: it fires at the point only. */
bool W2_GroundOrder(mobj_t *unit, fixed3_t point) {
    if (!can_ground(unit, point)) return false;
    W2_InterruptRepair(unit);
    W2_InterruptHarvest(unit);
    W2_InterruptBuild(unit);
    unit->w2.carrier = 0;
    unit->w2.unloading = false;
    unit->w2.stand_ground = false;
    unit->w2.cast.spell = 0;
    unit->waypoints = (waypoints_t){0};
    unit->attack.target = NULL;
    unit->harvest.target = -1;
    unit->harvest.base = NULL;
    unit->harvest.phase = 0;
    unit->harvest.timer_ms = 0;
    P_ClearMove(unit);
    unit->w2.ground.active = true;
    unit->w2.ground.position = point;
    return true;
}

/* The shot itself: the release frame of the attack row, once, then idle. */
static void fire_ground(mobj_t *unit) {
    fixed3_t point = unit->w2.ground.position;
    bool reached = ground_in_reach(unit, point);
    unit->w2.ground.active = false;
    if (!reached) return;
    S_ActorSound(unit, SE_ATTACK);
    unit->w2.buffs[W2_BUFF_INVISIBLE] = 0;
    /* A melee blow has no flight: Stratagus Missile-none hits the wall
     * with CalculateDamageStats at once. */
    if (!w2_fire_ground(unit, point) || !mobjinfo[unit->type_id].w2.projectile) {
        ivec2_t cell = fixed2_cell(fixed3_xy(point));
        int basic = mobjinfo[unit->type_id].w2.basic_damage, piercing = W2_PiercingDamage(unit);
        if (unit->w2.buffs[W2_BUFF_BLOODLUST]) { basic *= 2; piercing *= 2; }
        if (W2_WallAt(cell)) W2_HitWall(cell, W2_WallDamage(unit, basic, piercing, W2_SyncRand()));
    }
    /* A wall order lasts until the wall falls (Stratagus attack-tile order); only
     * a splash weapon's attack-ground is a single shot. */
    if (!W2_CanAttackGround(unit)) unit->w2.ground.active = W2_WallAt(fixed2_cell(fixed3_xy(point)));
}

/* Walk until the point is in reach, then enter the attack row. A unit that
 * cannot move to it, or a point it cannot see, gives the order up. */
void W2_TickGroundAttack(mobj_t *unit) {
    if (!unit->w2.ground.active) return;
    if (!can_ground(unit, unit->w2.ground.position)) { unit->w2.ground.active = false; return; }
    if (states[unit->core.state_id].group == W2_GROUP_ATTACK) return;
    fixed3_t point = unit->w2.ground.position;
    if (ground_in_reach(unit, point)) {
        int attack = mobjinfo[unit->type_id].missilestate;
        if (!attack) { unit->w2.ground.active = false; return; }
        P_ClearMove(unit);
        fixed2_t facing = fixed2_sub(fixed3_xy(point), fixed3_xy(unit->core.position));
        unit->core.angle = P_PointToAngle(facing.x, facing.y);
        P_SetMobjState(unit, attack);
        return;
    }
    if (P_HasMoveOrder(unit)) return;
    if (!(unit->traits & MF_MOBILE)) { unit->w2.ground.active = false; return; }
    if (P_MoveUnitTo(&level, unit, fixed3_xy(point))) return;
    /* A wall cell is solid: walk to a free cell beside it instead. */
    ivec2_t wall = fixed2_cell(fixed3_xy(point));
    static const ivec2_t around[8] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    if (W2_WallAt(wall))
        for (int i = 0; i < 8; ++i) {
            ivec2_t cell = ivec2_add(wall, around[i]);
            if (L_Contains(&level, cell.x, cell.y) && !level.blocked[L_Index(&level, cell.x, cell.y)] &&
                P_MoveUnitTo(&level, unit, fixed2_cell_center(cell))) return;
        }
    unit->w2.ground.active = false;
}

/* The blow of the attack row: the target may have moved since the windup. */
void A_W2_Attack(mobj_t *unit) {
    if (unit && unit->w2.cast.spell) { A_W2_Cast(unit); return; }
    if (unit && unit->w2.ground.active) { fire_ground(unit); return; }
    mobj_t *target = unit ? unit->attack.target : NULL;
    if (!target || target->remove || target->hp <= 0 || unit->hp <= 0 ||
        !P_CanTarget(unit, target) || !P_InAttackRange(unit, target)) return;
    if (W2_CanCast(unit, W2_SPELL_DEMOLISH)) {
        unit->w2.cast.spell = W2_SPELL_DEMOLISH;
        unit->w2.cast.position = unit->core.position;
        A_W2_Cast(unit);
        return;
    }
    S_ActorSound(unit, SE_ATTACK);
    unit->w2.buffs[W2_BUFF_INVISIBLE] = 0;
    if (w2_fire_projectile(unit, target)) return;
    P_DamageMobj(target, unit, W2_AttackDamage(unit, target, W2_SyncRand()));
}

/* A destroyed building stops blocking its cells. */
void A_W2_Collapse(mobj_t *unit) {
    if (!unit || unit->type_id == 0 || unit->type_id >= NUMMOBJTYPES) return;
    isize2_t foot = mobjinfo[unit->type_id].w2.footprint;
    if (foot.w <= 0 || foot.h <= 0 || !level.cell_solid) return;
    ivec2_t corner = fixed2_footprint_corner(fixed3_xy(unit->core.position), foot);
    w2_clear_footprint(corner.x, corner.y, foot);
    W2_RestoreOilPatch(unit);
}

/* Saved games keep the combat dice (m_menu.c writes them with the campaign). */
uint32_t W2_CombatState(void) { return w2_rng; }
void W2_SetCombatState(uint32_t state) { w2_rng = state; }
