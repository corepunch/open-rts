#include "t_local.h"
#include "starcraft.h"
#include "info.h"
#define CHECK(c) RTS_CHECK(c,"StarCraft shared gameplay",#c)
static mobj_t *spawn(int native,fvec2_t at,int owner) {
    mobj_t *u=P_SpawnMobj(fixed3_from_fvec2(at,0),native+1);
    if(u){u->owner=u->team=owner;u->allegiance=owner==consoleplayer?ALLEGIANCE_PLAYER:owner>=8?ALLEGIANCE_NEUTRAL:ALLEGIANCE_ENEMY;}
    return u;
}
static void tick(int n){while(n--){P_Ticker();G_ProductionTicker(1.0f/RTS_TICRATE);}}
static void order(mobj_t *u,ticorder_t type,fvec2_t at,mobj_t *target,int product) {
    ticcmd_t cmd={.order=type,.count=1,.units={u->id},.target=target?target->id:0,
        .position=fixed3_from_fvec2(at,0),.product=product};G_RunTiccmd(consoleplayer,&cmd);
}
int main(void) {
    G_InitGame();P_InitThinkers();consoleplayer=0;
    level.width=level.height=32;level.blocked=calloc(1024,1);level.cell_solid=calloc(1024,1);
    mobj_t *hall=spawn(106,(fvec2_t){6,6.5f},0),*worker=spawn(7,(fvec2_t){10.5f,6.5f},0);
    mobj_t *mineral=spawn(176,(fvec2_t){15,6.5f},11);CHECK(hall&&worker&&mineral);
    level.resource_vents=calloc(1,sizeof(*level.resource_vents));level.resource_vent_count=1;
    level.resource_vents[0]=(resourcevent_t){.cell={14,6},.footprint={2,1},.attachment={15,6.5f},
        .amount=24,.rate=8,.source_id=mineral->id,.active=true,.exhausts_source=true};
    P_SyncBuildingBlocking();
    order(worker,TC_ORDER,(fvec2_t){15,6.5f},NULL,0);
    CHECK(worker->harvest.phase==HARVEST_PHASE_TO_MINE);
    tick(1500);
    CHECK(level.player_resources[0][0]==24&&worker->harvest.cargo==0);
    CHECK(!level.resource_vents[0].active&&!level.cell_solid[L_Index(&level,14,6)]);
    level.player_resources[0][0]=500;
    order(worker,TC_CONSTRUCT,(fvec2_t){20,10},NULL,110);
    CHECK(worker->production&&worker->production->placed&&level.player_resources[0][0]==400);
    CHECK(!G_ModelHasActorType(NULL,0,110));
    tick(1600);CHECK(G_ModelHasActorType(NULL,0,110)&&!worker->production);
    CHECK(level.cell_solid[L_Index(&level,20,10)]);
    order(hall,TC_BUILD,(fvec2_t){0},NULL,8);CHECK(hall->production&&level.player_resources[0][0]==350);
    tick(500);CHECK(G_CountPlannedActors(0,8)==2&&!hall->production);
    mobj_t *geyser=spawn(188,(fvec2_t){24,20},11);CHECK(geyser);
    level.resource_vents=realloc(level.resource_vents,2*sizeof(*level.resource_vents));
    level.resource_vents[1]=(resourcevent_t){.cell={22,19},.footprint={4,2},.attachment={24,20},
        .amount=24,.rate=8,.resource_type=1,.source_id=geyser->id};
    level.resource_vent_count=2;P_SyncBuildingBlocking();
    CHECK(!P_CanPlaceBuilding(111,(ivec2_t){20,20},worker));
    CHECK(P_CanPlaceBuilding(111,(ivec2_t){22,19},worker));
    order(worker,TC_CONSTRUCT,(fvec2_t){22,19},NULL,111);
    CHECK(worker->production);tick(1600);
    CHECK(G_ModelHasActorType(NULL,0,111)&&level.resource_vents[1].active);
    order(worker,TC_ORDER,(fvec2_t){24,20},NULL,0);tick(2000);
    CHECK(level.player_resources[0][1]==24&&level.resource_vents[1].amount==0);
    mobj_t *marine=spawn(0,(fvec2_t){10.5f,20.5f},0),*enemy=spawn(0,(fvec2_t){13.5f,20.5f},1);
    CHECK(marine&&enemy);int hp=enemy->hp;
    order(marine,TC_ATTACK,(fvec2_t){13.5f,20.5f},enemy,0);CHECK(marine->attack.target==enemy);
    tick(5);CHECK(enemy->hp<hp);
    order(marine,TC_MOVE,(fvec2_t){8.5f,22.5f},NULL,0);tick(60);
    CHECK(marine->core.position.x<10*FIXED_ONE);
    mobj_t *barracks=spawn(111,(fvec2_t){20,25.5f},0);
    CHECK(barracks&&spawn(112,(fvec2_t){25.5f,25},0));
    const StaticProductDefinition *firebat=G_ModelProductByUIId(NULL,33);
    CHECK(firebat&&firebat->cost==50&&firebat->extra_costs[0]==25);
    level.player_resources[0][0]=100;
    CHECK(!G_QueueProduct(barracks,firebat)&&level.player_resources[0][0]==100);
    level.player_resources[0][1]=25;
    CHECK(G_QueueProduct(barracks,firebat));
    CHECK(level.player_resources[0][0]==50&&level.player_resources[0][1]==0);
    CHECK(P_InitSight());P_UpdateSight();
    CHECK(P_SightBrightness(&level,(ivec2_t){31,0})==0);
    CHECK(P_SightBrightness(&level,(ivec2_t){6,6})==16);
    memset(level.sight.cells,0,1024*sizeof(*level.sight.cells));
    P_RevealSight((ivec2_t){16,16},11,0x40000000,false);
    CHECK(P_SightBrightness(&level,(ivec2_t){27,16})==16);
    CHECK(P_SightBrightness(&level,(ivec2_t){28,16})==0);
    P_UpdateSight();
    V_AllocScreen(32,32);uint32_t palette[256];for(int i=0;i<256;i++)palette[i]=0xff000000u|i*0x10101u;I_SetPalette(palette);
    V_FillRect((irect_t){0,0,32,32},200);
    level.sight.cells[0]=SIGHT_EXPLORED;
    R_DrawMinimapFog(&level,(irect_t){0,0,32,32});
    CHECK(screens[0].pixels[31]==0&&screens[0].pixels[0]>0&&screens[0].pixels[0]<200);
    CHECK(screens[0].pixels[6*32+6]==200);
    V_FreeScreen();P_FreeLevel(&level);
    CHECK(G_DoLoadLevel("data/STARCRAFT/install/campaign/terran/terran01/staredit/scenario.chk",&level));
    CHECK(level.player_resources[1][0]==40);CHECK(P_LoadThings(level.map_path)==46&&level.resource_vent_count>0);
    P_FreeLevel(&level);
    puts("PASS: shared move/attack, mineral hauling, worker construction, unit training, fog and CHK economy");return 0;
}
