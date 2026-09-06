#include "engine_config.h"
#include "../rts_test.h"
#include "../../game/g_game.h"
#include "../../play/p_ai.h"
#include "../../play/p_local.h"
#include "../../games/dark-colony/info.h"
#include "../../games/dark-colony/dc_types.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *message) {
    return rts_fail("ai_team_aware", message);
}

static int assert_is_ally_basic(void) {
    mobj_t a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));

    a.allegiance = ALLEGIANCE_PLAYER;
    b.allegiance = ALLEGIANCE_PLAYER;
    if (!P_IsAlly(&a, &b)) return fail("same allegiance should be allies");

    a.allegiance = ALLEGIANCE_PLAYER;
    b.allegiance = ALLEGIANCE_ENEMY;
    if (P_IsAlly(&a, &b)) return fail("player and enemy should not be allies");

    a.allegiance = ALLEGIANCE_ENEMY;
    b.allegiance = ALLEGIANCE_ENEMY;
    if (!P_IsAlly(&a, &b)) return fail("same enemy allegiance should be allies");

    a.allegiance = ALLEGIANCE_ALLIED;
    b.allegiance = ALLEGIANCE_ALLIED;
    if (!P_IsAlly(&a, &b)) return fail("same allied allegiance should be allies");

    a.allegiance = ALLEGIANCE_NEUTRAL;
    b.allegiance = ALLEGIANCE_PLAYER;
    if (P_IsAlly(&a, &b)) return fail("neutral should not be ally of player");

    a.allegiance = ALLEGIANCE_PLAYER;
    b.allegiance = ALLEGIANCE_NEUTRAL;
    if (P_IsAlly(&a, &b)) return fail("player should not be ally of neutral");

    if (P_IsAlly(NULL, &b)) return fail("NULL first arg should not be ally");
    if (P_IsAlly(&a, NULL)) return fail("NULL second arg should not be ally");

    return 0;
}

static int assert_is_ally_cross_camp(void) {
    mobj_t a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));

    a.owner = 0;
    b.owner = 0;
    a.allegiance = ALLEGIANCE_PLAYER;
    b.allegiance = ALLEGIANCE_PLAYER;
    if (!P_IsAlly(&a, &b)) return fail("same owner same allegiance should be allies");

    a.owner = 0;
    b.owner = 2;
    a.allegiance = ALLEGIANCE_PLAYER;
    b.allegiance = ALLEGIANCE_ALLIED;
    if (!P_IsAlly(&a, &b)) return fail("player and allied should be allies");

    a.owner = 0;
    b.owner = 1;
    a.allegiance = ALLEGIANCE_PLAYER;
    b.allegiance = ALLEGIANCE_ENEMY;
    if (P_IsAlly(&a, &b)) return fail("player and enemy owner should not be allies");

    return 0;
}

static int assert_ai_init(void) {
    AiContext ctx;
    P_AiInit(&ctx);
    if (!ctx.initialized) return fail("AI context should be initialized");
    for (int t = 0; t < AI_MAX_TEAMS; ++t) {
        if (ctx.teams[t].attack_wave_timer_ms != AI_ATTACK_WAVE_INTERVAL_MS)
            return fail("attack wave timer should be initialized");
        if (ctx.teams[t].harvest_assignment_count != 0)
            return fail("harvest assignments should be zero");
        if (ctx.teams[t].has_base)
            return fail("has_base should be false initially");
    }
    return 0;
}

static int assert_ai_tick_no_crash(void) {
    AiContext ctx;
    P_AiInit(&ctx);
    level_t map;
    memset(&map, 0, sizeof(map));
    mobj_t units[4];
    memset(units, 0, sizeof(units));
    P_AiTick(NULL, &map, units, 4, NULL, 16);
    P_AiTick(&ctx, NULL, units, 4, NULL, 16);
    P_AiTick(&ctx, &map, NULL, 0, NULL, 16);
    P_AiTick(&ctx, &map, units, 0, NULL, 16);
    return 0;
}

