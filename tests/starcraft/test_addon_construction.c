#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <stdlib.h>

#define CHECK(c) RTS_CHECK(c,"StarCraft add-on construction",#c)

/* OpenBW order_PlaceAddon creates the add-on at once as an incomplete
 * building; secondary_order_BuildAddon steps it with resume_building_unit
 * and connects it at completion. Machine Shop: 750 HP, 600 frames, 50/50.
 * In 1/256 HP it starts at 19200 and gains (192000-19200+600-1)/600 = 288 a
 * frame. cancel_building_unit gives back 50*3/4 = 37 of each. */

static mobj_t *spawn(int type,fixed2_t at,int owner) {
    mobj_t *unit=P_SpawnMobj(fixed3_from_fixed2(at,0),(uint16_t)type);
    if(unit) {
        unit->owner=unit->team=(uint8_t)owner;
        unit->allegiance=owner==consoleplayer?ALLEGIANCE_PLAYER:ALLEGIANCE_ENEMY;
        sc_start(unit);
    }
    return unit;
}
static mobj_t *building(int type,ivec2_t cell,int owner) {
    mobj_t *mo=spawn(type,P_BuildingPosition((uint16_t)type,cell),owner);
    P_SyncBuildingBlocking();
    return mo;
}
static void tick(void) { P_Ticker(); G_ProductionTicker(RTS_TICK_MS); }
static int64_t raw(const mobj_t *unit) { return (int64_t)unit->hp*256-unit->sc.wound; }
static const StaticProductDefinition *product(int ui) { return G_ModelProductByUIId(NULL,ui); }
static void reset(void) {
    P_FreeThinkers();P_InitThinkers();
    memset(level.cell_solid,0,(size_t)level.width*level.height);
    memset(level.blocked,0,(size_t)level.width*level.height);
    sc_creep_reset(level.mission);
    level.player_resources[0][0]=level.player_resources[0][1]=1000;
}
static int live_count(uint16_t type) {
    int n=0;
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        const mobj_t *mo=(const mobj_t *)th;
        if(th->function==P_MobjThinker&&!mo->remove&&mo->hp>0&&mo->type_id==type)n++;
    }
    return n;
}
/* A Factory that has just started a Machine Shop. */
static mobj_t *started(mobj_t **factory_out) {
    reset();
    building(MT_SUPPLY_DEPOT,(ivec2_t){30,30},0);
    mobj_t *factory=building(MT_FACTORY,(ivec2_t){4,4},0);
    if(!factory||!G_QueueProduct(factory,product(MT_MACHINE_SHOP)))return NULL;
    *factory_out=factory;
    tick();
    return sc_addon_of(factory);
}

static int site_from_start(void) {
    mobj_t *factory;
    mobj_t *shop=started(&factory);
    CHECK(shop&&shop->type_id==MT_MACHINE_SHOP&&sc_foundation(shop));
    /* Paid once, linked both ways, the building's queue empty. */
    CHECK(level.player_resources[0][0]==950&&level.player_resources[0][1]==950);
    CHECK(shop->sc.parent==factory->id&&factory->sc.addon==shop->id&&!factory->production);
    CHECK(sc_addon_building(factory)==shop&&!sc_addon_ready(factory)&&shop->owner==0);
    /* Placed right of the Factory, its cells solid: it is a real building. */
    irect_t f=P_MobjCells(factory),s=P_MobjCells(shop);
    CHECK(s.x==f.x+f.w&&s.y+s.h==f.y+f.h);
    CHECK(level.cell_solid[L_Index(&level,s.x,s.y)]&2);
    int done=600-shop->sc.construction.frames;
    CHECK(done>=0&&done<=1&&raw(shop)==19200+288*done);
    /* unit_is_constructing: no training, no second add-on, no tank. */
    CHECK(!G_ModelProducerHasTech(factory,product(MT_VULTURE)));
    CHECK(!G_ModelProducerHasTech(factory,product(MT_MACHINE_SHOP)));
    CHECK(!G_ModelProducerHasTech(factory,product(MT_SIEGE_TANK)));
    /* An incomplete Machine Shop is no Machine Shop for the tech tree. */
    char why[96];
    CHECK(sc_product_lock(0,factory,product(MT_SIEGE_TANK),why,sizeof(why))==SC_LOCK_GREYED);
    /* The HP ramps a step a frame. */
    for(int i=0;i<400&&shop->sc.construction.frames>500;i++)tick();
    CHECK(shop->sc.construction.frames==500&&raw(shop)==19200+288*100);
    /* Damage is never healed back. */
    P_DamageMobj(shop,NULL,40);
    CHECK(shop->hp>0&&raw(shop)==19200+288*100-40*256);
    for(int i=0;i<400&&shop->sc.construction.frames>400;i++)tick();
    CHECK(raw(shop)==19200+288*200-40*256);
    /* resume_building_unit adds one more step on the completing frame. */
    for(int i=0;i<2000&&shop->production;i++)tick();
    CHECK(!shop->production&&!sc_foundation(shop));
    CHECK(raw(shop)==19200+288*601-40*256);
    CHECK(sc_addon_ready(factory)==shop&&shop->sc.parent==factory->id);
    CHECK(G_ModelProducerHasTech(factory,product(MT_VULTURE)));
    CHECK(sc_product_lock(0,factory,product(MT_SIEGE_TANK),why,sizeof(why))==SC_LOCK_OPEN);
    return 0;
}

