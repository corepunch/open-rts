#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <stdlib.h>

#define CHECK(c) RTS_CHECK(c,"StarCraft construction",#c)

static mobj_t *spawn(int type,fixed2_t at,int owner) {
    mobj_t *unit=P_SpawnMobj(fixed3_from_fixed2(at,0),(uint16_t)type);
    if(unit)unit->owner=unit->team=(uint8_t)owner;
    return unit;
}

static void tick(void) { P_Ticker(); G_ProductionTicker(RTS_TICK_MS); }

static mobj_t *site_of(uint16_t type) {
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        mobj_t *unit=(mobj_t *)th;
        if(th->function==P_MobjThinker&&!unit->remove&&unit->type_id==type&&
           unit->production&&unit->production->placed)return unit;
    }
    return NULL;
}

static bool reach_site(uint16_t type,mobj_t *morphing) {
    for(int i=0;i<1000;i++) {
        if(site_of(type)||(morphing&&morphing->type_id==type))return true;
        tick();
    }
    return false;
}

static int terran(void) {
    P_FreeThinkers();P_InitThinkers();
    memset(level.cell_solid,0,(size_t)level.width*level.height);
    level.player_resources[0][0]=500;
    mobj_t *base=spawn(MT_COMMAND_CENTER,FIXED2_LIT(4.5,4.5),0);
    mobj_t *scv=spawn(MT_SCV,FIXED2_LIT(7.5,7.5),0);
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,MT_BARRACKS);
    CHECK(base&&scv&&product&&G_PlaceProduct(scv,product,(ivec2_t){18,12}));
    CHECK(scv->production&&scv->production->placed&&level.player_resources[0][0]==500);
    CHECK(reach_site(MT_BARRACKS,NULL));
    mobj_t *site=site_of(MT_BARRACKS);
    CHECK(site&&site->hp>0&&site->hp<site->max_hp&&site->target==scv);
    CHECK(level.player_resources[0][0]==500-product->cost);
    CHECK(!G_ModelHasActorType(NULL,0,MT_BARRACKS));
    CHECK(scv->target==site&&scv->sc.queue.active==TC_CONSTRUCT&&scv->sc.queue.target==site->id);
    int hp=site->hp;
    P_DamageMobj(site,NULL,10);
    CHECK(site->hp==hp-10);
    int paused=site->production->time_left_ms;
    uint32_t old_scv_id=scv->id;
    P_RemoveMobj(scv);P_RunThinkers();
    CHECK(!P_MobjById(old_scv_id));
    CHECK(!site->target);
    tick();
    CHECK(site->production->time_left_ms==paused);
    mobj_t *replacement=spawn(MT_SCV,FIXED2_LIT(7.5,7.5),0);
    CHECK(replacement);
    ticcmd_t resume={.order=TC_CONSTRUCT,.product=MT_BARRACKS,.count=1,
        .units={replacement->id},
        .position=fixed3_from_fixed2(fixed2_cell_center((ivec2_t){18,12}),0)};
    ticcmd_t right_click={.order=TC_ORDER,.count=1,.units={replacement->id},.target=site->id,
        .position=fixed3_from_fixed2(fixed3_xy(site->core.position),0)};
    G_RunTiccmd(0,&right_click);
    CHECK(site->target==replacement&&replacement->sc.queue.target==site->id);
    paused=site->production->time_left_ms;
    tick();
    CHECK(site->production->time_left_ms==paused);
    ticcmd_t leave={.order=TC_MOVE,.count=1,.units={replacement->id},
        .position=fixed3_from_fixed2(FIXED2_LIT(2,2),0)};
    G_RunTiccmd(0,&leave);
    tick();
    CHECK(site->production->time_left_ms==paused&&!site->target);
    mobj_t *replacement2=spawn(MT_SCV,FIXED2_LIT(7.5,7.5),0);
    CHECK(replacement2);
    resume.units[0]=replacement2->id;
    G_RunTiccmd(0,&resume);
    CHECK(site->target==replacement2&&replacement2->sc.queue.target==site->id);
    ticcmd_t queued_move={.order=TC_MOVE,.queued=true,.count=1,.units={replacement2->id},
        .position=fixed3_from_fixed2(FIXED2_LIT(2,2),0)};
    G_RunTiccmd(0,&queued_move);
    CHECK(replacement2->sc.queue.count==1&&site->target==replacement2);
    paused=site->production->time_left_ms;
    uint32_t site_id=site->id;
    for(int i=0;i<4000&&site->production;i++)tick();
    CHECK(!site->production&&G_ModelHasActorType(NULL,0,MT_BARRACKS));
    CHECK(site->hp<site->max_hp&&!P_MobjById(site_id)->production);
    CHECK(replacement2->sc.queue.count==1&&!replacement2->sc.queue.active);
    tick();
    CHECK(replacement2->sc.queue.active==TC_MOVE&&P_HasMoveOrder(replacement2));
    CHECK(!replacement->target&&replacement->sc.queue.active!=TC_CONSTRUCT&&
          replacement->sc.queue.target!=site_id);
    CHECK(!replacement2->target&&replacement2->sc.queue.active==TC_MOVE);
    return 0;
}

