#include "engine.h"
#include "info.h"
#include "kknd.h"

/* KKnD's ruleset: Survivors and Evolved (Mutants). One ladder serves both,
 * written as {Survivor id, Mutant id, count} and split below. Income needs
 * the whole oil loop: a power station to unload at, a drill rig (bought as a
 * mobile derrick that deploys, see kk_ai_owned) and tankers. */

static const AiStep survivor_opening[] = {
    { 40, 1 },  /* Outpost, unpacked from the mobile outpost */
    { 42, 1 },  /* Machine shop */
    { 38, 1 },  /* Power station */
    { 55, 1 },  /* Drill rig */
    { 32, 1 },  /* Oil tanker */
    { 0,  3 },  /* Rifleman */
    { 32, 2 },
    { 47, 1 },  /* Research lab */
    { 16, 2 },  /* Dirt bike */
    { 12, 2 },  /* RPG launcher */
    { 49, 1 },  /* Guard tower */
    { 18, 2 },  /* 4x4 pickup */
    { 0,  6 },
    { 20, 2 },  /* ATV */
    { 14, 2 },  /* Sniper */
    { 24, 2 },  /* Anaconda */
    { 51, 1 },  /* Missile battery */
    { 55, 2 },
    { 32, 4 },
    { 28, 2 },  /* Autocannon */
    { 0,  10 },
    { 24, 4 },
    { 26, 2 },  /* Barrage craft */
    { 28, 4 },
    { 22, 2 },  /* Flame ATV */
};
static const AiStep evolved_opening[] = {
    { 41, 1 },  /* Clan hall */
    { 43, 1 },  /* Blacksmith */
    { 39, 1 },  /* Power station */
    { 56, 1 },  /* Drill rig */
    { 33, 1 },  /* Oil tanker */
    { 1,  3 },  /* Berserker */
    { 33, 2 },
    { 48, 1 },  /* Alchemy hall */
    { 17, 2 },  /* Dire wolf */
    { 13, 2 },  /* Bazooka */
    { 50, 1 },  /* Machinegun nest */
    { 19, 2 },  /* Bike and sidecar */
    { 1,  6 },
    { 21, 2 },  /* Monster truck */
    { 15, 2 },  /* Crazy Harry */
    { 25, 2 },  /* War mastodon */
    { 52, 1 },  /* Grapeshot tower */
    { 56, 2 },
    { 33, 4 },
    { 29, 2 },  /* Missile crab */
    { 1,  10 },
    { 25, 4 },
    { 27, 2 },  /* Giant beetle */
    { 29, 4 },
    { 23, 2 },  /* Giant scorpion */
};

#define OPENING(steps) .opening = steps, .opening_count = (int)(sizeof(steps) / sizeof(*steps))

/* Survivors fight with guns and engines: riflemen and rockets screen the
 * 4x4s, ATVs and heavy tanks, behind towers, and attack once they match
 * the enemy, falling back early to repair. The Evolved swarm with cheap
 * berserkers and beasts, attack sooner and fight to the end. KKnD has no
 * supply, so army_cap bounds the army. */
static const faction_t factions[2] = {
    [0] = { .name = "Survivors", OPENING(survivor_opening),
        .wave_interval_ms = 40000, .wave_min_size = 6, .wave_max_size = 16,
        .doctrine = { .workers = 3, .defenses = 2, .army_cap = 40, .counter = 50,
            .attack_ratio = 110, .retreat_ratio = 50,
            .roster = { {32,0},{49,0},{51,0},{53,0},
                        {0,15},{12,10},{4,5},{14,5},{16,5},{18,15},{20,15},{24,20},{28,10},{26,10} },
            .roster_count = 14 } },
    [1] = { .name = "Evolved", OPENING(evolved_opening),
        .wave_interval_ms = 40000, .wave_min_size = 6, .wave_max_size = 16,
        .doctrine = { .workers = 3, .defenses = 1, .army_cap = 40, .counter = 50,
            .attack_ratio = 80, .retreat_ratio = 30,
            .roster = { {33,0},{50,0},{52,0},{54,0},
                        {1,30},{13,10},{15,5},{17,20},{19,5},{21,5},{23,10},{25,15},{27,10},{29,5} },
            .roster_count = 14 } },
};

/* The catalog tags every building and unit with a faction (1 Survivor, 2
 * Mutant); -1 while the owner has nothing yet. */
static int faction_of(const level_t *map, int owner) {
    (void)map;
    int faction = KK_OwnerFaction(owner);
    return faction < 0 ? -1 : faction == 1 ? 0 : 1;
}

/* A producer's tech is its research level; one still unpacking cannot
 * produce yet. */
static int tech_level(const mobj_t *producer) {
    return gameinfo->states[producer->core.state_id].group == 6 ? -1 : producer->research.level;
}

/* A lab researches one producer at a time; KK_Research on a producer that is
 * already being researched would cancel it, so check first. */
static bool research(level_t *map, mobj_t *producer) {
    (void)map;
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next)
        if (th->function == P_MobjThinker && ((mobj_t *)th)->research.target == producer->id)
            return true; /* Already under way: keep waiting. */
    return KK_Research(producer);
}

/* Every product's tech level, from the same catalog rows (products.inc). */
static const productreq_t requirements[] = {
#define KK_PRODUCT(id,faction,category,kind,type,maker,cost,ticks,tech,limit,label) \
    { (id), { REQ_TECH, (maker), (tech) } },
#include "products.inc"
#undef KK_PRODUCT
};

const ruleset_t g_ruleset = {
    .game = "kknd",
    .policy = { .input = INPUT_LEFT_SELECT_ORDER },
    .factions = factions, .faction_count = 2, .faction_of = faction_of,
    .requirements = requirements, .requirement_count = (int)(sizeof(requirements) / sizeof(*requirements)),
    .tech_level = tech_level, .research = research,
};
