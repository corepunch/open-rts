#include "sc_local.h"

/* StarCraft's ruleset. A race's computer player is in two parts, as
 * Blizzard's melee AI has them: an opening of build/train lines (aiscript
 * TMCu/ZMCu/PMCu) and a doctrine that says, in numbers, what the race is
 * good at. The shared AI turns the doctrine into supply, workers, defenses,
 * expansions, research, an army mix bent toward what it scouts, and the call
 * of when to attack or fall back. */

#define OPENING(steps) .opening = steps, .opening_count = (int)(sizeof(steps) / sizeof(*steps))
#define SC_UPGRADE_STEP(upgrade, level) (SC_UPGRADE_UI + (upgrade) * 4 + (level) - 1)
/* What a melee start spawns, in pixels from the start location: the town hall
 * is centred on it and covers 128 by 96, the workers stand in the row below,
 * and Zerg also start with an Overlord above. */
static const startunit_t zerg_start[] = {
    {MT_HATCHERY, 1, {0, 0}, {0, 0}}, {MT_DRONE, 4, {48, 64}, {24, 0}}, {MT_OVERLORD, 1, {0, -80}, {0, 0}},
};
static const startunit_t terran_start[] = {
    {MT_COMMAND_CENTER, 1, {0, 0}, {0, 0}}, {MT_SCV, 4, {48, 64}, {24, 0}},
};
static const startunit_t protoss_start[] = {
    {MT_NEXUS, 1, {0, 0}, {0, 0}}, {MT_PROBE, 4, {48, 64}, {24, 0}},
};
#define START(units) .start_units = units, .start_unit_count = (int)(sizeof(units) / sizeof(*units))

/* The openings follow the retail melee scripts line by line (build and
 * train lines of TMCu, ZMCu and PMCu, test_ai checks the order), then
 * reach the buildings the roster needs. Supply, workers past the opening,
 * static defense, expansions and the army are the doctrine's. */
/* Terran: turtles and pushes. A bunkered marine opening into tanks;
 * turrets and bunkers hold the base, the army leaves only with a clear
 * edge and backs off before it is traded away. Expands late. */
static const AiStep terran_opening[] = {
    {MT_SCV,7},{MT_BARRACKS,1},{MT_SCV,8},{MT_SUPPLY_DEPOT,1},{MT_SCV,10},{MT_MARINE,1},{MT_SCV,11},
    {MT_MARINE,2},{MT_SCV,12},{MT_SUPPLY_DEPOT,2},{MT_MARINE,3},{MT_SCV,13},{MT_MARINE,4},{MT_SCV,14},
    {MT_BUNKER,1},{MT_MARINE,5},{MT_SCV,15},{MT_BARRACKS,2},{MT_MARINE,6},{MT_SCV,16},{MT_MARINE,7},
    {MT_SCV,17},{MT_REFINERY,1},{MT_MARINE,8},{MT_SCV,18},{MT_MARINE,10},{MT_SCV,19},{MT_MARINE,12},
    {MT_ACADEMY,1},{MT_MARINE,14},{MT_BARRACKS,3},{MT_SCV,20},{MT_FACTORY,1},{MT_MARINE,16},{MT_FIREBAT,1},
    {MT_MACHINE_SHOP,1},{SC_TECH_UI+SC_TECH_SIEGE_MODE,1},{MT_SIEGE_TANK,2},{MT_ENGINEERING_BAY,1},
    {MT_STARPORT,1},{MT_SCIENCE_FACILITY,1},{MT_ARMORY,1},{MT_REFINERY,2},
};
/* Zerg: cheap, fast and many. A 9-pool zergling rush behind a sunken,
 * a second hatchery early at the natural expansion, waves that trade
 * freely and come back often; hive tech for Ultralisks and Guardians. */
static const AiStep zerg_opening[] = {
    {MT_DRONE,9},{MT_OVERLORD,2},{MT_SPAWNING_POOL,1},{MT_DRONE,11},{MT_CREEP_COLONY,1},{MT_EXTRACTOR,1},
    {MT_ZERGLING,6},{MT_SUNKEN_COLONY,1},{MT_ZERGLING,12},{MT_OVERLORD,3},{MT_DRONE,13},{MT_DRONE,14},
    {MT_HYDRALISK_DEN,1},{MT_DRONE,16},{MT_HATCHERY,2},{MT_DRONE,17},{MT_EVOLUTION_CHAMBER,1},{MT_DRONE,18},
    {SC_UPGRADE_STEP(11,1),1},{MT_LAIR,1},{MT_EXTRACTOR,2},{MT_SPIRE,1},{MT_QUEENS_NEST,1},{MT_HIVE,1},{MT_ULTRALISK_CAVERN,1},
    {MT_GREATER_SPIRE,1},
};
/* Protoss: few, expensive, strong. A zealot opening on a teching base,
 * cannons at home after the Forge, then the natural; templar, reavers and
 * carriers, and attacks once it out-trades what it has seen. */
