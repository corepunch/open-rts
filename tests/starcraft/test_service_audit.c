#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <stdlib.h>

#define CHECK(c) RTS_CHECK(c,"StarCraft building services",#c)

static mobj_t *spawn(int type,fixed2_t at,int owner) {
    mobj_t *u=sc_spawn_actor((unsigned)type-1,
        (ivec2_t){at.x>>(FIXED_FRAC_BITS-5),at.y>>(FIXED_FRAC_BITS-5)},(uint8_t)owner);
    if(u)u->allegiance=owner==consoleplayer?ALLEGIANCE_PLAYER:ALLEGIANCE_ENEMY;
    return u;
}
static void tick(int n) { while(n--) { P_Ticker();G_ProductionTicker(RTS_TICK_MS); } }
static void reset(void) {
    P_FreeThinkers();P_InitThinkers();
    memset(level.cell_solid,0,(size_t)level.width*level.height);
    memset(level.blocked,0,(size_t)level.width*level.height);
    level.player_resources[0][0]=level.player_resources[0][1]=5000;
}
static void make_foundation(mobj_t *unit,int frames) {
    unit->production=calloc(1,sizeof(*unit->production));
    *unit->production=(production_t){.actor_id=unit->type_id,.product_class=RTS_PRODUCT_BUILDING,
        .product_type=unit->type_id,.queue_count=1,.time_ms=(frames*1000+23)/24,
        .time_left_ms=(frames*1000+23)/24,.placed=true};
    unit->sc.construction.frames=frames;
}

/* unit_can_use_shield_battery and update_unit_energy both need u_completed. */
static int shield_battery(void) {
    reset();
    mobj_t *battery=spawn(MT_SHIELD_BATTERY,FIXED2_LIT(8,8),0);
    mobj_t *probe=spawn(MT_PROBE,FIXED2_LIT(10,8),0);
    CHECK(battery&&probe);
    sc_start(battery);sc_start(probe);
    probe->sc.shields=0;
    battery->sc.energy=0;
    make_foundation(battery,1000);
    tick(100);
    CHECK(battery->sc.energy==0);
    battery->sc.energy=50*256;
    CHECK(!sc_can_recharge_shields(probe,battery));
    P_FreeMobjProduction(battery);
    CHECK(sc_can_recharge_shields(probe,battery));
    battery->sc.energy=0;
    tick(100);
    CHECK(battery->sc.energy>0);
    return 0;
}

static int visible_cells(void) {
    int n=0;
    for(int i=0;i<level.width*level.height;i++)n+=(level.sight.cells[i]&UINT32_C(0x40000000))!=0;
    return n;
}

/* A Hatchery morphing into a Lair keeps every service OpenBW leaves running
 * (supply, depot, larvae, sight) and loses the ones that need u_completed. */
static int lair_morph(void) {
    reset();
    mobj_t *hatchery=spawn(MT_HATCHERY,FIXED2_LIT(14,14),0);
    spawn(MT_SPAWNING_POOL,FIXED2_LIT(14,20),0);
    spawn(MT_QUEENS_NEST,FIXED2_LIT(22,20),0);
    CHECK(hatchery&&P_InitSight());
    P_UpdateSight();
    int sight_before=visible_cells();
    int used,have_before,have_after;
    sc_supply_counts(0,&used,&have_before);
    tick(1);
    CHECK(sc_foundation(hatchery)==false);
    ticcmd_t morph={.order=TC_BUILD,.count=1,.units={hatchery->id},.product=MT_LAIR};
    G_RunTiccmd(0,&morph);
    CHECK(hatchery->production&&hatchery->type_id==MT_HATCHERY&&!sc_foundation(hatchery));
    sc_supply_counts(0,&used,&have_after);
    CHECK(have_after==have_before);
    P_UpdateSight();
    CHECK(visible_cells()==sight_before);
    CHECK(G_ModelUnitReady(hatchery)&&(hatchery->traits&MF_RESOURCE_BASE));
    /* A Queen and a research order wait for the finished Lair. */
    const StaticProductDefinition *queen=G_ModelProductByUIId(NULL,MT_QUEEN);
    const StaticProductDefinition *burrow=G_ModelProductByUIId(NULL,SC_TECH_UI+SC_TECH_BURROWING);
    CHECK(queen&&burrow&&G_ModelProductAvailable(NULL,0,queen));
    CHECK(!G_QueueProduct(hatchery,queen)&&!G_QueueProduct(hatchery,burrow));
    CHECK(hatchery->production->queue_count==1);
    /* secondary_order_SpawningLarva has no completion test: larvae keep coming. */
    int before=0,after=0;
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next)
        before+=th->function==P_MobjThinker&&((mobj_t *)th)->type_id==MT_LARVA;
    tick(500);
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next)
        after+=th->function==P_MobjThinker&&((mobj_t *)th)->type_id==MT_LARVA;
    CHECK(after>before);
    /* Once the morph ends the same orders are accepted again. */
    for(int i=0;i<4000&&hatchery->type_id!=MT_LAIR;i++)tick(1);
    CHECK(hatchery->type_id==MT_LAIR&&!hatchery->production);
    CHECK(G_QueueProduct(hatchery,burrow));
    return 0;
}

