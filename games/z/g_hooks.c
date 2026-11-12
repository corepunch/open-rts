#include "z_local.h"
#include "info.h"

uint32_t G_SaveLayout(void) { return 24; }

/* 0x63900..0x63917 installs the boarding robot's range on an APC. */
fixed_t Z_AttackRangeBonus(const mobj_t *unit, const weapondef_t *weapon) {
    if (!unit || !weapon || unit->type_id != MT_UNIT_APC || !unit->z.driver ||
        !Z_IsRobot(unit->z.driver - 1)) return 0;
    const mobjtype_t *driver = P_ActorType(MT_UNITS + unit->z.driver - 1);
    return driver ? driver->attack.range - weapon->range : 0;
}

/* Z's share of the engine's game hooks: its rules run once a tic after the
 * thinkers, an order onto an empty vehicle or gun sends robots in, and a
 * factory's STOP empties its queue. */

void G_ClockEnd(int64_t before, int64_t clock) {
    Z_SquadCooldownTick();
    Z_Ticker();
    if (clock != before && (clock & 3) == 0) P_UpdateSight();
}

/* 0x5969a records the projectile's owner in the struck building's territory. */
void G_UnitDamaged(mobj_t *victim, const mobj_t *source, int old_hp) {
    Z_BodyDamaged(victim, old_hp);
    if (old_hp > 0 && victim->hp <= 0) {
        Z_SquadLeave(victim);
        Z_DetourCancel(victim);
        victim->z.route = (zroute_t){0};
        if (victim->z.building) Z_BuildingDestroyed(victim);
    }
    zmap_t *map = level.native_data;
    if (!map || netgame || !source || source->team != 0 || victim->team != 1 ||
        victim->type_id < MT_FORT || victim->type_id >= MT_UNITS) return;
    ivec2_t origin = victim->type_id == MT_FORT ? map->forts[0] : Z_BuildingOrigin(victim);
    fixed2_t at = fixed2_scale(fixed2_from_cell(origin), FIXED_ONE / Z_TILE);
    int t = Z_AITerritory(map, at);
    if (t >= 0) map->ai.territories[t].attacker = source->id;
}

/* The vehicle firing path 0x58bb8 reloads from mutable body +0x20.
 * Other attack paths use the unit's own +0x75 instead. */
int G_WeaponCooldown(const mobj_t *unit, const weapondef_t *weapon, int cooldown_ms) {
    (void)weapon;
    if (unit->type_id == MT_UNIT_APC && unit->z.driver && Z_IsRobot(unit->z.driver - 1)) {
        const mobjtype_t *driver = P_ActorType(MT_UNITS + unit->z.driver - 1);
        if (driver && driver->attack.cooldown_ms > 0) {
            int steps = (driver->attack.cooldown_ms * Z_NATIVE_HZ + 999) / 1000;
            return (steps + steps / 2) * 1000 / Z_NATIVE_HZ;
        }
    }
    if (!unit->z.body_tracked) return cooldown_ms;
    switch (unit->type_id) {
    case MT_UNIT_LIGHT_TANK: case MT_UNIT_MED_TANK: case MT_UNIT_HEAVY_TANK: case MT_UNIT_M_MISSILE:
        return (100 - unit->z.body_fire) * 1000 / Z_NATIVE_HZ;
    default: return cooldown_ms;
    }
}

bool G_UnitCommandable(const mobj_t *unit) {
    return unit->z.service_work >= 0;
}

bool G_GameUnitCommand(int player, const ticcmd_t *cmd, mobj_t *const *units,
                       int count, mobj_t *target) {
    if (Z_RouteCommand(player, cmd, units, count)) return true;
    if (cmd->order == TC_ORDER && (Z_CliffOrder(cmd, units, count) || Z_BridgeOrder(cmd, units, count))) return true;
    switch (cmd->order) {
    case TC_UNLOAD:
        for (int i = 0; i < count; ++i) Z_EjectCrew(units[i]);
        return true;
    case TC_ATTACK:
        return Z_BoardOrder(units, count, target);
    case TC_CONSTRUCT:
        Z_PlaceGun(units[0], cmd->product,
                   (ivec2_t){fixed_floor_int(cmd->position.x), fixed_floor_int(cmd->position.y)});
        return true;
    case TC_CANCEL_PRODUCTION:
        for (int i = 0; i < count; ++i)
            while (units[i]->production && units[i]->production->queue_count > 0)
                G_RemoveProductionAt(units[i], 0);
        return true;
    default:
        return false;
    }
}

/* Any other order calls a robot off the vehicle it was walking to. */
void G_InterruptOrders(const ticcmd_t *cmd, mobj_t *const *units, int count, bool late) {
    bool interrupts = late ? cmd->order == TC_ORDER :
        cmd->order == TC_STOP || cmd->order == TC_MOVE || cmd->order == TC_ATTACK ||
        cmd->order == TC_PATH || cmd->order == TC_WAYPOINT;
    if (!interrupts) return;
    for (int i = 0; i < count; ++i) {
        units[i]->z.board = 0;
        Z_CancelService(units[i]);
    }
}