static const AiStep protoss_opening[] = {
    {MT_PROBE,8},{MT_PYLON,1},{MT_PROBE,10},{MT_GATEWAY,1},{MT_PROBE,12},{MT_PYLON,2},{MT_PROBE,13},
    {MT_ZEALOT,1},{MT_PROBE,14},{MT_GATEWAY,2},{MT_PROBE,15},{MT_ZEALOT,2},{MT_PROBE,16},{MT_PROBE,17},
    {MT_ZEALOT,4},{MT_PROBE,18},{MT_ZEALOT,5},{MT_ASSIMILATOR,1},{MT_ZEALOT,6},{MT_ZEALOT,8},{MT_FORGE,1},
    {MT_ZEALOT,9},{MT_ZEALOT,10},{SC_UPGRADE_STEP(13,1),1},{MT_CYBERNETICS_CORE,1},{MT_DRAGOON,2},{MT_ASSIMILATOR,2},
    {MT_CITADEL_OF_ADUN,1},{MT_TEMPLAR_ARCHIVES,1},{SC_TECH_UI+SC_TECH_PSIONIC_STORM,1},{MT_ROBOTICS_FACILITY,1},
    {MT_ROBOTICS_SUPPORT_BAY,1},{MT_STARGATE,1},{MT_FLEET_BEACON,1},{MT_OBSERVATORY,1},
};
/* CHK sides: Zerg, Terran, Protoss. */
static const faction_t factions[3] = {
    [0] = { .name = "Zerg", START(zerg_start), OPENING(zerg_opening), .wave_interval_ms = 30000, .wave_min_size = 6, .wave_max_size = 32,
        .doctrine = { .workers = 14, .supply_buffer = 4, .defenses = 1, .research = 25, .counter = 60,
            .expand_workers = 12, .max_towns = 3, .scout = MT_SPAWNING_POOL, .attack_ratio = 70, .retreat_ratio = 35,
            .roster = { {MT_DRONE,0},{MT_OVERLORD,0},{MT_CREEP_COLONY,0},{MT_SUNKEN_COLONY,0},{MT_SPORE_COLONY,0},
                        {MT_ZERGLING,45},{MT_HYDRALISK,30},{MT_MUTALISK,15},{MT_ULTRALISK,10},{MT_GUARDIAN,8} },
            .roster_count = 10 } },
    [1] = { .name = "Terran", START(terran_start), OPENING(terran_opening), .wave_interval_ms = 60000, .wave_min_size = 14, .wave_max_size = 30,
        .doctrine = { .workers = 20, .supply_buffer = 6, .defenses = 2, .research = 25, .counter = 70,
            .expand_workers = 20, .expand_after = MT_FACTORY, .max_towns = 2, .scout = MT_BARRACKS,
            .attack_ratio = 140, .retreat_ratio = 70,
            .roster = { {MT_SCV,0},{MT_SUPPLY_DEPOT,0},{MT_BUNKER,0},{MT_MISSILE_TURRET,0},{MT_SCIENCE_VESSEL,0},
                        {MT_MARINE,40},{MT_FIREBAT,10},{MT_VULTURE,10},{MT_GOLIATH,15},{MT_SIEGE_TANK,30},
                        {MT_WRAITH,5},{MT_BATTLECRUISER,5} },
            .roster_count = 12 } },
    [2] = { .name = "Protoss", START(protoss_start), OPENING(protoss_opening), .wave_interval_ms = 45000, .wave_min_size = 8, .wave_max_size = 20,
        .doctrine = { .workers = 20, .supply_buffer = 8, .defenses = 1, .research = 25, .counter = 70,
            .expand_workers = 16, .expand_after = MT_FORGE, .max_towns = 2, .scout = MT_GATEWAY,
            .attack_ratio = 110, .retreat_ratio = 60,
            .roster = { {MT_PROBE,0},{MT_PYLON,0},{MT_PHOTON_CANNON,0},{MT_OBSERVER,0},
                        {MT_ZEALOT,30},{MT_DRAGOON,35},{MT_HIGH_TEMPLAR,10},{MT_REAVER,10},{MT_CARRIER,10},
                        {MT_SCOUT,5},{MT_ARBITER,3} },
            .roster_count = 11 } },
};

