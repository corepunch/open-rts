#include "t_local.h"
#include "engine.h"
#include "dark-reign.h"
#include "info.h"

#define CHECK(c) RTS_CHECK(c, "Dark Reign upgrades", #c)

/* IsUpgradeOf buildings are bought on the tier they replace and turn that
 * building into the next tier where it stands. */
static mobj_t *spawn(uint16_t type, fixed2_t at) {
    mobj_t *unit = P_SpawnMobj(fixed3_from_fixed2(at, 0), type);
    if (unit) { unit->owner = unit->team = 0; unit->allegiance = ALLEGIANCE_PLAYER; }
    return unit;
}

int main(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {0};
    CHECK(model && rts_game_model_load(model, &config));
    P_FreeThinkers();
    P_InitThinkers();
    level.mission = NULL; /* no campaign tech cap */
    level.player_resources[0][0] = 100000;
    mobj_t *hq = spawn(MT_FG_HQ1, FIXED2_LIT(20, 20));
    mobj_t *barracks = spawn(MT_FG_BARRACKS, FIXED2_LIT(30, 20));
    mobj_t *factory = spawn(MT_FG_VEHICLE_FACTORY, FIXED2_LIT(20, 30));
    mobj_t *rig = spawn(MT_FG_CONSTRUCTION_CREW, FIXED2_LIT(30, 30));
    CHECK(hq && barracks && factory && rig);
    const StaticProductDefinition *hq2 = G_ModelProductByUIId(NULL, 10002);
    CHECK(hq2 && DR_IsUpgrade(hq2));
    CHECK(DR_UpgradeOf(MT_FG_HQ1) == MT_FG_HQ2 && DR_UpgradeOf(MT_FG_HQ2) == MT_FG_HQ3);
    CHECK(DR_UpgradeOf(MT_FG_HQ3) == MT_NULL && DR_UpgradeOf(MT_IMP_BARRACKS) == MT_IMP_ADV_BARRACKS);
    CHECK(!DR_IsUpgrade(G_ModelProductByUIId(NULL, 10004)));
    /* The rig no longer puts up a second HQ; the HQ upgrades itself. */
    CHECK(!G_QueueProduct(rig, hq2));
    hq->hp = hq->max_hp / 2;
    uint32_t id = hq->id;
    int credits = level.player_resources[0][0];
    CHECK(G_QueueProduct(hq, hq2));
    CHECK(level.player_resources[0][0] == credits - hq2->cost);
    int ms = G_ModelProductTrainingTimeMs(hq2);
    CHECK(ms > 0);
    for (int t = 0; t <= ms + 1000 && hq->type_id == MT_FG_HQ1; t += RTS_TICK_MS) G_ProductionTicker(RTS_TICK_MS);
    CHECK(hq->id == id && hq->type_id == MT_FG_HQ2 && !hq->remove);
    CHECK(hq->max_hp == P_ActorType(MT_FG_HQ2)->max_hp);
    CHECK(hq->hp == hq->max_hp / 2);
    CHECK(!hq->production || hq->production->queue_count == 0);
    CHECK(G_CountPlannedActors(0, MT_FG_HQ1) == 0 && G_CountPlannedActors(0, MT_FG_HQ2) == 1);
    /* An HQ 2 still satisfies what needed an HQ 1. */
    CHECK(DR_PrerequisiteMet(0, 10001));
    /* The Advanced Barracks comes from the barracks the same way. */
    const StaticProductDefinition *adv = G_ModelProductByUIId(NULL, 10005);
    CHECK(adv && G_QueueProduct(barracks, adv));
    for (int t = 0; t <= G_ModelProductTrainingTimeMs(adv) + 1000 && barracks->type_id == MT_FG_BARRACKS;
         t += RTS_TICK_MS) G_ProductionTicker(RTS_TICK_MS);
    CHECK(barracks->type_id == MT_FG_ADV_BARRACKS && barracks->hp == barracks->max_hp);
    rts_game_model_destroy(model);
    printf("PASS: HQ and barracks upgrade in place, keep their share of health and count as the old tier\n");
    return 0;
}
