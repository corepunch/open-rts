#include "t_local.h"
#include "engine.h"
#include <string.h>

#define CHECK(c) RTS_CHECK(c, "ai vents", #c)

static AiContext ctx;
static mobj_t *units[2];

static int dummy_level(int owner) { (void)owner; return AI_LEVEL_NORMAL; }
static int level_of(const level_t *map, int owner) { (void)map; return owner == 1 ? dummy_level(owner) : AI_LEVEL_NONE; }
static const AiGameInterface game = { .name = "vents", .features = AI_FEATURE_ECONOMY, .player_level = level_of };

/* The nearest vent sits inside a walled-off pocket; the harvester must fall
 * through to the next vent instead of idling forever. */
int main(void) {
    if (!strcmp(g_game_id, "dark-colony")) return 0; /* Native pathing has its own suite. */
    static level_t map;
    memset(&map, 0, sizeof(map));
    P_InitThinkers();
    map.width = map.height = 64;
    map.blocked = calloc(64 * 64, 1);
    map.resource_vents = calloc(2, sizeof(*map.resource_vents));
    CHECK(map.blocked && map.resource_vents);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) map.blocked[y * 64 + x] = 1;
    map.resource_vent_count = 2;
    map.resource_vents[0] = (resourcevent_t){ .cell = {2, 2}, .attachment = FIXED2_LIT(2, 2), .amount = 10000, .rate = 1, .active = true };
    map.resource_vents[1] = (resourcevent_t){ .cell = {45, 45}, .attachment = FIXED2_LIT(45, 45), .amount = 10000, .rate = 1, .active = true };
    mobj_t *slug = spawn_mobj_fixture((mobj_t){0});
    slug->owner = 1; slug->hp = 100; slug->traits = MF_MOBILE | MF_HARVESTER | MF_SELECTABLE;
    slug->allegiance = ALLEGIANCE_ENEMY; slug->radius = FIXED_LIT(0.4);
    slug->core.position = (fixed3_t){ 5 << 16, 30 << 16, 0 };
    units[0] = slug;
    CHECK(!P_HarvestUnitTo(&map, slug, map.resource_vents[0].attachment)); /* Truly unreachable. */
    CHECK(P_HarvestUnitTo(&map, slug, map.resource_vents[1].attachment));
    slug->harvest.phase = HARVEST_PHASE_NONE; slug->harvest.target = -1;
    P_AiInit(&ctx);
    P_AiAttachGame(&ctx, &game);
    for (int i = 0; i < 8; ++i) P_AiTick(&ctx, &map, units, 1, NULL, 33);
    CHECK(slug->harvest.target == 1);
    CHECK(P_AiStats(&ctx, 1)->harvest_orders == 1);
    free(map.blocked); free(map.resource_vents);
    puts("PASS: ai harvesters skip an unreachable vent and take the next one");
    return 0;
}
