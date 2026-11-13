#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <stdlib.h>

#define CHECK(c) RTS_CHECK(c,"StarCraft foundation readiness",#c)
enum { SC_TEST_DARK_TEMPLAR = 75 };

static mobj_t *spawn(int type,fixed2_t at,int owner) {
    mobj_t *unit=P_SpawnMobj(fixed3_from_fixed2(at,0),(uint16_t)type);
    if(unit) {
        unit->owner=unit->team=(uint8_t)owner;
        unit->allegiance=owner==consoleplayer?ALLEGIANCE_PLAYER:ALLEGIANCE_ENEMY;
    }
    return unit;
}

static void tick(void) { P_Ticker();G_ProductionTicker(RTS_TICK_MS); }

int main(void) {
    G_InitGame();
    P_InitThinkers();
    consoleplayer=0;
    level.width=level.height=32;
    level.blocked=calloc(32*32,1);
    level.cell_solid=calloc(32*32,1);
    level.mission=calloc(1,sizeof(sc_mission_t));
    CHECK(level.blocked&&level.cell_solid&&level.mission);

    mobj_t *turret=spawn(MT_MISSILE_TURRET,FIXED2_LIT(8,8),0);
    mobj_t *enemy=spawn(MT_WRAITH,FIXED2_LIT(11,8),1);
    CHECK(turret&&enemy&&turret->production==NULL);
    turret->production=calloc(1,sizeof(*turret->production));
    CHECK(turret->production);
    *turret->production=(production_t){.actor_id=MT_MISSILE_TURRET,
        .product_class=RTS_PRODUCT_BUILDING,.product_type=MT_MISSILE_TURRET,
        .queue_count=1,.time_ms=1000,.time_left_ms=1000,.placed=true};

    CHECK(!G_ModelUnitReady(turret));
    CHECK(P_InitSight());
    P_UpdateSight();
    uint32_t team_zero=UINT32_C(0x40000000);
    CHECK(level.sight.cells[L_Index(&level,12,8)]&team_zero);
    CHECK(!(level.sight.cells[L_Index(&level,13,8)]&team_zero));
    CHECK(!(P_Detectors(enemy)&team_zero));
    CHECK(P_AttackTarget(turret)==NULL);
    A_Look(turret);
    CHECK(!turret->attack.target);
    turret->attack.target=enemy;
    CHECK(!P_Attack(turret));
    CHECK(enemy->hp==enemy->max_hp&&!turret->attack.target);

    P_FreeMobjProduction(turret);
    CHECK(G_ModelUnitReady(turret));
    CHECK(P_AttackTarget(turret)==enemy);
    CHECK(P_Attack(turret)&&turret->attack.target==enemy);

    mobj_t *cloaked=spawn(SC_TEST_DARK_TEMPLAR,FIXED2_LIT(10,8),1);
    mobj_t *observer=spawn(MT_MARINE,FIXED2_LIT(9,8),0);
    CHECK(cloaked&&observer&&(cloaked->traits&MF_CLOAKED));
    turret->production=calloc(1,sizeof(*turret->production));
    CHECK(turret->production);
    *turret->production=(production_t){.actor_id=MT_MISSILE_TURRET,
        .product_class=RTS_PRODUCT_BUILDING,.product_type=MT_MISSILE_TURRET,
        .queue_count=1,.time_ms=1000,.time_left_ms=1000,.placed=true};
    P_UpdateSight();
    CHECK(!G_ModelUnitReady(turret));
    CHECK(!(P_Detectors(cloaked)&UINT32_C(0x40000000)));
    CHECK(!P_VisibleTo(observer,cloaked));
    P_FreeMobjProduction(turret);
    P_UpdateSight();
    CHECK((P_Detectors(cloaked)&UINT32_C(0x40000000))&&P_VisibleTo(observer,cloaked));

    mobj_t *hatchery=spawn(MT_HATCHERY,FIXED2_LIT(8,20),0);
    mobj_t *colony=spawn(MT_CREEP_COLONY,FIXED2_LIT(23,20),0);
    CHECK(hatchery&&colony);
    hatchery->production=calloc(1,sizeof(*hatchery->production));
    colony->production=calloc(1,sizeof(*colony->production));
    CHECK(hatchery->production&&colony->production);
    *hatchery->production=(production_t){.actor_id=MT_HATCHERY,
        .product_class=RTS_PRODUCT_BUILDING,.product_type=MT_HATCHERY,
        .queue_count=1,.time_ms=1000,.time_left_ms=1000,.placed=true};
    *colony->production=(production_t){.actor_id=MT_CREEP_COLONY,
        .product_class=RTS_PRODUCT_BUILDING,.product_type=MT_CREEP_COLONY,
        .queue_count=1,.time_ms=1000,.time_left_ms=1000,.placed=true};
    /* A foundation holds no creep: OpenBW gives a Hatchery or Creep Colony its
     * footprint and ring only when it completes (set_creep_building_tiles).
     * Creep lives in tile state, so the tics must run for it to appear. */
    tick();tick();
    CHECK(!sc_ground_ok(MT_SPAWNING_POOL,(ivec2_t){9,20},0));
    CHECK(!sc_ground_ok(MT_CREEP_COLONY,(ivec2_t){22,19},0));
    P_FreeMobjProduction(hatchery);
    P_FreeMobjProduction(colony);
    tick();
    CHECK(sc_ground_ok(MT_SPAWNING_POOL,(ivec2_t){9,20},0)); /* inside the Hatchery's ring */
    CHECK(sc_ground_ok(MT_CREEP_COLONY,(ivec2_t){22,19},0)); /* the Colony's footprint */
    /* A completed Hatchery remains operational during its in-place Lair morph. */
    hatchery->production=calloc(1,sizeof(*hatchery->production));
    CHECK(hatchery->production);
    *hatchery->production=(production_t){.actor_id=MT_LAIR,
        .product_class=RTS_PRODUCT_BUILDING,.product_type=MT_LAIR,
        .queue_count=1,.time_ms=1000,.time_left_ms=1000};
    CHECK(G_ModelUnitReady(hatchery)&&sc_ground_ok(MT_SPAWNING_POOL,(ivec2_t){9,20},0));
    P_FreeMobjProduction(hatchery);

    mobj_t *depot=spawn(MT_COMMAND_CENTER,FIXED2_LIT(7,28),0);
    mobj_t *worker=spawn(MT_SCV,FIXED2_LIT(10,28),0);
    CHECK(depot&&worker&&(depot->traits&MF_RESOURCE_BASE));
    depot->production=calloc(1,sizeof(*depot->production));
    CHECK(depot->production);
    *depot->production=(production_t){.actor_id=MT_COMMAND_CENTER,
        .product_class=RTS_PRODUCT_BUILDING,.product_type=MT_COMMAND_CENTER,
        .queue_count=1,.time_ms=1000,.time_left_ms=1000,.placed=true};
    worker->harvest.resource_type=0;
    worker->harvest.cargo=8;
    int minerals_before=level.player_resources[0][0];
    CHECK(!P_ReturnHarvest(&level,worker,depot));
    CHECK(worker->harvest.cargo==8&&level.player_resources[0][0]==minerals_before);
    P_FreeMobjProduction(depot);
    CHECK(P_ReturnHarvest(&level,worker,depot));
    for(int i=0;i<300&&level.player_resources[0][0]==minerals_before;i++) {
        P_Ticker();
        G_ProductionTicker(RTS_TICK_MS);
    }
    CHECK(level.player_resources[0][0]==minerals_before+8&&worker->harvest.cargo==0);

    mobj_t *bunker=spawn(MT_BUNKER,FIXED2_LIT(21,8),0);
    mobj_t *marine=spawn(MT_MARINE,FIXED2_LIT(21,8),0);
    CHECK(bunker&&marine);
    bunker->production=calloc(1,sizeof(*bunker->production));
    CHECK(bunker->production);
    *bunker->production=(production_t){.actor_id=MT_BUNKER,
        .product_class=RTS_PRODUCT_BUILDING,.product_type=MT_BUNKER,
        .queue_count=1,.time_ms=1000,.time_left_ms=1000,.placed=true};
    CHECK(!sc_can_board(marine,bunker)&&!sc_board(marine,bunker));
    P_FreeMobjProduction(bunker);
    CHECK(sc_can_board(marine,bunker)&&sc_board(marine,bunker));
    CHECK((marine->sc.flags&SC_LOADED)&&marine->sc.parent==bunker->id);

    P_FreeLevel(&level);
    puts("PASS: StarCraft incomplete buildings wait for operational readiness");
    return 0;
}