static int cancel_refunds(void) {
    /* action_cancel_addon on the building. */
    mobj_t *factory;
    mobj_t *shop=started(&factory);
    CHECK(shop);
    CHECK(sc_cancel_production(factory,-1));
    CHECK(shop->remove&&!factory->sc.addon&&!live_count(MT_MACHINE_SHOP));
    CHECK(level.player_resources[0][0]==987&&level.player_resources[0][1]==987);
    CHECK(G_ModelProducerHasTech(factory,product(MT_MACHINE_SHOP)));
    /* action_cancel_building_unit on the add-on itself (the status panel). */
    shop=started(&factory);
    CHECK(shop);
    ticcmd_t cancel={.order=TC_CANCEL_PRODUCTION,.product=0,.count=1,.units={shop->id}};
    G_RunTiccmd(0,&cancel);
    CHECK(shop->remove&&!factory->sc.addon);
    CHECK(level.player_resources[0][0]==987&&level.player_resources[0][1]==987);
    /* Through the building's command, as the Cancel Addon button sends it. */
    shop=started(&factory);
    CHECK(shop);
    cancel.units[0]=factory->id;cancel.product=-1;
    G_RunTiccmd(0,&cancel);
    CHECK(shop->remove&&level.player_resources[0][0]==987);
    return 0;
}

static int parent_lifts_or_dies(void) {
    /* secondary_order_BuildAddon cancels the add-on once its building flies. */
    mobj_t *factory;
    mobj_t *shop=started(&factory);
    CHECK(shop);
    P_MobjSetSelected(factory,true);
    ticcmd_t lift={.order=TC_LIFT,.count=1,.units={factory->id}};
    G_RunTiccmd(0,&lift);
    CHECK(shop->remove&&!factory->sc.addon&&(factory->traits&MF_FLY));
    CHECK(level.player_resources[0][0]==987&&level.player_resources[0][1]==987);
    /* The kill path cancels it as well: three quarters back. */
    shop=started(&factory);
    CHECK(shop);
    P_DamageMobj(factory,NULL,factory->hp+10);
    CHECK(shop->remove&&level.player_resources[0][0]==987);
    /* A finished add-on is abandoned instead: neutral, unlinked. */
    shop=started(&factory);
    CHECK(shop);
    for(int i=0;i<2000&&shop->production;i++)tick();
    CHECK(sc_addon_ready(factory)==shop);
    P_DamageMobj(factory,NULL,factory->hp+10);
    CHECK(!shop->remove&&shop->owner==11&&!shop->sc.parent);
    return 0;
}

static int addon_destroyed(void) {
    mobj_t *factory;
    mobj_t *shop=started(&factory);
    CHECK(shop);
    P_DamageMobj(shop,NULL,shop->hp+10);
    tick();
    /* No refund for a destroyed site; the building is free again. */
    CHECK(level.player_resources[0][0]==950&&!sc_addon_of(factory)&&!factory->sc.addon);
    CHECK(G_ModelProducerHasTech(factory,product(MT_VULTURE)));
    CHECK(G_ModelProducerHasTech(factory,product(MT_MACHINE_SHOP)));
    return 0;
}

static int blocked_place_refunds(void) {
    reset();
    mobj_t *factory=building(MT_FACTORY,(ivec2_t){4,4},0);
    CHECK(factory&&G_QueueProduct(factory,product(MT_MACHINE_SHOP)));
    CHECK(level.player_resources[0][0]==950);
    /* Something lands in the add-on's place before it starts. */
    ivec2_t cell;
    CHECK(sc_addon_site(factory,MT_MACHINE_SHOP,&cell));
    CHECK(building(MT_SUPPLY_DEPOT,cell,0));
    tick();
    CHECK(!live_count(MT_MACHINE_SHOP)&&!factory->production&&!factory->sc.addon);
    CHECK(level.player_resources[0][0]==1000&&level.player_resources[0][1]==1000);
    return 0;
}