static int blocked_start(void) {
    P_FreeThinkers();P_InitThinkers();
    memset(level.cell_solid,0,(size_t)level.width*level.height);
    level.player_resources[0][0]=500;
    CHECK(spawn(MT_COMMAND_CENTER,FIXED2_LIT(4.5,4.5),0));
    mobj_t *scv=spawn(MT_SCV,FIXED2_LIT(7.5,7.5),0);
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,MT_BARRACKS);
    ivec2_t cell={18,12};
    CHECK(scv&&product);
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_CONSTRUCT,.product=product->ui_id,
        .count=1,.units={scv->id},.position=fixed3_from_fixed2(FIXED2_LIT(18,12),0)});
    CHECK(scv->production&&scv->sc.queue.active==TC_CONSTRUCT);
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_MOVE,.queued=true,.count=1,.units={scv->id},
        .position=fixed3_from_fixed2(FIXED2_LIT(2,2),0)});
    CHECK(scv->sc.queue.count==1);
    CHECK(level.player_resources[0][0]==500);
    fixed2_t at=P_BuildingPosition(MT_BARRACKS,cell);
    mobj_t *blocker=spawn(MT_BARRACKS,at,1);
    CHECK(blocker);P_SyncBuildingBlocking();
    for(int i=0;i<1000&&scv->production;i++)tick();
    CHECK(!scv->production&&!site_of(MT_BARRACKS));
    CHECK(level.player_resources[0][0]==500);
    tick();
    CHECK(scv->sc.queue.active==TC_MOVE&&P_HasMoveOrder(scv));
    return 0;
}

static int protoss(void) {
    P_FreeThinkers();P_InitThinkers();
    memset(level.cell_solid,0,(size_t)level.width*level.height);
    level.player_resources[0][0]=500;
    mobj_t *probe=spawn(MT_PROBE,FIXED2_LIT(7.5,22.5),0);
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,MT_PYLON);
    CHECK(probe&&product&&G_PlaceProduct(probe,product,(ivec2_t){18,24}));
    CHECK(level.player_resources[0][0]==500);
    CHECK(reach_site(MT_PYLON,NULL));
    mobj_t *site=site_of(MT_PYLON);
    CHECK(site&&site->hp<site->max_hp&&probe->type_id==MT_PROBE&&!probe->production);
    CHECK(level.player_resources[0][0]==500-product->cost);
    CHECK(!probe->target&&!probe->sc.queue.active);
    CHECK(!G_ModelHasActorType(NULL,0,MT_PYLON));
    CHECK(site->sc.shields>0&&site->sc.shields<(sc_units[MT_PYLON-1].shields<<8));
    int before=level.player_resources[0][0];
    uint32_t site_id=site->id;
    CHECK(sc_cancel_production(site,-1));
    CHECK(level.player_resources[0][0]==before+product->cost*3/4);
    P_RunThinkers();
    CHECK(!P_MobjById(site_id));
    return 0;
}