static int assert_ai_detects_base(void) {
    AiContext ctx;
    P_AiInit(&ctx);
    level_t map;
    memset(&map, 0, sizeof(map));

    mobj_t units[2];
    memset(units, 0, sizeof(units));
    units[0].owner = 0;
    units[0].hp = 100;
    units[0].allegiance = ALLEGIANCE_PLAYER;
    units[0].traits = MF_RESOURCE_BASE;
    units[0].core.position = (fixed3_t){ 10 << 16, 20 << 16, 0 };

    units[1].owner = 1;
    units[1].hp = 100;
    units[1].allegiance = ALLEGIANCE_ENEMY;
    units[1].traits = MF_RESOURCE_BASE;
    units[1].core.position = (fixed3_t){ 50 << 16, 60 << 16, 0 };

    P_AiTick(&ctx, &map, units, 2, NULL, 16);

    if (!ctx.teams[0].has_base) return fail("team 0 should detect its base");
    if (!ctx.teams[1].has_base) return fail("team 1 should detect its base");
    if (ctx.teams[2].has_base) return fail("team 2 should have no base");

    return 0;
}

static int assert_ai_counts_units(void) {
    AiContext ctx;
    P_AiInit(&ctx);
    level_t map;
    memset(&map, 0, sizeof(map));

    mobj_t units[3];
    memset(units, 0, sizeof(units));
    units[0].owner = 0;
    units[0].hp = 100;
    units[0].allegiance = ALLEGIANCE_PLAYER;
    units[0].traits = MF_RESOURCE_BASE | MF_MOBILE;
    units[0].core.position = (fixed3_t){ 10 << 16, 10 << 16, 0 };

    units[1].owner = 0;
    units[1].hp = 100;
    units[1].allegiance = ALLEGIANCE_PLAYER;
    units[1].traits = MF_ATTACK | MF_MOBILE;
    units[1].core.position = (fixed3_t){ 11 << 16, 10 << 16, 0 };

    units[2].owner = 0;
    units[2].hp = 100;
    units[2].allegiance = ALLEGIANCE_PLAYER;
    units[2].traits = MF_HARVESTER | MF_MOBILE;
    units[2].core.position = (fixed3_t){ 12 << 16, 10 << 16, 0 };

    P_AiTick(&ctx, &map, units, 3, NULL, 16);

    if (ctx.teams[0].combat_unit_count != 1)
        return fail("should count 1 combat unit");
    if (ctx.teams[0].harvester_count != 1)
        return fail("should count 1 harvester");

    return 0;
}

static int assert_ai_defense_rally(void) {
    AiContext ctx;
    P_AiInit(&ctx);
    level_t map;
    memset(&map, 0, sizeof(map));
    map.width = 100;
    map.height = 100;

    mobj_t units[3];
    memset(units, 0, sizeof(units));

    units[0].owner = 0;
    units[0].hp = 100;
    units[0].allegiance = ALLEGIANCE_PLAYER;
    units[0].traits = MF_RESOURCE_BASE | MF_MOBILE;
    units[0].core.position = (fixed3_t){ 10 << 16, 10 << 16, 0 };

    units[1].owner = 0;
    units[1].hp = 100;
    units[1].allegiance = ALLEGIANCE_PLAYER;
    units[1].traits = MF_ATTACK | MF_MOBILE;
    units[1].core.position = (fixed3_t){ 11 << 16, 10 << 16, 0 };
    units[1].movement.order_arrived = true;

    units[2].owner = 1;
    units[2].hp = 100;
    units[2].allegiance = ALLEGIANCE_ENEMY;
    units[2].traits = MF_ATTACK | MF_MOBILE;
    units[2].core.position = (fixed3_t){ 12 << 16, 10 << 16, 0 };

    P_AiTick(&ctx, &map, units, 3, NULL, 16);

    if (units[1].attack.target != 2)
        return fail("defender should target the enemy");

    return 0;
}

static int assert_ai_attack_wave_timer(void) {
    AiContext ctx;
    P_AiInit(&ctx);
    level_t map;
    memset(&map, 0, sizeof(map));

    mobj_t units[2];
    memset(units, 0, sizeof(units));
    units[0].owner = 0;
    units[0].hp = 100;
    units[0].allegiance = ALLEGIANCE_PLAYER;
    units[0].traits = MF_RESOURCE_BASE | MF_MOBILE;
    units[0].core.position = (fixed3_t){ 10 << 16, 10 << 16, 0 };

    units[1].owner = 1;
    units[1].hp = 100;
    units[1].allegiance = ALLEGIANCE_ENEMY;
    units[1].traits = MF_RESOURCE_BASE | MF_MOBILE;
    units[1].core.position = (fixed3_t){ 50 << 16, 50 << 16, 0 };

    int timer_before = ctx.teams[0].attack_wave_timer_ms;
    P_AiTick(&ctx, &map, units, 2, NULL, 16);
    int timer_after = ctx.teams[0].attack_wave_timer_ms;

    if (timer_after >= timer_before)
        return fail("attack wave timer should decrease");

    return 0;
}

