#include "engine.h"
#include "dark-reign.h"
#include "info.h"

/* Dark Reign's ruleset: Freedom Guard and Imperium computer players. One
 * ladder serves both, written as {Freedom Guard id, Imperium id, count}
 * and split into the two openings below. Buildings come from Construction
 * Rigs, units from the matching factory. */

static const AiStep fg_opening[] = {
    { 10001, 1 },   /* HQ 1 */
    { 10006, 1 },   /* Vehicle factory */
    { 13,    1 },   /* Freighter */
    { 10004, 1 },   /* Barracks */
    { 9,     3 },   /* Raider */
    { 13,    2 },
    { 10013, 1 },   /* Guard tower */
    { 20,    2 },   /* Skirmish tank */
    { 10,    2 },   /* Mercenary */
    { 17,    2 },   /* Tank hunter */
    { 10002, 1 },   /* HQ 2 */
    { 10005, 1 },   /* Advanced barracks */
    { 10007, 1 },   /* Advanced vehicle factory */
    { 8,     2 },   /* Sniper */
    { 9,     6 },
    { 20,    4 },
    { 16,    2 },   /* Triple rail tank */
    { 10014, 1 },   /* Advanced guard tower */
    { 19,    2 },   /* Hellstorm */
    { 10,    4 },
    { 16,    4 },
    { 17,    4 },
};
static const AiStep imperium_opening[] = {
    { 11001, 1 },   /* HQ 1 */
    { 11006, 1 },   /* Assembly plant */
    { 1006,  1 },   /* Freighter */
    { 11004, 1 },   /* Training facility */
    { 1002,  3 },   /* Guardian */
    { 1006,  2 },
    { 11014, 1 },   /* Guard tower */
    { 1010,  2 },   /* Scout tank */
    { 1003,  2 },   /* Bion */
    { 1011,  2 },   /* Plasma tank */
    { 11002, 1 },   /* HQ 2 */
    { 11005, 1 },   /* Advanced barracks */
    { 11007, 1 },   /* Advanced vehicle factory */
    { 1004,  2 },   /* Exterminator */
    { 1002,  6 },
    { 1010,  4 },
    { 1012,  2 },   /* Tachyon tank */
    { 11015, 1 },   /* Advanced guard tower */
    { 1017,  2 },   /* S.C.A.R.A.B. */
    { 1003,  4 },
    { 1012,  4 },
    { 1011,  4 },
};

#define OPENING(steps) .opening = steps, .opening_count = (int)(sizeof(steps) / sizeof(*steps))

/* The sides' characters, from UNITS.TXT and WEAPON.TXT. The Freedom Guard
 * fields cheap infantry, bikes and hover tanks that strike early and pull
 * back to heal. The Imperium masses armour and plasma, waits for a clear
 * edge and fights it out. Anti-air (Flak Jack, M.A.D.) only shoots up, so
 * the counter weight brings it in once flyers are seen. Dark Reign has no
 * supply, so army_cap bounds the army. */
static const faction_t factions[2] = {
    [0] = { .name = "Freedom Guard", OPENING(fg_opening),
        .wave_interval_ms = 40000, .wave_min_size = 6, .wave_max_size = 14,
        .doctrine = { .workers = 3, .defenses = 2, .army_cap = 40, .counter = 60,
            .attack_ratio = 90, .retreat_ratio = 60,
            .roster = { {13,0},{10013,0},{10014,0},{10012,0},
                        {9,30},{10,20},{1,15},{20,20},{17,10},{12,5},{16,10},{19,5},{7,3} },
            .roster_count = 13 } },
    [1] = { .name = "Imperium", OPENING(imperium_opening),
        .wave_interval_ms = 40000, .wave_min_size = 6, .wave_max_size = 14,
        .doctrine = { .workers = 3, .defenses = 3, .army_cap = 40, .counter = 60,
            .attack_ratio = 130, .retreat_ratio = 35,
            .roster = { {1006,0},{11014,0},{11015,0},{11013,0},
                        {1002,15},{1003,20},{1004,10},{1010,10},{1011,25},{1015,5},{1013,5},{1012,15},{1017,5},
                        {1008,3} },
            .roster_count = 14 } },
};

/* Read from what the owner already has; -1 while it has nothing yet. */
static int faction_of(const level_t *map, int owner) {
    (void)map;
    if (G_ModelHasActorType(NULL, owner, MT_FG_CONSTRUCTION_CREW) ||
        G_ModelHasActorType(NULL, owner, MT_FG_HQ1) ||
        G_ModelHasActorType(NULL, owner, MT_FG_HQ2) ||
        G_ModelHasActorType(NULL, owner, MT_FG_HQ3)) return 0;
    if (G_ModelHasActorType(NULL, owner, MT_IMP_CONSTRUCTION_CREW) ||
        G_ModelHasActorType(NULL, owner, MT_IMP_HQ1) ||
        G_ModelHasActorType(NULL, owner, MT_IMP_HQ2) ||
        G_ModelHasActorType(NULL, owner, MT_IMP_HQ3)) return 1;
    return -1;
}

/* What the actor table cannot show: snipers, scouts and saboteurs hide as
 * terrain and spies as enemy units (CanMorphInto*), and the Amper boosts
 * the units around it (CanBoost). */
static const actorrole_t actors[NUMMOBJTYPES] = {
    [MT_FG_SNIPER] = { .roles = AI_ROLE_CLOAKED }, [MT_FG_SCOUT] = { .roles = AI_ROLE_CLOAKED },
    [MT_FG_SABOTEUR] = { .roles = AI_ROLE_CLOAKED }, [MT_FG_SPY] = { .roles = AI_ROLE_CLOAKED },
    [MT_IMP_SPY] = { .roles = AI_ROLE_CLOAKED }, [MT_IMP_AMPER] = { .roles = AI_ROLE_SUPPORT },
};

static const StaticProductDefinition *prerequisite_product(int id) { return G_ModelProductByUIId(NULL, id); }

const ruleset_t g_ruleset = {
    .game = "dark-reign",
    .policy = { .input = INPUT_LEFT_SELECT_ORDER },
    .prerequisite_product = prerequisite_product, .prerequisite_met = DR_PrerequisiteMet,
    .factions = factions, .faction_count = 2, .faction_of = faction_of,
    .actors = actors, .actor_count = NUMMOBJTYPES,
};