static int pending_interrupts(void) {
    P_FreeThinkers();P_InitThinkers();
    memset(level.cell_solid,0,(size_t)level.width*level.height);
    level.player_resources[0][0]=500;
    CHECK(spawn(MT_COMMAND_CENTER,FIXED2_LIT(4.5,4.5),0));
    mobj_t *stop_worker=spawn(MT_SCV,FIXED2_LIT(7.5,7.5),0);
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,MT_BARRACKS);
    ivec2_t first={18,12};
    CHECK(stop_worker&&product&&G_PlaceProduct(stop_worker,product,first));
    CHECK(level.player_resources[0][0]==500&&stop_worker->production);
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_STOP,.count=1,.units={stop_worker->id}});
    CHECK(!stop_worker->production&&level.player_resources[0][0]==500);

    mobj_t *move_worker=spawn(MT_SCV,FIXED2_LIT(7.5,7.5),0);
    ivec2_t second={18,20};
    CHECK(move_worker&&G_PlaceProduct(move_worker,product,second));
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_MOVE,.count=1,.units={move_worker->id},
        .position=fixed3_from_fixed2(FIXED2_LIT(2,2),0)});
    CHECK(!move_worker->production&&level.player_resources[0][0]==500);
    for(int i=0;i<1000;i++)tick();
    CHECK(!site_of(MT_BARRACKS)&&level.player_resources[0][0]==500);

    mobj_t *cancel_worker=spawn(MT_SCV,FIXED2_LIT(7.5,7.5),0);
    CHECK(cancel_worker&&G_PlaceProduct(cancel_worker,product,(ivec2_t){18,28}));
    CHECK(sc_cancel_production(cancel_worker,-1));
    CHECK(!cancel_worker->production&&level.player_resources[0][0]==500);

    P_FreeThinkers();P_InitThinkers();
    memset(level.cell_solid,0,(size_t)level.width*level.height);
    level.player_resources[0][0]=500;
    CHECK(spawn(MT_COMMAND_CENTER,FIXED2_LIT(4.5,4.5),0));
    mobj_t *ground_worker=spawn(MT_SCV,FIXED2_LIT(7.5,7.5),0);
    CHECK(ground_worker&&G_PlaceProduct(ground_worker,product,(ivec2_t){18,12}));
    ticcmd_t ground={.order=TC_ORDER,.count=1,.units={ground_worker->id},
        .position=fixed3_from_fixed2(FIXED2_LIT(2,2),0)};
    G_RunTiccmd(0,&ground);
    CHECK(!ground_worker->production&&P_HasMoveOrder(ground_worker)&&
          level.player_resources[0][0]==500&&!site_of(MT_BARRACKS));

    P_FreeThinkers();P_InitThinkers();
    memset(level.cell_solid,0,(size_t)level.width*level.height);
    level.player_resources[0][0]=500;
    CHECK(spawn(MT_COMMAND_CENTER,FIXED2_LIT(4.5,4.5),0));
    mobj_t *empty_harvest_worker=spawn(MT_SCV,FIXED2_LIT(7.5,7.5),0);
    CHECK(empty_harvest_worker&&product);
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_CONSTRUCT,.product=product->ui_id,
        .count=1,.units={empty_harvest_worker->id},
        .position=fixed3_from_fixed2(FIXED2_LIT(18,12),0)});
    CHECK(empty_harvest_worker->production);
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_HARVEST,.count=1,
        .units={empty_harvest_worker->id},.position=fixed3_from_fixed2(FIXED2_LIT(2,2),0)});
    CHECK(empty_harvest_worker->production&&level.player_resources[0][0]==500);
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_MOVE,.queued=true,.count=1,
        .units={empty_harvest_worker->id},.position=fixed3_from_fixed2(FIXED2_LIT(3,3),0)});
    CHECK(empty_harvest_worker->sc.queue.count==1);
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_HARVEST,.count=1,
        .units={empty_harvest_worker->id},.position=fixed3_from_fixed2(FIXED2_LIT(2,2),0)});
    CHECK(empty_harvest_worker->production&&empty_harvest_worker->sc.queue.active==TC_CONSTRUCT&&
          empty_harvest_worker->sc.queue.count==1&&
          empty_harvest_worker->sc.queue.pending[0].order==TC_MOVE&&
          level.player_resources[0][0]==500);

    P_FreeThinkers();P_InitThinkers();
    memset(level.cell_solid,0,(size_t)level.width*level.height);
    level.player_resources[0][0]=500;
    CHECK(spawn(MT_COMMAND_CENTER,FIXED2_LIT(4.5,4.5),0));
    mobj_t *mineral_worker=spawn(MT_SCV,FIXED2_LIT(7.5,7.5),0);
    mobj_t *mineral=spawn(MT_MINERAL_FIELD1,FIXED2_LIT(15,6.5),11);
    level.resource_vents=calloc(1,sizeof(*level.resource_vents));
    level.resource_vent_count=1;
    level.resource_vents[0]=(resourcevent_t){.cell={14,6},.footprint={2,1},
        .attachment=FIXED2_LIT(15,6.5),.amount=24,.rate=8,.source_id=mineral->id,.active=true};
    CHECK(mineral_worker&&mineral&&G_PlaceProduct(mineral_worker,product,(ivec2_t){18,12}));
    ticcmd_t harvest={.order=TC_HARVEST,.count=1,.units={mineral_worker->id},
        .position=fixed3_from_fixed2(FIXED2_LIT(15,6.5),0)};
    G_RunTiccmd(0,&harvest);
    CHECK(!mineral_worker->production&&mineral_worker->harvest.phase==HARVEST_PHASE_TO_MINE&&
          level.player_resources[0][0]==500&&!site_of(MT_BARRACKS));
    for(int i=0;i<1000&&mineral_worker->harvest.phase!=HARVEST_PHASE_MINING;i++)tick();
    CHECK(mineral_worker->harvest.phase==HARVEST_PHASE_MINING);
    tick();
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_MOVE,.queued=true,.count=1,
        .units={mineral_worker->id},.position=fixed3_from_fixed2(FIXED2_LIT(3,3),0)});
    CHECK(mineral_worker->sc.queue.active==TC_HARVEST&&
          mineral_worker->sc.queue.harvest_phase==HARVEST_PHASE_MINING&&
          mineral_worker->sc.queue.count==1&&
          mineral_worker->sc.queue.pending[0].order==TC_MOVE);
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_HARVEST,.count=1,
        .units={mineral_worker->id},.position=fixed3_from_fixed2(FIXED2_LIT(15,6.5),0)});
    CHECK(mineral_worker->sc.queue.active==TC_HARVEST&&mineral_worker->sc.queue.count==0);

    G_RunTiccmd(0,&(ticcmd_t){.order=TC_HARVEST,.queued=true,.count=1,
        .units={mineral_worker->id},.position=fixed3_from_fixed2(FIXED2_LIT(15,6.5),0)});
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_MOVE,.queued=true,.count=1,
        .units={mineral_worker->id},.position=fixed3_from_fixed2(FIXED2_LIT(3,3),0)});
    CHECK(mineral_worker->sc.queue.count==2&&
          mineral_worker->sc.queue.pending[0].order==TC_HARVEST&&
          mineral_worker->sc.queue.pending[1].order==TC_MOVE);
    mineral_worker->harvest.phase=HARVEST_PHASE_NONE;
    mineral_worker->harvest.target=-1;
    P_ClearMove(mineral_worker);
    mineral_worker->movement.order_arrived=true;
    tick();
    CHECK(mineral_worker->harvest.phase!=HARVEST_PHASE_NONE&&mineral_worker->harvest.target==0&&
          mineral_worker->sc.queue.active==TC_HARVEST&&
          mineral_worker->sc.queue.count==1&&
          mineral_worker->sc.queue.pending[0].order==TC_MOVE);
    return 0;
}

