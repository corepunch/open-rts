#include "game.h"
#include "kknd.h"
#include "info.h"
#include "d_net.h"
#include <assert.h>

static mobj_t *spawn(uint16_t type) {
    mobj_t *u = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){20,20},0),type);
    assert(u);
    return u;
}
static void ticks(int count) { while (count--) P_RunThinkers(); }

int main(void) {
    P_InitThinkers();
    level.width = level.height = 96;
    level.blocked = calloc(96*96,1);
    assert(level.blocked);
    mobj_t *outpost = spawn(MT_SURV_OUTPOST);
    mobj_t *second = spawn(MT_SURV_OUTPOST);
    assert(G_ModelRadarLevel(0) == 0 && KK_NextTechLevel(outpost) == 1);
    assert(!KK_Research(outpost));
    mobj_t *lab = spawn(MT_SURV_RESEARCH_LAB);
    uint32_t hash = G_Consistency();
    level.player_resources[0][0] = 2000;
    assert(G_BuildOrder(outpost,KKND_RESEARCH));
    assert(lab->research.target == outpost->id);
    assert(lab->research.total_cost == 750 && lab->research.total_time == 700);
    ticks(700*RTS_TICRATE/25);
    assert(outpost->research.level == 1 && lab->research.target == 0);
    assert(second->research.level == 0);
    assert(G_ModelRadarLevel(0) == 1 && KK_NextTechLevel(outpost) == 2);
    assert(G_Consistency() != hash);
    assert(level.player_resources[0][0] == 1250);
    /* Research pauses on insufficient cash; cancellation retains spent oil. */
    assert(KK_Research(outpost));
    level.player_resources[0][0] = 0;
    ticks(60);
    int left = lab->research.remaining_time;
    ticks(60);
    assert(lab->research.remaining_time == left && outpost->research.level == 1);
    level.player_resources[0][0] = 10000;
    ticks(60);
    assert(lab->research.remaining_time < left);
    int money = level.player_resources[0][0];
    assert(KK_Research(outpost));
    assert(!lab->research.target && outpost->research.level == 1);
    assert(level.player_resources[0][0] == money);
    assert(KK_Research(lab));
    ticks(700*RTS_TICRATE/25);
    assert(lab->research.level == 1);
    assert(KK_Research(outpost));
    assert(lab->research.total_cost == 1150 && lab->research.total_time == 940);
    ticks(940*RTS_TICRATE/25);
    assert(G_ModelRadarLevel(0) == 2 && !KK_NextTechLevel(outpost));
    assert(KK_Research(second));
    second->hp = 0;
    ticks(2);
    assert(!lab->research.target);
    outpost->owner = 1;
    assert(!G_BuildOrder(outpost,KKND_RESEARCH));
    assert(!KK_Research(outpost));
    P_FreeLevel(&level);
    puts("PASS: native base research, radar, exact time/cost, lab upgrades, pause, cancel and ownership");
    return 0;
}
