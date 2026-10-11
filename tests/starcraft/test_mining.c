#include "t_local.h"
#include "starcraft.h"
#include "info.h"
#include "sc_local.h"
#define CHECK(c) RTS_CHECK(c,"StarCraft mining",#c)
static mobj_t *spawn(int native,fixed2_t at,int owner) {
    mobj_t *u=P_SpawnMobj(fixed3_from_fixed2(at,0),native+1);
    if(u){u->owner=u->team=owner;u->allegiance=owner==consoleplayer?ALLEGIANCE_PLAYER:ALLEGIANCE_NEUTRAL;}
    return u;
}
static void tick(int n){while(n--){P_Ticker();G_ProductionTicker(RTS_TICK_MS);}}
static void order(mobj_t *u,ticorder_t type,fixed2_t at,mobj_t *target) {
    ticcmd_t cmd={.order=type,.count=1,.units={u->id},.target=target?target->id:0,
        .position=fixed3_from_fixed2(at,0)};G_RunTiccmd(consoleplayer,&cmd);
}
int main(void) {
    G_InitGame();P_InitThinkers();consoleplayer=0;
    level.width=level.height=32;level.blocked=calloc(1024,1);level.cell_solid=calloc(1024,1);
    /* No iscript here: a one-tic swing stands in for the compiled one. */
    int swing=SC_SCRIPT_STATES;
    states[swing]=(state_t){.sprite=MT_SCV-1,.tics=1,.group=6,.nextstate=1+(MT_SCV-1)*2};
    sc_set_harvest_state(MT_SCV,swing);
    spawn(106,FIXED2_LIT(6,6.5f),0);
    mobj_t *a=spawn(7,FIXED2_LIT(10.5,6.5),0),*b=spawn(7,FIXED2_LIT(9.5,6.5),0);
    mobj_t *mineral=spawn(176,FIXED2_LIT(15,6.5),11);CHECK(a&&b&&mineral);
    level.resource_vents=calloc(1,sizeof(*level.resource_vents));level.resource_vent_count=1;
    level.resource_vents[0]=(resourcevent_t){.cell={14,6},.footprint={2,1},.attachment=FIXED2_LIT(15,6.5),
        .amount=4000,.rate=8,.source_id=mineral->id,.active=true,.exhausts_source=true};
    P_SyncBuildingBlocking();
    order(a,TC_ORDER,FIXED2_LIT(15,6.5),NULL);order(b,TC_ORDER,FIXED2_LIT(15,6.5),NULL);
    CHECK(!fixed2_near(a->movement.goal,b->movement.goal,FIXED_LIT(0.001)));
    bool both=false,swung=false;
    for(int i=0;i<600&&!both;i++){tick(1);both=sc_mining(a)&&sc_mining(b);}
    CHECK(both);
    CHECK(!fixed2_near(fixed3_xy(a->core.position),fixed3_xy(b->core.position),FIXED_LIT(0.001)));
    /* Miners share the field: neither is pushed off nor planned around. */
    uint8_t *soft=P_IdleBlockers(&level,NULL,0);CHECK(!soft);free(soft);
    bool held=true;
    for(int i=0;i<60;i++){
        fixed3_t pa=a->core.position,pb=b->core.position;
        bool both_mining=sc_mining(a)&&sc_mining(b);
        tick(1);
        swung|=states[a->core.state_id].group==6;
        if(both_mining&&sc_mining(a)&&sc_mining(b))
            held&=a->core.position.x==pa.x&&a->core.position.y==pa.y&&
                  b->core.position.x==pb.x&&b->core.position.y==pb.y;
    }
    CHECK(swung&&held);
    bool left=false,returned=false;
    for(int i=0;i<1200&&!returned;i++){
        tick(1);
        if(!sc_mining(a)) left=true;
        if(left&&sc_mining(a)) returned=true;
    }
    CHECK(returned);
    a->harvest.cargo=8;
    order(a,TC_RETURN_GOODS,(fixed2_t){0},NULL);
    CHECK(a->harvest.target==0&&a->harvest.phase==HARVEST_PHASE_TO_BASE);
    for(int i=0;i<600&&a->harvest.cargo;i++)tick(1);
    CHECK(!a->harvest.cargo&&a->harvest.target==0&&a->harvest.phase==HARVEST_PHASE_TO_MINE);
    for(int i=0;i<600&&!sc_mining(a);i++)tick(1);
    CHECK(sc_mining(a));
    CHECK(!fixed2_near(fixed3_xy(a->core.position),level.resource_vents[0].attachment,FIXED_LIT(0.001)));
    fixed2_t at=fixed3_xy(a->core.position);
    mobj_t *walker=spawn(0,(fixed2_t){at.x-FIXED_ONE,at.y},0);CHECK(walker);
    fixed2_t ahead={FIXED_ONE,0},steered=P_SteerAvoid(walker,ahead,FIXED_LIT(0.1));
    CHECK(steered.x==ahead.x&&steered.y==ahead.y);
    printf("StarCraft mining: ok\n");
    return 0;
}
