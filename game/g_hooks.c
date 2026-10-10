#include "engine.h"

/* Weak defaults for the game hooks declared in engine.h. A game that needs
 * one defines it in games/<id>/ and the linker prefers that definition. */
#define WEAK __attribute__((weak))

WEAK uint32_t G_ConsistencyExtra(uint32_t hash) { return hash; }
WEAK bool G_GameCommand(int player, const ticcmd_t *cmd) { (void)player; (void)cmd; return false; }
WEAK bool G_UnitCommandable(const mobj_t *unit) { (void)unit; return true; }
WEAK bool G_GameUnitCommand(int player, const ticcmd_t *cmd, mobj_t *const *units,
                            int count, mobj_t *target) {
    (void)player; (void)cmd; (void)units; (void)count; (void)target;
    return false;
}
WEAK void G_InterruptOrders(const ticcmd_t *cmd, mobj_t *const *units, int count, bool late) {
    (void)cmd; (void)units; (void)count; (void)late;
}
WEAK bool G_GameSave(const char *path, const char *name, const app_t *app,
                     const AiContext *ai, const hudtext_t *hud) {
    return G_SaveGame(path, name, app, ai, hud);
}
WEAK bool G_GameLoad(const char *path, app_t *app, AiContext *ai, hudtext_t *hud) {
    return G_LoadGame(path, app, ai, hud);
}
WEAK bool G_TakeCameraRequest(fvec2_t *cell) { (void)cell; return false; }
WEAK void G_FreeLevelData(level_t *map) { (void)map; }
WEAK void G_ClockBegin(int64_t clock) { (void)clock; }
WEAK void G_ClockEnd(int64_t before, int64_t clock) {
    if (clock != before && (clock & 3) == 0) P_UpdateSight();
}
WEAK const uint32_t *G_AllianceMasks(const level_t *map) { return map->sight.allies; }
WEAK void G_ProductionBegin(int elapsed_ms) { (void)elapsed_ms; }
WEAK uint32_t G_SaveLayout(void) { return 0; }
WEAK bool G_SightRadius(const mobj_t *actor, int *radius) { (void)actor; (void)radius; return true; }
WEAK bool G_ViewerSees(const mobj_t *target, int owner, int team) {
    (void)target; (void)owner; (void)team;
    return true;
}
WEAK bool G_GameHarvestAt(mobj_t *const *units, int count, fixed2_t position, bool *issued) {
    (void)units; (void)count; (void)position; (void)issued;
    return false;
}
WEAK bool G_QueueLocksProduct(const StaticProductDefinition *product) { (void)product; return true; }