static int faction_of(const level_t *map, int owner) {
    (void)map;
    int side = sc_player_side(owner);
    return side >= 0 && side < 3 ? side : 1;
}

/* What units.dat says beyond the engine's actor: supply, cloaking, casters,
 * shields and what a Bunker defends with. A Creep Colony is the site a
 * Sunken or Spore Colony grows out of; interceptors and scarabs count in
 * their Carrier or Reaver. Brood War's double hits are in weapons.dat. */
#define SC_NOT_FIGHTER (AI_ROLE_FIGHTER | AI_ROLE_HITS_GROUND | AI_ROLE_HITS_AIR)
static actorrole_t actors[SC_TYPES + 1] = {
#define SC_UNIT(id,name,hp,flags,w,h,sight,orders,race,minerals,gas,portrait,time,supply,used,armor,armor_up,build_score,destroy_score,shields,subunit,ground,air,size,speed,space,space_provided,addon_x,addon_y) \
    [(id)+1] = { .roles = ((supply) > 0 ? AI_ROLE_SUPPLY : 0) | \
                          (((flags) & (0x200 | 0x400000)) ? AI_ROLE_CLOAKED : 0) | \
                          (((flags) & 0x200000) ? AI_ROLE_SUPPORT : 0) | \
                          ((space_provided) && ((flags) & 1) ? AI_ROLE_DEFENSE | AI_ROLE_HITS_GROUND | AI_ROLE_HITS_AIR : 0) | \
                          ((id) + 1 == MT_CREEP_COLONY ? AI_ROLE_DEFENSE : 0), \
                 .not_roles = ((id) + 1 == MT_INTERCEPTOR || (id) + 1 == MT_SCARAB) ? SC_NOT_FIGHTER : 0, \
                 .extra_hp = (shields) },
#include "units.inc"
#undef SC_UNIT
};

/* Map overrides: the tables stay the authority, R_PatchSet remembers what a
 * map replaced and the next level load puts it back. */
static int unavailable[8][SC_TYPES]; /* PUNI: owner may not build the unit */

bool sc_unit_unavailable(int owner, int type) {
    return owner >= 0 && owner < 8 && type >= 1 && type <= SC_TYPES && unavailable[owner][type - 1];
}

static bool patch_apply(const rulepatch_t *e) {
    int v = e->value;
    if (e->table == SC_PATCH_UNIT && e->row < SC_TYPES) {
        sc_unit_t *unit = &sc_units[e->row];
        switch (e->field) {
        case SC_UNIT_HP: return R_PatchSet(&unit->hp, v);
        /* Only units that have shields (units.dat) can be given a value. */
        case SC_UNIT_SHIELDS:
            return unit->shields > 0 && R_PatchSet(&unit->shields, v) &&
                   R_PatchSet(&actors[e->row + 1].extra_hp, v);
        case SC_UNIT_ARMOR: return R_PatchSet(&unit->armor, v);
        case SC_UNIT_BUILD_TIME: return R_PatchSet(&unit->build_time, v);
        case SC_UNIT_MINERALS: return R_PatchSet(&unit->minerals, v);
        case SC_UNIT_GAS: return R_PatchSet(&unit->gas, v);
        }
    } else if (e->table == SC_PATCH_UPGRADE && e->row < SC_UPGRADES) {
        sc_upgrade_t *upgrade = &sc_upgrades[e->row];
        switch (e->field) {
        case SC_UPGRADE_MINERALS: return R_PatchSet(&upgrade->minerals, v);
        case SC_UPGRADE_MINERAL_FACTOR: return R_PatchSet(&upgrade->mineral_factor, v);
        case SC_UPGRADE_GAS: return R_PatchSet(&upgrade->gas, v);
        case SC_UPGRADE_GAS_FACTOR: return R_PatchSet(&upgrade->gas_factor, v);
        case SC_UPGRADE_TIME: return R_PatchSet(&upgrade->time, v);
        case SC_UPGRADE_TIME_FACTOR: return R_PatchSet(&upgrade->time_factor, v);
        }
    } else if (e->table == SC_PATCH_WEAPON && e->row < SC_WEAPONS) {
        sc_weapon_t *weapon = &sc_weapons[e->row];
        switch (e->field) {
        case SC_WEAPON_DAMAGE: return R_PatchSet(&weapon->damage, v);
        case SC_WEAPON_BONUS: return R_PatchSet(&weapon->bonus, v);
        }
    } else if (e->table == SC_PATCH_TECH && e->row < SC_TECHS && sc_techs[e->row].name) {
        sc_tech_t *tech = &sc_techs[e->row];
        switch (e->field) {
        case SC_TECH_MINERALS: return R_PatchSet(&tech->minerals, v);
        case SC_TECH_GAS: return R_PatchSet(&tech->gas, v);
        case SC_TECH_TIME: return R_PatchSet(&tech->time, v);
        case SC_TECH_ENERGY: return R_PatchSet(&tech->energy, v);
        }
    } else if (e->table == SC_PATCH_UNAVAILABLE && e->row < 8 && e->field < SC_TYPES) {
        return R_PatchSet(&unavailable[e->row][e->field], !v);
    }
    return false;
}