static int harvest_stops_foundation(void) {
    P_FreeThinkers();P_InitThinkers();
    memset(level.cell_solid,0,(size_t)level.width*level.height);
    level.player_resources[0][0]=500;
    CHECK(spawn(MT_COMMAND_CENTER,FIXED2_LIT(4.5,4.5),0));
    mobj_t *scv=spawn(MT_SCV,FIXED2_LIT(7.5,7.5),0);
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,MT_BARRACKS);
    mobj_t *mineral=spawn(MT_MINERAL_FIELD1,FIXED2_LIT(16.5,16.5),11);
    level.resource_vents=calloc(1,sizeof(*level.resource_vents));
    level.resource_vent_count=1;
    level.resource_vents[0]=(resourcevent_t){.cell={16,16},.footprint={2,1},
        .attachment=FIXED2_LIT(16.5,16.5),.amount=24,.rate=8,.source_id=mineral->id,.active=true};
    CHECK(scv&&product&&mineral);
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_CONSTRUCT,.product=product->ui_id,
        .count=1,.units={scv->id},.position=fixed3_from_fixed2(FIXED2_LIT(18,12),0)});
    CHECK(reach_site(MT_BARRACKS,NULL));
    mobj_t *site=site_of(MT_BARRACKS);
    CHECK(site&&site->target==scv&&scv->target==site);
    int progress=site->production->time_left_ms;
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_HARVEST,.count=1,.units={scv->id},
        .position=fixed3_from_fixed2(FIXED2_LIT(16.5,16.5),0)});
    CHECK(scv->harvest.phase==HARVEST_PHASE_TO_MINE&&
          scv->sc.queue.active==TC_HARVEST&&scv->target==NULL&&site->target==NULL);
    for(int i=0;i<20;i++)tick();
    CHECK(site->production&&site->production->time_left_ms==progress);
    return 0;
}

