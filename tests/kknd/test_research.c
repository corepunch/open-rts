#include "game.h"
#include "kknd.h"
#include "info.h"
#include "rts_test.h"
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
    mobj_t *barracks = spawn(MT_SURV_BARRACKS);
    mobj_t *second = spawn(MT_SURV_BARRACKS);
    const StaticProductDefinition *rifle = G_ModelProductByUIId(NULL,1);
    const StaticProductDefinition *swat = G_ModelProductByUIId(NULL,5);
    assert(G_ModelProductAvailable(NULL,0,rifle));
    assert(!G_ModelProductAvailable(NULL,0,swat));
    assert(!KK_Research(barracks));
    mobj_t *lab = spawn(MT_SURV_RESEARCH_LAB);
    assert(!G_ModelProductAvailable(NULL,0,G_ModelProductByUIId(NULL,25))); /* One lab. */
    mobj_t *outpost = spawn(MT_SURV_OUTPOST);
    assert(G_ModelRadarLevel(0) == 0 && KK_NextTechLevel(outpost) == 1);
    uint32_t hash = G_Consistency();
    outpost->research.level = 1;
    assert(G_ModelRadarLevel(0) == 1 && KK_NextTechLevel(outpost) == 2);
    assert(G_Consistency() != hash);
    outpost->research.level = 2;
    assert(G_ModelRadarLevel(0) == 2);
    level.player_resources[0][0] = 2000;
    assert(G_BuildOrder(barracks,KKND_RESEARCH));
    assert(lab->research.target == barracks->id);
    assert(lab->research.total_cost == 750 && lab->research.total_time == 700);
    ticks(700*RTS_TICRATE/25);
    assert(barracks->research.level == 1 && lab->research.target == 0);
    assert(level.player_resources[0][0] == 1250);
    assert(G_ModelProductAvailable(NULL,0,swat));
    assert(G_ModelProducerHasTech(barracks,swat));
    assert(!G_ModelProducerHasTech(second,swat));
    assert(!G_QueueProduct(second,swat));
    assert(G_QueueProduct(barracks,swat));
    /* Research runs independently of production, pauses on insufficient cash,
       and cancellation preserves only the money already spent. */
    assert(KK_Research(barracks));
    level.player_resources[0][0] = 0;
    ticks(60);
    int left = lab->research.remaining_time;
    ticks(60);
    assert(lab->research.remaining_time == left && barracks->research.level == 1);
    level.player_resources[0][0] = 10000;
    ticks(60);
    assert(lab->research.remaining_time < left);
    int money = level.player_resources[0][0];
    assert(KK_Research(barracks));
    assert(!lab->research.target && barracks->research.level == 1);
    assert(level.player_resources[0][0] == money);
    assert(KK_Research(lab));
    ticks(700*RTS_TICRATE/25);
    assert(lab->research.level == 1);
    assert(KK_Research(barracks));
    assert(lab->research.total_cost == 1150 && lab->research.total_time == 940);
    barracks->hp = 0;
    ticks(2);
    assert(!lab->research.target);
    second->owner = 1;
    assert(!G_BuildOrder(second,KKND_RESEARCH));
    assert(!KK_Research(second));
    P_FreeLevel(&level);
    puts("PASS: producer-local research, exact time/cost, lab upgrades, pause, cancel and ownership");
    return 0;
}