static int assert_allegiance_targeting(void) {
    AiContext ctx;
    P_AiInit(&ctx);
    level_t map;
    memset(&map, 0, sizeof(map));
    map.width = 100;
    map.height = 100;

    mobj_t units[4];
    memset(units, 0, sizeof(units));

    units[0].owner = 0;
    units[0].hp = 100;
    units[0].allegiance = ALLEGIANCE_PLAYER;
    units[0].traits = MF_RESOURCE_BASE | MF_MOBILE;
    units[0].core.position = (fixed3_t){ 10 << 16, 10 << 16, 0 };

    units[1].owner = 0;
    units[1].hp = 100;
    units[1].allegiance = ALLEGIANCE_PLAYER;
    units[1].traits = MF_ATTACK | MF_MOBILE;
    units[1].core.position = (fixed3_t){ 11 << 16, 10 << 16, 0 };
    units[1].movement.order_arrived = true;

    units[2].owner = 1;
    units[2].hp = 100;
    units[2].allegiance = ALLEGIANCE_ALLIED;
    units[2].traits = MF_ATTACK | MF_MOBILE;
    units[2].core.position = (fixed3_t){ 12 << 16, 10 << 16, 0 };

    units[3].owner = 2;
    units[3].hp = 100;
    units[3].allegiance = ALLEGIANCE_ENEMY;
    units[3].traits = MF_ATTACK | MF_MOBILE;
    units[3].core.position = (fixed3_t){ 15 << 16, 10 << 16, 0 };

    P_AiTick(&ctx, &map, units, 4, NULL, 16);

    if (units[1].attack.target == 2)
        return fail("should not target allied unit");

    if (units[1].attack.target != 3)
        return fail("should target enemy unit");

    return 0;
}

static int assert_harvesting_assignment(void) {
    AiContext ctx;
    P_AiInit(&ctx);
    level_t map;
    memset(&map, 0, sizeof(map));
    map.width = 100;
    map.height = 100;

    resourcevent_t vents[1];
    memset(vents, 0, sizeof(vents));
    vents[0].active = true;
    vents[0].amount = 1000;
    vents[0].rate = 10;
    vents[0].attachment = (fvec2_t){ 20.5f, 20.5f };
    vents[0].cell = (ivec2_t){ 20, 20 };
    map.resource_vents = vents;
    map.resource_vent_count = 1;

    mobj_t units[2];
    memset(units, 0, sizeof(units));

    units[0].owner = 0;
    units[0].hp = 100;
    units[0].allegiance = ALLEGIANCE_PLAYER;
    units[0].traits = MF_RESOURCE_BASE | MF_MOBILE;
    units[0].core.position = (fixed3_t){ 10 << 16, 10 << 16, 0 };

    units[1].owner = 0;
    units[1].hp = 100;
    units[1].allegiance = ALLEGIANCE_PLAYER;
    units[1].traits = MF_HARVESTER | MF_MOBILE;
    units[1].core.position = (fixed3_t){ 15 << 16, 15 << 16, 0 };
    units[1].attack.target = -1;
    units[1].harvest.target = -1;

    P_AiTick(&ctx, &map, units, 2, NULL, 16);

    if (ctx.teams[0].harvest_assignment_count != 1)
        return fail("should assign 1 slug to harvest");
    if (ctx.teams[0].harvest_assignments[0].vent_index != 0)
        return fail("should assign to vent 0");

    return 0;
}

static int assert_defense_trigger(void) {
    AiContext ctx;
    P_AiInit(&ctx);
    level_t map;
    memset(&map, 0, sizeof(map));
    map.width = 100;
    map.height = 100;

    mobj_t units[3];
    memset(units, 0, sizeof(units));

    units[0].owner = 0;
    units[0].hp = 100;
    units[0].allegiance = ALLEGIANCE_PLAYER;
    units[0].traits = MF_RESOURCE_BASE | MF_MOBILE;
    units[0].core.position = (fixed3_t){ 50 << 16, 50 << 16, 0 };

    units[1].owner = 0;
    units[1].hp = 100;
    units[1].allegiance = ALLEGIANCE_PLAYER;
    units[1].traits = MF_ATTACK | MF_MOBILE;
    units[1].core.position = (fixed3_t){ 52 << 16, 50 << 16, 0 };
    units[1].movement.order_arrived = true;

    units[2].owner = 2;
    units[2].hp = 100;
    units[2].allegiance = ALLEGIANCE_ENEMY;
    units[2].traits = MF_ATTACK | MF_MOBILE;
    units[2].core.position = (fixed3_t){ 55 << 16, 50 << 16, 0 };

    P_AiTick(&ctx, &map, units, 3, NULL, 16);

    if (units[1].attack.target != 2)
        return fail("defender should intercept enemy near base");

    return 0;
}