static int harvest_group_acceptance(void) {
    P_FreeThinkers();P_InitThinkers();
    memset(level.cell_solid,0,(size_t)level.width*level.height);
    memset(level.blocked,0,(size_t)level.width*level.height);
    level.player_resources[0][0]=500;
    CHECK(spawn(MT_COMMAND_CENTER,FIXED2_LIT(4.5,4.5),0));
    mobj_t *near=spawn(MT_SCV,FIXED2_LIT(12.5,10.5),0);
    mobj_t *far=spawn(MT_SCV,FIXED2_LIT(36.5,10.5),0);
    mobj_t *mineral=spawn(MT_MINERAL_FIELD1,FIXED2_LIT(14.5,10.5),11);
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,MT_BARRACKS);
    level.resource_vents=calloc(1,sizeof(*level.resource_vents));
    level.resource_vent_count=1;
    level.resource_vents[0]=(resourcevent_t){.cell={14,10},.footprint={2,1},
        .attachment=FIXED2_LIT(14.5,10.5),.amount=24,.rate=8,.source_id=mineral->id,.active=true};
    CHECK(near&&far&&mineral&&product);
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_CONSTRUCT,.product=product->ui_id,
        .count=1,.units={near->id},.position=fixed3_from_fixed2(FIXED2_LIT(18,15),0)});
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_CONSTRUCT,.product=product->ui_id,
        .count=1,.units={far->id},.position=fixed3_from_fixed2(FIXED2_LIT(32,15),0)});
    CHECK(near->production&&far->production&&near->sc.queue.active==TC_CONSTRUCT&&
          far->sc.queue.active==TC_CONSTRUCT);
    for(int y=0;y<level.height;y++)level.cell_solid[L_Index(&level,24,y)]=1;
    P_SyncBuildingBlocking();
    ticcmd_t harvest={.order=TC_HARVEST,.count=2,.units={near->id,far->id},
        .position=fixed3_from_fixed2(FIXED2_LIT(14.5,10.5),0)};
    G_RunTiccmd(0,&harvest);
    CHECK(!near->production&&near->harvest.phase==HARVEST_PHASE_TO_MINE);
    CHECK(far->production&&far->sc.queue.active==TC_CONSTRUCT&&
          level.player_resources[0][0]==500);
    return 0;
}