static int infested_parent_lets_go(void) {
    /* order_InfestedCommandCenter: set_secondary_order clears
     * current_build_unit, so the Comsat stays its owner's, unfinished. */
    reset();
    level.player_resources[1][0]=level.player_resources[1][1]=1000;
    mobj_t *cc=building(MT_COMMAND_CENTER,(ivec2_t){12,4},1);
    building(MT_ACADEMY,(ivec2_t){24,20},1);
    mobj_t *queen=spawn(MT_QUEEN,FIXED2_LIT(4.5,12.5),0);
    CHECK(cc&&queen&&spawn(MT_OVERLORD,FIXED2_LIT(13.5,10.5),0));
    CHECK(G_QueueProduct(cc,product(MT_COMSAT_STATION)));
    tick();
    mobj_t *comsat=sc_addon_building(cc);
    CHECK(comsat&&comsat->owner==1);
    P_UpdateSight();
    cc->hp=cc->max_hp/2-1;
    ticcmd_t cast={.order=TC_SPELL,.count=1,.units={queen->id},.product=SC_TECH_INFESTATION,
        .target=cc->id,.position=cc->core.position};
    G_RunTiccmd(0,&cast);
    for(int i=0;i<RTS_TICRATE*10;i++)tick();
    CHECK(cc->type_id==MT_INFESTED_COMMAND_CENTER&&cc->owner==0&&!cc->sc.addon);
    int frames=comsat->sc.construction.frames;
    CHECK(!comsat->remove&&comsat->owner==1&&!comsat->sc.parent&&sc_foundation(comsat)&&frames>0);
    for(int i=0;i<10;i++)tick();
    CHECK(comsat->sc.construction.frames==frames);
    /* Its owner can still cancel it for three quarters. */
    int before=level.player_resources[1][0];
    CHECK(sc_cancel_production(comsat,0)&&comsat->remove&&level.player_resources[1][0]==before+37);
    return 0;
}

static int save_round_trip(void) {
    /* Saves need a real map: the first Terran mission. */
    P_FreeThinkers();P_InitThinkers();
    P_FreeLevel(&level);
    sc_set_map_mode(SC_MAP_CAMPAIGN);
    CHECK(G_DoLoadLevel("data/STARCRAFT/install/campaign/terran/terran01/staredit/scenario.chk",&level));
    CHECK(G_LoadLevelData(g_game_default_root,&level));
    CHECK(P_LoadThings(level.map_path)>0&&P_InitSight());
    level.player_resources[0][0]=level.player_resources[0][1]=1000;
    mobj_t *factory=NULL;
    for(int y=2;y<level.height-6&&!factory;y++)for(int x=2;x<level.width-8&&!factory;x++) {
        ivec2_t cell={x,y};
        if(!P_CanPlaceBuilding(MT_FACTORY,cell,NULL))continue;
        mobj_t *f=building(MT_FACTORY,cell,0);
        ivec2_t shop_cell;
        if(f&&sc_addon_site(f,MT_MACHINE_SHOP,&shop_cell))factory=f;
        else if(f) { P_RemoveMobj(f); P_SyncBuildingBlocking(); }
    }
    CHECK(factory&&G_QueueProduct(factory,product(MT_MACHINE_SHOP)));
    tick();
    mobj_t *shop=sc_addon_building(factory);
    CHECK(shop);
    for(int i=0;i<30;i++)tick();
    uint32_t shop_id=shop->id,factory_id=factory->id;
    int frames=shop->sc.construction.frames;
    int64_t hp=raw(shop);
    uint32_t hash=G_Consistency();
    /* The link and the build frames are hashed. */
    shop->sc.construction.frames--;
    CHECK(G_Consistency()!=hash);
    shop->sc.construction.frames++;
    factory->sc.addon=0;
    CHECK(G_Consistency()!=hash);
    factory->sc.addon=shop_id;
    app_t app={0};AiContext ai={0};hudtext_t hud={0};
    void *saved=NULL;size_t length=0;
    CHECK(G_SaveGameBlob(&saved,&length,"addon",&app,&ai,&hud));
    tick();
    CHECK(G_LoadGameBlob(saved,length,&app,&ai,&hud));
    free(saved);
    CHECK(G_Consistency()==hash);
    shop=P_MobjById(shop_id);factory=P_MobjById(factory_id);
    CHECK(shop&&factory&&sc_addon_building(factory)==shop&&shop->sc.construction.frames==frames&&raw(shop)==hp);
    tick();
    CHECK(shop->sc.construction.frames<frames&&raw(shop)>hp);
    return 0;
}

int main(void) {
    G_InitGame();P_InitThinkers();consoleplayer=0;
    level.width=level.height=48;
    level.blocked=calloc(48*48,1);
    level.cell_solid=calloc(48*48,1);
    level.map_path[0]='x';level.map_path[1]=0;
    static sc_mission_t mission;mission.rng=1;level.mission=&mission;
    CHECK(level.blocked&&level.cell_solid&&P_InitSight());
    RTS_RUN(site_from_start());RTS_RUN(cancel_refunds());RTS_RUN(parent_lifts_or_dies());
    RTS_RUN(addon_destroyed());RTS_RUN(blocked_place_refunds());RTS_RUN(infested_parent_lets_go());
    RTS_RUN(save_round_trip());
    P_FreeLevel(&level);
    puts("PASS: StarCraft add-ons are incomplete sites from the start, step, cancel and die as OpenBW's");
    return 0;
}