static int assert_attack_wave_dispatch(void) {
    AiContext ctx;
    P_AiInit(&ctx);
    ctx.teams[0].attack_wave_timer_ms = 0;

    level_t map;
    memset(&map, 0, sizeof(map));
    map.width = 100;
    map.height = 100;

    mobj_t units[4];
    memset(units, 0, sizeof(units));

    units[0].owner = 0;
    units[0].hp = 100;
    units[0].allegiance = ALLEGIANCE_PLAYER;
    units[0].traits = MF_RESOURCE_BASE | MF_MOBILE;
    units[0].core.position = (fixed3_t){ 10 << 16, 10 << 16, 0 };

    units[1].owner = 0;
    units[1].hp = 100;
    units[1].allegiance = ALLEGIANCE_PLAYER;
    units[1].traits = MF_ATTACK | MF_MOBILE;
    units[1].core.position = (fixed3_t){ 12 << 16, 10 << 16, 0 };
    units[1].movement.order_arrived = true;

    units[2].owner = 2;
    units[2].hp = 100;
    units[2].allegiance = ALLEGIANCE_ENEMY;
    units[2].traits = MF_RESOURCE_BASE | MF_MOBILE;
    units[2].core.position = (fixed3_t){ 80 << 16, 80 << 16, 0 };

    units[3].owner = 2;
    units[3].hp = 100;
    units[3].allegiance = ALLEGIANCE_ENEMY;
    units[3].traits = MF_ATTACK | MF_MOBILE;
    units[3].core.position = (fixed3_t){ 82 << 16, 80 << 16, 0 };

    P_AiTick(&ctx, &map, units, 4, NULL, 16);

    if (units[1].attack.target != 2)
        return fail("attack wave should target enemy base");

    return 0;
}

static int assert_allegiance_team_detection(void) {
    AiContext ctx;
    P_AiInit(&ctx);
    level_t map;
    memset(&map, 0, sizeof(map));

    mobj_t units[3];
    memset(units, 0, sizeof(units));

    units[0].owner = 0;
    units[0].hp = 100;
    units[0].allegiance = ALLEGIANCE_PLAYER;
    units[0].traits = MF_RESOURCE_BASE | MF_MOBILE;
    units[0].core.position = (fixed3_t){ 10 << 16, 10 << 16, 0 };

    units[1].owner = 1;
    units[1].hp = 100;
    units[1].allegiance = ALLEGIANCE_ALLIED;
    units[1].traits = MF_RESOURCE_BASE | MF_MOBILE;
    units[1].core.position = (fixed3_t){ 30 << 16, 30 << 16, 0 };

    units[2].owner = 2;
    units[2].hp = 100;
    units[2].allegiance = ALLEGIANCE_ENEMY;
    units[2].traits = MF_RESOURCE_BASE | MF_MOBILE;
    units[2].core.position = (fixed3_t){ 60 << 16, 60 << 16, 0 };

    P_AiTick(&ctx, &map, units, 3, NULL, 16);

    if (ctx.teams[0].allegiance != ALLEGIANCE_PLAYER)
        return fail("team 0 should have PLAYER allegiance");
    if (ctx.teams[1].allegiance != ALLEGIANCE_ALLIED)
        return fail("team 1 should have ALLIED allegiance");
    if (ctx.teams[2].allegiance != ALLEGIANCE_ENEMY)
        return fail("team 2 should have ENEMY allegiance");

    return 0;
}

int main(void) {
    RTS_RUN(assert_is_ally_basic());
    RTS_RUN(assert_is_ally_cross_camp());
    RTS_RUN(assert_ai_init());
    RTS_RUN(assert_ai_tick_no_crash());
    RTS_RUN(assert_ai_detects_base());
    RTS_RUN(assert_ai_counts_units());
    RTS_RUN(assert_ai_defense_rally());
    RTS_RUN(assert_ai_attack_wave_timer());
    RTS_RUN(assert_allegiance_targeting());
    RTS_RUN(assert_harvesting_assignment());
    RTS_RUN(assert_defense_trigger());
    RTS_RUN(assert_attack_wave_dispatch());
    RTS_RUN(assert_allegiance_team_detection());
    printf("All AI team-aware tests passed.\n");
    return 0;
}