static int pending_repair(void) {
    P_FreeThinkers();P_InitThinkers();
    memset(level.cell_solid,0,(size_t)level.width*level.height);
    level.player_resources[0][0]=500;
    CHECK(spawn(MT_COMMAND_CENTER,FIXED2_LIT(4.5,4.5),0));
    mobj_t *scv=spawn(MT_SCV,FIXED2_LIT(7.5,7.5),0);
    mobj_t *barracks=spawn(MT_BARRACKS,FIXED2_LIT(30.5,30.5),0);
    CHECK(scv&&barracks);
    P_DamageMobj(barracks,NULL,10);
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,MT_BARRACKS);
    CHECK(product&&G_PlaceProduct(scv,product,(ivec2_t){14,12}));
    CHECK(scv->production&&level.player_resources[0][0]==500);
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_REPAIR,.target=barracks->id,.count=1,
        .units={scv->id}});
    CHECK(!scv->production&&level.player_resources[0][0]==500&&
          scv->sc.order.kind==SC_ORDER_REPAIR&&scv->sc.order.target==barracks->id);
    return 0;
}

static int shift_before_foundation(void) {
    P_FreeThinkers();P_InitThinkers();
    memset(level.cell_solid,0,(size_t)level.width*level.height);
    level.player_resources[0][0]=500;
    CHECK(spawn(MT_COMMAND_CENTER,FIXED2_LIT(4.5,4.5),0));
    mobj_t *scv=spawn(MT_SCV,FIXED2_LIT(7.5,7.5),0);
    const StaticProductDefinition *barracks=G_ModelProductByUIId(NULL,MT_BARRACKS);
    CHECK(scv&&barracks);
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_CONSTRUCT,.product=barracks->ui_id,
        .count=1,.units={scv->id},.position=fixed3_from_fixed2(FIXED2_LIT(18,12),0)});
    CHECK(scv->production&&scv->sc.queue.active==TC_CONSTRUCT);
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_MOVE,.queued=true,.count=1,.units={scv->id},
        .position=fixed3_from_fixed2(FIXED2_LIT(2,2),0)});
    CHECK(scv->sc.queue.count==1&&scv->production);
    CHECK(reach_site(MT_BARRACKS,NULL));
    mobj_t *site=site_of(MT_BARRACKS);
    CHECK(site&&site->target==scv&&scv->sc.queue.target==site->id&&scv->sc.queue.count==1);
    for(int i=0;i<4000&&site->production;i++)tick();
    CHECK(!site->production&&scv->sc.queue.count==1&&!scv->sc.queue.active);
    tick();
    CHECK(scv->sc.queue.active==TC_MOVE&&P_HasMoveOrder(scv));

    P_FreeThinkers();P_InitThinkers();
    memset(level.cell_solid,0,(size_t)level.width*level.height);
    level.player_resources[0][0]=500;
    CHECK(spawn(MT_COMMAND_CENTER,FIXED2_LIT(4.5,20.5),0));
    mobj_t *probe=spawn(MT_PROBE,FIXED2_LIT(7.5,22.5),0);
    const StaticProductDefinition *pylon=G_ModelProductByUIId(NULL,MT_PYLON);
    CHECK(probe&&pylon);
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_CONSTRUCT,.product=pylon->ui_id,
        .count=1,.units={probe->id},.position=fixed3_from_fixed2(FIXED2_LIT(18,24),0)});
    CHECK(probe->production&&probe->sc.queue.active==TC_CONSTRUCT);
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_MOVE,.queued=true,.count=1,.units={probe->id},
        .position=fixed3_from_fixed2(FIXED2_LIT(2,2),0)});
    CHECK(probe->sc.queue.count==1&&probe->production);
    CHECK(reach_site(MT_PYLON,NULL));
    site=site_of(MT_PYLON);
    CHECK(site&&site->production&&probe->type_id==MT_PROBE&&!probe->production&&
          probe->sc.queue.count==1&&!probe->sc.queue.active);
    tick();
    CHECK(probe->sc.queue.active==TC_MOVE&&P_HasMoveOrder(probe));
    return 0;
}

