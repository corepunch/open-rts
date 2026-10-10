#include "engine.h"
#include "dark-colony.h"

/* Dark Colony's share of the engine's game hooks: alliances and purchases
 * that need no unit, its environment clock, and the state it adds to saves and
 * to the lockstep checksum. */

uint32_t G_ConsistencyExtra(uint32_t hash) {
#define HASH(v) hash = G_HashValue(hash, (uint32_t)(v))
    for (int p = 0; p < 8; ++p) {
        HASH(level.peace[p]);
        HASH(level.alliance_offers[0][p]); HASH(level.alliance_offers[1][p]);
        HASH(level.exo_income[p]);
    }
    for (int owner = 0; owner < 8; ++owner)
        for (int row = 0; row < 110; ++row) {
            HASH(level.purchases[owner][row].selected);
            HASH(level.purchases[owner][row].queued);
        }
#undef HASH
    return hash;
}

bool G_GameCommand(int player, const ticcmd_t *cmd) {
    switch (cmd->order) {
    case TC_ALLY:
    case TC_SHARE_SIGHT:
        DC_SetAlliance(player, cmd->target, cmd->order == TC_SHARE_SIGHT, cmd->product != 0);
        return true;
    case TC_GIVE:
        if (cmd->target < 8 && cmd->target != (unsigned)player &&
            DC_PlayerActive(cmd->target) && level.player_resources[player][0] > 1000 &&
            level.player_resources[cmd->target][0] <= INT_MAX - 1000) {
            level.player_resources[player][0] -= 1000;
            level.player_resources[cmd->target][0] += 1000;
        }
        return true;
    case TC_PURCHASE:
        DC_SelectPurchase(player, cmd->product, cmd->target != 0);
        return true;
    case TC_SUBMIT:
        DC_SubmitPurchases(player);
        return true;
    default:
        return false;
    }
}

void G_FreeLevelData(level_t *map) { DC_FreeWeapons(map); }

void G_ProductionBegin(int elapsed_ms) { (void)elapsed_ms; DC_RunPurchases(); }

const uint32_t *G_AllianceMasks(const level_t *map) { return map->peace; }

void G_ClockBegin(int64_t clock) { DC_TickSupport(clock); }

void G_ClockEnd(int64_t before, int64_t clock) {
    /* Requested fog refresh: 10 Hz on the unchanged 30 Hz simulation clock. */
    if ((leveltime + 1) % (RTS_TICRATE / 10) == 0) P_UpdateSight();
    /* DC.EXE 0x418c52: base income retains its sixteen-native-tic cadence. */
    if (clock != before && (clock & 15) == 0) DC_TickIncome();
}

bool G_GameSave(const char *path, const char *name, const app_t *app,
                const AiContext *ai, const hudtext_t *hud) {
    return DC_SaveGame(path, name, app, ai, hud);
}

bool G_GameLoad(const char *path, app_t *app, AiContext *ai, hudtext_t *hud) {
    return DC_LoadGame(path, app, ai, hud);
}

/* The generic save body (network resync) carries what DC adds to a level. */
typedef struct {
    uint8_t offers[2][8], purchases[sizeof(level.purchases)];
    uint32_t peace[8];
    int exo_income[8];
    size_t mission_size;
} dc_extra_t;

size_t G_SaveExtraSize(void) {
    size_t mission = 0;
    DC_MissionArchive(&mission);
    return sizeof(dc_extra_t) + mission;
}

void G_SaveExtra(void *out) {
    size_t mission = 0;
    const void *archive = DC_MissionArchive(&mission);
    dc_extra_t extra = {.mission_size = mission};
    memcpy(extra.offers, level.alliance_offers, sizeof(extra.offers));
    memcpy(extra.purchases, level.purchases, sizeof(extra.purchases));
    memcpy(extra.peace, level.peace, sizeof(extra.peace));
    memcpy(extra.exo_income, level.exo_income, sizeof(extra.exo_income));
    memcpy(out, &extra, sizeof(extra));
    if (mission) memcpy((uint8_t *)out + sizeof(extra), archive, mission);
}

bool G_LoadExtra(const void *data, size_t size) {
    dc_extra_t extra;
    if (size < sizeof(extra)) return false;
    memcpy(&extra, data, sizeof(extra));
    const uint8_t *mission = (const uint8_t *)data + sizeof(extra);
    if (extra.mission_size != size - sizeof(extra) ||
        !DC_ValidateMissionArchive(mission, extra.mission_size) ||
        !DC_RestoreMission(mission, extra.mission_size)) return false;
    memcpy(level.alliance_offers, extra.offers, sizeof(extra.offers));
    memcpy(level.purchases, extra.purchases, sizeof(extra.purchases));
    memcpy(level.peace, extra.peace, sizeof(extra.peace));
    memcpy(level.exo_income, extra.exo_income, sizeof(extra.exo_income));
    return true;
}

/* A queue of units takes one product; buildings and research do not. */
bool G_QueueLocksProduct(const StaticProductDefinition *product) {
    return product->product_class == RTS_PRODUCT_UNIT;
}