/* Hit points and damage reach the actor types; costs reach the catalog. */
static void patch_done(void) {
    sc_refresh_actors();
    sc_rebuild_products();
}

/* upgrades.dat research that levels two and three wait for a second tier of
 * tech. Each level also needs the one before it. Zerg's Lair and Hive are
 * not simulated yet, so their upgrades carry only the level rule. */
#define TIER(u, level, needs) { SC_UPGRADE_UI + (u) * 4 + (level) - 1, { REQ_UPGRADE, (u), (level) - 1 } }, \
    { SC_UPGRADE_UI + (u) * 4 + (level) - 1, { REQ_BUILDING, (needs), 0 } }
#define LEVELS(u) { SC_UPGRADE_UI + (u) * 4 + 1, { REQ_UPGRADE, (u), 1 } }, { SC_UPGRADE_UI + (u) * 4 + 2, { REQ_UPGRADE, (u), 2 } }
static const productreq_t requirements[] = {
    /* Terran infantry (Engineering Bay) need the Armory, vehicles and ships (Armory) the Science Facility. */
    TIER(7, 2, MT_ARMORY), TIER(7, 3, MT_ARMORY), TIER(0, 2, MT_ARMORY), TIER(0, 3, MT_ARMORY),
    TIER(8, 2, MT_SCIENCE_FACILITY), TIER(8, 3, MT_SCIENCE_FACILITY), TIER(1, 2, MT_SCIENCE_FACILITY), TIER(1, 3, MT_SCIENCE_FACILITY),
    TIER(9, 2, MT_SCIENCE_FACILITY), TIER(9, 3, MT_SCIENCE_FACILITY), TIER(2, 2, MT_SCIENCE_FACILITY), TIER(2, 3, MT_SCIENCE_FACILITY),
    /* Protoss ground upgrades (Forge) need the Templar Archives, air the Fleet Beacon, shields the Cybernetics Core. */
    TIER(13, 2, MT_TEMPLAR_ARCHIVES), TIER(13, 3, MT_TEMPLAR_ARCHIVES), TIER(5, 2, MT_TEMPLAR_ARCHIVES), TIER(5, 3, MT_TEMPLAR_ARCHIVES),
    TIER(14, 2, MT_FLEET_BEACON), TIER(14, 3, MT_FLEET_BEACON), TIER(6, 2, MT_FLEET_BEACON), TIER(6, 3, MT_FLEET_BEACON),
    TIER(15, 2, MT_CYBERNETICS_CORE), TIER(15, 3, MT_CYBERNETICS_CORE),
    /* Zerg. */
    LEVELS(10), LEVELS(11), LEVELS(3), LEVELS(12), LEVELS(4),
};
#undef TIER
#undef LEVELS

static bool prerequisite_met(int owner, int id) { return sc_owner_has(owner, (uint16_t)id); }

const ruleset_t g_ruleset = {
    .game = "starcraft",
    .policy = { .input = INPUT_RIGHT_CLICK_ORDERS, .sight = SIGHT_RADIAL, .select = SELECT_ANY,
                .f10 = F10_CONTROL_MENU, .turning = TURN_INSTANT },
    .factions = factions, .faction_count = 3, .faction_of = faction_of,
    .prerequisite_met = prerequisite_met,
    .actors = actors, .actor_count = SC_TYPES + 1,
    .requirements = requirements, .requirement_count = (int)(sizeof(requirements) / sizeof(*requirements)),
    .upgrade_level = sc_upgrade_level, .upgrade_product = sc_upgrade_product,
    .patch_apply = patch_apply, .patch_done = patch_done,
};