static int terran_cancel(void) {
    P_FreeThinkers();P_InitThinkers();
    memset(level.cell_solid,0,(size_t)level.width*level.height);
    level.player_resources[0][0]=500;
    CHECK(spawn(MT_COMMAND_CENTER,FIXED2_LIT(4.5,4.5),0));
    mobj_t *scv=spawn(MT_SCV,FIXED2_LIT(7.5,7.5),0);
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,MT_BARRACKS);
    CHECK(scv&&product&&G_PlaceProduct(scv,product,(ivec2_t){18,12}));
    CHECK(reach_site(MT_BARRACKS,NULL));
    mobj_t *site=site_of(MT_BARRACKS);
    CHECK(site&&site->target==scv&&level.player_resources[0][0]==500-product->cost);
    P_DamageMobj(site,NULL,5);
    int damaged_hp=site->hp;
    CHECK(sc_cancel_production(site,-1));
    CHECK(level.player_resources[0][0]==500-product->cost+product->cost*3/4);
    CHECK(!scv->target&&!scv->sc.queue.active&&site->remove&&site->hp==damaged_hp);
    return 0;
}

static int killed_foundation_and_supply(void) {
    P_FreeThinkers();P_InitThinkers();
    memset(level.cell_solid,0,(size_t)level.width*level.height);
    level.player_resources[0][0]=500;
    CHECK(spawn(MT_COMMAND_CENTER,FIXED2_LIT(4.5,4.5),0));
    mobj_t *scv=spawn(MT_SCV,FIXED2_LIT(7.5,7.5),0);
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,MT_SUPPLY_DEPOT);
    int used_before,provided_before,used_after,provided_after;
    sc_supply_counts(0,&used_before,&provided_before);
    CHECK(scv&&product&&G_PlaceProduct(scv,product,(ivec2_t){14,12}));
    CHECK(reach_site(MT_SUPPLY_DEPOT,NULL));
    mobj_t *site=site_of(MT_SUPPLY_DEPOT);
    CHECK(site&&site->target==scv);
    sc_supply_counts(0,&used_after,&provided_after);
    CHECK(used_after==used_before&&provided_after==provided_before);
    P_DamageMobj(site,NULL,site->hp);
    CHECK(!scv->target&&!scv->sc.queue.active);
    return 0;
}

static int zerg(void) {
    P_FreeThinkers();P_InitThinkers();
    memset(level.cell_solid,0,(size_t)level.width*level.height);
    level.player_resources[0][0]=500;
    mobj_t *hatchery=spawn(MT_HATCHERY,FIXED2_LIT(10.5,34.5),0);
    mobj_t *drone=spawn(MT_DRONE,FIXED2_LIT(13.5,34.5),0);
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,MT_SPAWNING_POOL);
    drone->hp=drone->max_hp/2;
    int worker_hp=drone->hp;
    CHECK(hatchery&&drone&&product&&G_PlaceProduct(drone,product,(ivec2_t){16,34}));
    CHECK(level.player_resources[0][0]==500);
    CHECK(reach_site(MT_SPAWNING_POOL,drone));
    CHECK(drone->type_id==MT_SPAWNING_POOL&&drone->production&&
          drone->production->placed&&drone->hp<drone->max_hp);
    CHECK(level.player_resources[0][0]==500-product->cost);
    CHECK(!G_ModelHasActorType(NULL,0,MT_SPAWNING_POOL));
    int before=level.player_resources[0][0];
    CHECK(sc_cancel_production(drone,-1));
    CHECK(drone->type_id==MT_DRONE&&!drone->production&&
          drone->hp==worker_hp&&level.player_resources[0][0]==before+product->cost*3/4);
    return 0;
}

int main(void) {
    G_InitGame();P_InitThinkers();consoleplayer=0;
    level.width=level.height=48;
    level.blocked=calloc(48*48,1);
    level.cell_solid=calloc(48*48,1);
    level.map_path[0]='x';level.map_path[1]=0;
    static sc_mission_t mission;mission.rng=1;level.mission=&mission;
    CHECK(level.blocked&&level.cell_solid);
    RTS_RUN(terran());RTS_RUN(blocked_start());RTS_RUN(pending_interrupts());
    RTS_RUN(harvest_group_acceptance());
    RTS_RUN(harvest_stops_foundation());
    RTS_RUN(pending_repair());
    RTS_RUN(shift_before_foundation());
    RTS_RUN(terran_cancel());RTS_RUN(killed_foundation_and_supply());
    RTS_RUN(protoss());RTS_RUN(zerg());
    P_FreeLevel(&level);
    puts("PASS: StarCraft Terran, Protoss and Zerg construction foundations");
    return 0;
}
