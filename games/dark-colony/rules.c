#include "dark-colony.h"
#include "engine.h"
#include "info.h"

/* Dark Colony's ruleset: the Human and Gray (alien) computer players.
 * DC.EXE's decision code is not ported; each faction's opening ladder
 * reproduces the observable structure (economy first, tech up, steady army,
 * waves) through the retail purchase path, and the doctrine adds what the
 * ladder cannot: replacing losses, an army mix and when to attack or fall
 * back. The only verified AI versus AI+ difference is the credit multiplier
 * (see DC_ApplyAiIncome), so both levels share a doctrine. */

enum {
    /* MAINE button ids of the DEPEND rows, see DARK_COLONY_PRODUCTS. */
    UI_BARRACKS = 80, UI_SCIPOD = 81, UI_ROBOFTR = 82, UI_SCIPOD2 = 85, UI_ROBOFTR2 = 86,
    UI_EXPLOITER = 87, UI_FIRESTORM = 88, UI_TROOPER = 89, UI_SENTINEL = 90,
    UI_REAPER = 91, UI_BARRAGER = 93,
    UI_WARFOLD = 41, UI_BREEDPOD = 42, UI_GENESAC = 43, UI_BROZAAR = 46,
    UI_XENOWORT = 47, UI_GRAY = 48, UI_SYDEMON = 50, UI_ATRIL = 51,
    UI_PODUPGRADE = 97, UI_GENEUPGRADE = 98, UI_SLOM = 71,
};

static const AiStep human_opening[] = {
    {UI_EXPLOITER,1},{UI_BARRACKS,1},{UI_TROOPER,3},{UI_EXPLOITER,2},{UI_SCIPOD,1},{UI_TROOPER,6},
    {UI_ROBOFTR,1},{UI_REAPER,3},{UI_SENTINEL,2},{UI_EXPLOITER,3},{UI_SCIPOD2,1},{UI_TROOPER,10},
    {UI_REAPER,6},{UI_ROBOFTR2,1},{UI_BARRAGER,2},{UI_FIRESTORM,2},{UI_TROOPER,16},{UI_REAPER,10},
    {UI_SENTINEL,6},{UI_BARRAGER,4},{UI_FIRESTORM,4},{UI_TROOPER,24},{UI_REAPER,16},{UI_SENTINEL,12},
    {UI_BARRAGER,8},{UI_FIRESTORM,8},{UI_TROOPER,36},{UI_REAPER,24},
};
static const AiStep gray_opening[] = {
    {UI_BROZAAR,1},{UI_WARFOLD,1},{UI_GRAY,3},{UI_BROZAAR,2},{UI_BREEDPOD,1},{UI_GRAY,6},
    {UI_GENESAC,1},{UI_SYDEMON,3},{UI_SLOM,2},{UI_BROZAAR,3},{UI_GRAY,10},{UI_SYDEMON,6},
    {UI_PODUPGRADE,1},{UI_GENEUPGRADE,1},{UI_ATRIL,2},{UI_XENOWORT,2},{UI_GRAY,16},{UI_SYDEMON,10},
    {UI_SLOM,6},{UI_ATRIL,4},{UI_GRAY,24},{UI_SYDEMON,16},{UI_SLOM,12},{UI_ATRIL,8},
    {UI_XENOWORT,6},{UI_GRAY,36},{UI_SYDEMON,24},
};

#define OPENING(steps) .opening = steps, .opening_count = (int)(sizeof(steps) / sizeof(*steps))

/* Humans: troopers and walkers, patient and well supported. Grays: cheap
 * swarms of Grays and Sy-Demons that attack early and trade freely. */
static const faction_t factions[2] = {
    [0] = { .name = "Human", OPENING(human_opening),
        .wave_interval_ms = 45000, .wave_min_size = 6, .wave_max_size = 16,
        .doctrine = { .workers = 4, .counter = 40, .attack_ratio = 90, .retreat_ratio = 50,
            .roster = { {UI_EXPLOITER,0},{UI_TROOPER,40},{UI_SENTINEL,15},{UI_REAPER,25},
                        {UI_FIRESTORM,10},{UI_BARRAGER,10} },
            .roster_count = 6 } },
    [1] = { .name = "Gray", OPENING(gray_opening),
        .wave_interval_ms = 45000, .wave_min_size = 6, .wave_max_size = 16,
        .doctrine = { .workers = 4, .counter = 40, .attack_ratio = 75, .retreat_ratio = 40,
            .roster = { {UI_BROZAAR,0},{UI_GRAY,40},{UI_SYDEMON,25},{UI_SLOM,15},
                        {UI_ATRIL,10},{UI_XENOWORT,10} },
            .roster_count = 6 } },
};

static int faction_of(const level_t *map, int owner) {
    if (!map || owner < 0 || owner >= 8) return -1;
    return DC_PlayerRace(owner) == 0 ? 0 : 1;
}

const ruleset_t g_ruleset = {
    .game = "dark-colony",
    .policy = { .input = INPUT_LEFT_SELECT_ORDER, .stance = STANCE_MOVE_ONLY_STICKY },
    .prerequisite_product = DC_PrerequisiteProduct, .prerequisite_met = DC_PrerequisiteMet,
    .factions = factions, .faction_count = 2, .faction_of = faction_of,
};