/* unit_can_gather_gas needs a completed refinery: nothing is mined while the
 * Assimilator is a foundation, and gas flows once it finishes. */
static int incomplete_assimilator(void) {
    reset();
    free(level.resource_vents);
    level.resource_vents=calloc(1,sizeof(*level.resource_vents));
    level.resource_vent_count=1;
    level.player_resources[0][1]=0;
    spawn(MT_NEXUS,FIXED2_LIT(10,35),0);
    ivec2_t cell={20,34};
    fixed2_t at=P_BuildingPosition(MT_ASSIMILATOR,cell);
    mobj_t *geyser=spawn(MT_VESPENE_GEYSER,at,11);
    const mobjtype_t *type=P_ActorType(MT_VESPENE_GEYSER);
    CHECK(geyser&&type);
    ivec2_t geyser_cell=fixed2_cell(at);
    level.resource_vents[0]=(resourcevent_t){.cell=geyser_cell,.footprint=type->footprint,
        .attachment=fixed2_cell_center((ivec2_t){geyser_cell.x+type->footprint.w/2,
                                                  geyser_cell.y+type->footprint.h/2}),
        .resource_type=1,.amount=500,.rate=8,.source_id=geyser->id,.active=true};
    mobj_t *probe=spawn(MT_PROBE,fixed2_add(at,FIXED2_LIT(-4,0)),0);
    mobj_t *site=spawn(MT_ASSIMILATOR,at,0);
    CHECK(probe&&site);
    make_foundation(site,sc_units[MT_ASSIMILATOR-1].build_time);
    ticcmd_t harvest={.order=TC_HARVEST,.count=1,.units={probe->id},
        .position=fixed3_from_fixed2(level.resource_vents[0].attachment,0)};
    G_RunTiccmd(0,&harvest);
    tick(300);
    CHECK(site->production&&level.player_resources[0][1]==0);
    for(int i=0;i<2000&&site->production;i++)tick(1);
    CHECK(!site->production);
    G_RunTiccmd(0,&harvest);
    for(int i=0;i<3000&&level.player_resources[0][1]==0;i++)tick(1);
    CHECK(level.player_resources[0][1]>0);
    return 0;
}

int main(void) {
    G_InitGame();P_InitThinkers();consoleplayer=0;
    level.width=level.height=48;
    level.blocked=calloc(48*48,1);
    level.cell_solid=calloc(48*48,1);
    level.mission=calloc(1,sizeof(sc_mission_t));
    ((sc_mission_t *)level.mission)->rng=1;
    CHECK(level.blocked&&level.cell_solid&&level.mission);
    RTS_RUN(shield_battery());RTS_RUN(lair_morph());RTS_RUN(incomplete_assimilator());
    puts("PASS: StarCraft unfinished and morphing buildings offer only OpenBW's services");
    return 0;
}
