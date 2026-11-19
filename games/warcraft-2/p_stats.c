#include "engine.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#include <string.h>

/* Per-player historical counters for the result scene, and the score and rank
 * built from them. The rules follow Stratagus and Wargus: a unit or building
 * counts when it is produced, a kill or razing credits the killer's player
 * with the victim type's Points (info.c `.points`), gathered resources count
 * as they are banked, and a victory is worth 500 more on the local player's
 * score (reference/wargus/scripts/menus/results.lua). The retail executable's
 * own bookkeeping was not traced. */

const w2_tally_t *W2_Tally(int owner) {
    return owner >= 0 && owner < 8 ? &level.w2_tally[owner] : NULL;
}

static bool wall(int type) { return type == MT_HUMAN_WALL || type == MT_ORC_WALL; }

/* A finished unit or building (G_UnitProduced). Walls and neutral owners
 * are not counted, as Stratagus leaves walls out of TotalBuildings. */
void W2_TallyProduced(const mobj_t *unit) {
    if (!unit || unit->owner >= 8 || !G_UnitHoldsSide(unit) || wall(unit->type_id)) return;
    w2_tally_t *tally = &level.w2_tally[unit->owner];
    if (mobjinfo[unit->type_id].w2.flags & W2_STRUCTURE) ++tally->buildings;
    else ++tally->units;
}

/* G_UnitKilled: the killer's player earns the victim's Points. Kills by no
 * one (spell backlash, drowning) and of one's own units score nothing. */
void W2_TallyKill(const mobj_t *victim, const mobj_t *killer) {
    if (!victim || !killer || killer->owner >= 8 || killer->owner == victim->owner ||
        !G_UnitHoldsSide(victim)) return;
    const mobjinfo_t *info = &mobjinfo[victim->type_id];
    w2_tally_t *tally = &level.w2_tally[killer->owner];
    tally->score += info->w2.points;
    if (info->w2.flags & W2_STRUCTURE) ++tally->razings;
    else ++tally->kills;
}

void W2_TallyResource(int owner, int resource, int amount) {
    if (owner < 0 || owner >= 8 || resource < 0 || resource >= 3 || amount <= 0) return;
    level.w2_tally[owner].resources[resource] += amount;
}

uint32_t W2_TallyHash(uint32_t hash) {
    for (int p = 0; p < 8; ++p) {
        const w2_tally_t *t = &level.w2_tally[p];
        const int fields[] = {t->units, t->buildings, t->resources[0], t->resources[1],
                              t->resources[2], t->kills, t->razings, t->score};
        for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
            hash = G_HashValue(hash, (uint32_t)fields[i]);
    }
    return hash;
}

/* Wargus rank tables: the score at which each rank starts. */
static const int rank_floor[] = {0, 2000, 5000, 8000, 18000, 28000, 40000, 55000, 70000, 85000,
    105000, 125000, 145000, 165000, 185000, 205000, 230000, 255000, 280000};
static const char *const human_ranks[] = {"Servant", "Peasant", "Squire", "Footman", "Corporal",
    "Sergeant", "Lieutenant", "Captain", "Major", "Knight", "General", "Admiral", "Marshall",
    "Lord", "Grand Admiral", "Highlord", "Thundergod", "God", "Designer"};
static const char *const orc_ranks[] = {"Slave", "Peon", "Rogue", "Grunt", "Slasher", "Marauder",
    "Commander", "Captain", "Major", "Knight", "General", "Master", "Marshall", "Chieftain",
    "Overlord", "War Chief", "Demigod", "God", "Designer"};
enum { RANKS = sizeof(rank_floor) / sizeof(rank_floor[0]) };

/* The highest rank whose floor the score exceeds (results.lua compares
 * with `>`, so exactly 2000 is still the lowest rank). */
int W2_RankIndex(int score) {
    int rank = 0;
    for (int i = 1; i < RANKS; ++i) {
        if (score > rank_floor[i]) rank = i;
        else break;
    }
    return rank;
}

const char *W2_RankName(bool orc, int score) {
    return (orc ? orc_ranks : human_ranks)[W2_RankIndex(score)];
}
