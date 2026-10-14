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
static int timing(void) {
    const int workers[]={MT_SCV,MT_DRONE,MT_PROBE};
    const int depots[]={MT_COMMAND_CENTER,MT_HATCHERY,MT_NEXUS};
    for(int r=0;r<2;r++)for(int w=0;w<3;w++) {
        P_FreeThinkers();P_InitThinkers();memset(level.cell_solid,0,1024);
        free(level.resource_vents);level.resource_vents=calloc(1,sizeof(*level.resource_vents));
        level.resource_vent_count=1;
        mobj_t *base=spawn(depots[w]-1,FIXED2_LIT(6,6.5),0);
        mobj_t *worker=spawn(workers[w]-1,FIXED2_LIT(10.5,6.5),0);
        mobj_t *mineral=spawn(MT_MINERAL_FIELD1-1,FIXED2_LIT(15,6.5),11);
        CHECK(base&&worker&&mineral);
        level.resource_vents[0]=(resourcevent_t){.cell={14,6},.footprint={2,1},.attachment=FIXED2_LIT(15,6.5),
            .resource_type=r,.amount=4000,.rate=8,.source_id=mineral->id,.active=true};
        P_SyncBuildingBlocking();
        order(worker,TC_ORDER,FIXED2_LIT(15,6.5),NULL);
        for(int cycle=0;cycle<2;cycle++) {
            for(int i=0;i<1200&&worker->harvest.phase!=HARVEST_PHASE_MINING;i++)tick(1);
            CHECK(worker->harvest.phase==HARVEST_PHASE_MINING&&worker->harvest.timer_ms==0);
            /* Retail: 75 frames at minerals, 37 in a refinery, 24 a second. */
            int duration=r?1542:3125;
            int tics=(duration+RTS_TICK_MS-1)/RTS_TICK_MS;
            tick(tics-1);CHECK(worker->harvest.cargo==0);
            tick(1);CHECK(worker->harvest.cargo==8&&worker->harvest.phase==HARVEST_PHASE_TO_BASE);
            int resources=level.player_resources[0][r];
            /* The cargo drops the tick the worker reaches the depot. */
            for(int i=0;i<1200&&worker->harvest.cargo&&!worker->movement.order_arrived;i++)tick(1);
            if(worker->harvest.cargo)tick(1);
            CHECK(!worker->harvest.cargo&&level.player_resources[0][r]==resources+8);
        }
    }
    return 0;
}
/* An exhausted geyser keeps giving 2 a trip; a busy field sends the next
 * worker to a free one beside it. */
static int depletion_and_hops(void) {
    P_FreeThinkers();P_InitThinkers();memset(level.cell_solid,0,1024);
    free(level.resource_vents);level.resource_vents=calloc(2,sizeof(*level.resource_vents));
    level.resource_vent_count=1;
    mobj_t *base=spawn(MT_COMMAND_CENTER-1,FIXED2_LIT(6,6.5),0);
    mobj_t *worker=spawn(MT_SCV-1,FIXED2_LIT(10.5,6.5),0);
    mobj_t *geyser=spawn(MT_MINERAL_FIELD1-1,FIXED2_LIT(15,6.5),11);
    CHECK(base&&worker&&geyser);
    level.resource_vents[0]=(resourcevent_t){.cell={14,6},.footprint={2,1},.attachment=FIXED2_LIT(15,6.5),
        .resource_type=1,.amount=10,.rate=8,.source_id=geyser->id,.active=true,.depleted_rate=2};
    P_SyncBuildingBlocking();
    order(worker,TC_ORDER,FIXED2_LIT(15,6.5),NULL);
    int gas=level.player_resources[0][1];
    for(int i=0;i<3000&&level.player_resources[0][1]<gas+14;i++)tick(1);
    CHECK(level.player_resources[0][1]==gas+14); /* 8, then the last 2 */
    CHECK(level.resource_vents[0].depleted&&level.resource_vents[0].active);
    for(int i=0;i<3000&&level.player_resources[0][1]<gas+16;i++)tick(1);
    CHECK(level.player_resources[0][1]==gas+16&&worker->harvest.target==0); /* 2 a trip from now on */

    P_FreeThinkers();P_InitThinkers();memset(level.cell_solid,0,1024);
    level.resource_vent_count=2;
    base=spawn(MT_COMMAND_CENTER-1,FIXED2_LIT(6,6.5),0);
    mobj_t *a=spawn(MT_SCV-1,FIXED2_LIT(10.5,6.5),0),*b=spawn(MT_SCV-1,FIXED2_LIT(10.5,7.5),0);
    mobj_t *one=spawn(MT_MINERAL_FIELD1-1,FIXED2_LIT(15,6.5),11),*two=spawn(MT_MINERAL_FIELD1-1,FIXED2_LIT(15,8.5),11);
    CHECK(base&&a&&b&&one&&two);
    level.resource_vents[0]=(resourcevent_t){.cell={14,6},.footprint={2,1},.attachment=FIXED2_LIT(15,6.5),
        .amount=1500,.rate=8,.source_id=one->id,.active=true,.exhausts_source=true};
    level.resource_vents[1]=(resourcevent_t){.cell={14,8},.footprint={2,1},.attachment=FIXED2_LIT(15,8.5),
        .amount=1500,.rate=8,.source_id=two->id,.active=true,.exhausts_source=true};
    P_SyncBuildingBlocking();
    order(a,TC_ORDER,FIXED2_LIT(15,6.5),NULL);order(b,TC_ORDER,FIXED2_LIT(15,6.5),NULL);
    bool both=false;
    for(int i=0;i<900&&!both;i++){tick(1);both=sc_mining(a)&&sc_mining(b);}
    CHECK(both&&a->harvest.target!=b->harvest.target);
    return 0;
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
    /* Retail: one worker works a field; the other waits beside it. */
    int first_mining=0,overlap=0;bool swung=false;
    for(int i=0;i<600&&!first_mining;i++){tick(1);first_mining=sc_mining(a)?1:sc_mining(b)?2:0;}
    CHECK(first_mining);
    mobj_t *miner=first_mining==1?a:b,*waiter=first_mining==1?b:a;
    bool waiter_mined=false;
    for(int i=0;i<1200&&!waiter_mined;i++){
        tick(1);
        swung|=states[miner->core.state_id].group==6;
        overlap+=sc_mining(a)&&sc_mining(b);
        waiter_mined=sc_mining(waiter);
    }
    CHECK(swung&&waiter_mined&&overlap==0);
    CHECK(!fixed2_near(fixed3_xy(a->core.position),fixed3_xy(b->core.position),FIXED_LIT(0.001)));
    /* The miner at work is not pushed off nor planned around. */
    uint8_t *soft=P_IdleBlockers(&level,NULL,0);CHECK(!soft);free(soft);
    bool held=true;
    for(int i=0;i<30&&sc_mining(waiter);i++){
        fixed3_t p=waiter->core.position;
        tick(1);
        if(sc_mining(waiter))held&=waiter->core.position.x==p.x&&waiter->core.position.y==p.y;
    }
    CHECK(held);
    bool returned=false,left=false;
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
    for(int i=0;i<1200&&!sc_mining(a);i++)tick(1);
    CHECK(sc_mining(a));
    CHECK(!fixed2_near(fixed3_xy(a->core.position),level.resource_vents[0].attachment,FIXED_LIT(0.001)));
    fixed2_t at=fixed3_xy(a->core.position);
    mobj_t *walker=spawn(0,(fixed2_t){at.x-FIXED_ONE,at.y},0);CHECK(walker);
    fixed2_t ahead={FIXED_ONE,0},steered=P_SteerAvoid(walker,ahead,FIXED_LIT(0.1));
    CHECK(steered.x==ahead.x&&steered.y==ahead.y);
    CHECK(timing()==0);
    CHECK(depletion_and_hops()==0);
    printf("StarCraft mining: ok\n");
    return 0;
}
