#include "t_local.h"
#include "sc_local.h"
#define CHECK(c) RTS_CHECK(c,"StarCraft orders",#c)
static mobj_t *spawn(int type, fixed2_t at, int owner) {
    mobj_t *u=P_SpawnMobj(fixed3_from_fixed2(at,0),type);
    if(u){u->owner=u->team=owner;u->allegiance=owner?ALLEGIANCE_ENEMY:ALLEGIANCE_PLAYER;}
    return u;
}
static void tick(int n){while(n--)P_Ticker();}
static void order(mobj_t *u,ticorder_t kind,fixed2_t at,mobj_t *target) {
    ticcmd_t cmd={.order=kind,.count=1,.units={u->id},.target=target?target->id:0,
        .position=fixed3_from_fixed2(at,0)};
    if(kind==TC_PATH)cmd.path=(waypoints_t){.count=1,.mode=WP_BACKTRACK,.points={fixed2_cell(at)}};
    G_RunTiccmd(u->owner,&cmd);
}
static void reset(void) {
    P_FreeThinkers();P_InitThinkers();
    memset(level.cell_solid,0,1024);
    level.player_resources[0][0]=level.player_resources[0][1]=1000;
}
static int combat_orders(void) {
    reset();
    mobj_t *marine=spawn(MT_MARINE,FIXED2_LIT(6.5,6.5),0);
    mobj_t *enemy=spawn(MT_MARINE,FIXED2_LIT(9.5,6.5),1);
    CHECK(marine&&enemy);
    order(enemy,TC_STAND_GROUND,(fixed2_t){0},NULL);
    order(marine,TC_STAND_GROUND,(fixed2_t){0},NULL);
    order(marine,TC_MOVE,FIXED2_LIT(-2,-2),NULL);
    CHECK(marine->sc.flags&SC_HOLD); /* A rejected order preserves Hold. */
    fixed3_t start=marine->core.position;
    tick(20);
    CHECK(enemy->hp<enemy->max_hp);
    CHECK(marine->core.position.x==start.x&&marine->core.position.y==start.y);
    enemy->core.position=fixed3_from_fixed2(FIXED2_LIT(19.5,6.5),0);
    tick(30);
    CHECK(!P_HasMoveOrder(marine)&&marine->core.position.x==start.x);
    order(marine,TC_MOVE,FIXED2_LIT(6.5,20.5),NULL);
    CHECK(!(marine->sc.flags&SC_HOLD)&&marine->move_only);
    P_DamageMobj(marine,enemy,1);tick(10);
    CHECK(!marine->attack.target&&marine->movement.goal.y==FIXED_LIT(20.5));
    order(marine,TC_STOP,(fixed2_t){0},NULL);
    CHECK(!marine->move_only&&!P_HasMoveOrder(marine));
    order(marine,TC_ORDER,FIXED2_LIT(6.5,20.5),NULL);
    P_DamageMobj(marine,enemy,1);
    CHECK(marine->move_only&&!marine->attack.target);

    reset();
    marine=spawn(MT_MARINE,FIXED2_LIT(4.5,6.5),0);
    enemy=spawn(MT_MARINE,FIXED2_LIT(10.5,6.5),1);
    enemy->traits&=~MF_ATTACK;enemy->hp=1;
    uint32_t enemy_id=enemy->id;
    order(marine,TC_ATTACK,FIXED2_LIT(20.5,6.5),NULL);
    CHECK(marine->waypoints.count==1&&marine->waypoints.mode==WP_ONCE);
    bool fought=false;
    for(int i=0;i<600&&marine->waypoints.count;i++){
        tick(1);enemy=P_MobjById(enemy_id);fought|=!enemy||enemy->hp<=0;
    }
    CHECK(fought&&!marine->waypoints.count&&marine->movement.order_arrived);
    CHECK(fixed2_near(fixed3_xy(marine->core.position),FIXED2_LIT(20.5,6.5),FIXED_LIT(0.1)));

    reset();
    marine=spawn(MT_MARINE,FIXED2_LIT(4.5,6.5),0);
    order(marine,TC_PATH,FIXED2_LIT(12.5,6.5),NULL);
    bool returning=false,again=false;
    for(int i=0;i<600&&!again;i++) {
        tick(1);returning|=marine->waypoints.current==1;
        again=returning&&marine->waypoints.current==0;
    }
    CHECK(returning&&again&&marine->waypoints.count==2);
    enemy=spawn(MT_MARINE,fixed2_add(fixed3_xy(marine->core.position),FIXED2_LIT(2,0)),1);
    enemy->traits&=~MF_ATTACK;
    marine->attack.target=enemy;
    enemy->core.position=fixed3_from_fixed2(FIXED2_LIT(18.5,12.5),0);
    tick(2);CHECK(marine->attack.target==enemy&&marine->waypoints.count==2);
    /* A ground patrol lets an air target go instead of leaving its route. */
    enemy->traits|=MF_FLY;
    tick(2);CHECK(!marine->attack.target&&marine->waypoints.count==2);
    order(marine,TC_STOP,(fixed2_t){0},NULL);CHECK(!marine->waypoints.count);
    mobj_t *scout=spawn(MT_OVERLORD,FIXED2_LIT(4.5,20.5),0);
    order(scout,TC_PATH,FIXED2_LIT(5.5,20.5),NULL);
    for(int i=0;i<600&&scout->waypoints.current==0;i++)tick(1);
    CHECK(scout->waypoints.current==1); /* An unarmed flyer can patrol. */
    return 0;
}
static int repairs(void) {
    reset();
    mobj_t *scv=spawn(MT_SCV,FIXED2_LIT(5.5,5.5),0);
    mobj_t *vulture=spawn(MT_VULTURE,FIXED2_LIT(6.5,5.5),0);
    CHECK(scv&&vulture);
    vulture->hp=40;
    order(scv,TC_REPAIR,(fixed2_t){0},vulture);
    CHECK(scv->sc.order.kind==SC_ORDER_REPAIR);
    sc_repair_ticker(scv,1);
    /* 80 HP, 450 native frames: rate 41/256; first debit is 1 mineral. */
    CHECK(vulture->hp==41&&vulture->sc.wound==215);
    CHECK(level.player_resources[0][0]==999&&level.player_resources[0][1]==1000);
    for(int i=0;i<400&&scv->sc.order.kind;i++)tick(1);
    CHECK(vulture->hp==80&&!vulture->sc.wound&&!scv->sc.order.kind);
    CHECK(level.player_resources[0][0]==987);
    vulture->hp=50;level.player_resources[0][0]=0;
    order(scv,TC_REPAIR,(fixed2_t){0},vulture);tick(2);
    CHECK(vulture->hp==50&&!scv->sc.order.kind);
    level.player_resources[0][0]=1000;
    mobj_t *marine=spawn(MT_MARINE,FIXED2_LIT(6.5,7.5),0);marine->hp=10;
    mobj_t *dragoon=spawn(MT_DRAGOON,FIXED2_LIT(6.5,9.5),0);dragoon->hp=10;
    order(scv,TC_REPAIR,(fixed2_t){0},marine);CHECK(!scv->sc.order.kind);
    order(scv,TC_REPAIR,(fixed2_t){0},dragoon);CHECK(!scv->sc.order.kind);
    order(scv,TC_REPAIR,(fixed2_t){0},scv);CHECK(!scv->sc.order.kind);
    vulture->owner=vulture->team=1;vulture->allegiance=ALLEGIANCE_ENEMY;
    order(scv,TC_REPAIR,(fixed2_t){0},vulture);CHECK(!scv->sc.order.kind);
    vulture->owner=vulture->team=0;vulture->allegiance=ALLEGIANCE_PLAYER;
    order(scv,TC_REPAIR,(fixed2_t){0},vulture);
    order(scv,TC_MOVE,FIXED2_LIT(4.5,15.5),NULL);CHECK(!scv->sc.order.kind);
    int hp=vulture->hp;tick(10);CHECK(vulture->hp==hp);
    vulture->owner=1; /* Friendly Terran units can be repaired at our expense. */
    order(scv,TC_REPAIR,(fixed2_t){0},vulture);
    CHECK(scv->sc.order.kind==SC_ORDER_REPAIR);
    order(scv,TC_STOP,(fixed2_t){0},NULL);

    reset();
    scv=spawn(MT_SCV,FIXED2_LIT(3.5,5.5),0);
    mobj_t *bunker=spawn(MT_BUNKER,FIXED2_LIT(12.5,6),0);bunker->hp=320;
    P_SyncBuildingBlocking();
    order(scv,TC_ORDER,fixed3_xy(bunker->core.position),bunker);
    CHECK(scv->sc.order.kind==SC_ORDER_REPAIR);
    for(int i=0;i<900&&scv->sc.order.kind;i++)tick(1);
    CHECK(bunker->hp==bunker->max_hp&&!(scv->sc.flags&SC_LOADED));
    mobj_t *wraith=spawn(MT_WRAITH,fixed2_add(fixed3_xy(scv->core.position),FIXED2_LIT(0,1)),0);
    wraith->hp=100;
    order(scv,TC_REPAIR,(fixed2_t){0},wraith);sc_repair_ticker(scv,1);
    CHECK(wraith->hp>100&&level.player_resources[0][1]==999);
    mobj_t *second=spawn(MT_SCV,fixed2_add(fixed3_xy(wraith->core.position),FIXED2_LIT(1,0)),0);
    order(second,TC_REPAIR,(fixed2_t){0},wraith);
    int raw=wraith->hp*256-wraith->sc.wound;
    sc_repair_ticker(scv,1);sc_repair_ticker(second,1);
    CHECK(wraith->hp*256-wraith->sc.wound==raw+62); /* Two 31/256 repairs. */
    P_RemoveMobj(wraith);tick(1);CHECK(!scv->sc.order.kind&&!P_HasMoveOrder(scv));
    return 0;
}
static int cargo(void) {
    reset();
    mobj_t *scv=spawn(MT_SCV,FIXED2_LIT(12.5,8.5),0);
    mobj_t *hall=spawn(MT_COMMAND_CENTER,FIXED2_LIT(6,6.5),0);
    CHECK(scv&&hall);P_SyncBuildingBlocking();
    mobj_t *far_hall=spawn(MT_COMMAND_CENTER,FIXED2_LIT(24,20.5),0);P_SyncBuildingBlocking();
    scv->harvest.cargo=8;
    order(scv,TC_ORDER,fixed3_xy(far_hall->core.position),far_hall);
    CHECK(scv->harvest.base==far_hall);
    scv->harvest.cargo=8;scv->harvest.resource_type=1;
    order(scv,TC_STOP,(fixed2_t){0},NULL);CHECK(scv->harvest.target==-1);
    order(scv,TC_RETURN_GOODS,(fixed2_t){0},NULL);
    for(int i=0;i<500&&scv->harvest.cargo;i++)tick(1);
    CHECK(!scv->harvest.cargo&&level.player_resources[0][1]==1008);
    CHECK(scv->harvest.phase==HARVEST_PHASE_NONE);
    scv->harvest.cargo=8;P_RemoveMobj(hall);P_RemoveMobj(far_hall);
    order(scv,TC_RETURN_GOODS,(fixed2_t){0},NULL);tick(30);
    CHECK(scv->harvest.cargo==8);
    hall=spawn(MT_COMMAND_CENTER,FIXED2_LIT(6,6.5),0);P_SyncBuildingBlocking();
    for(int i=0;i<500&&scv->harvest.cargo;i++)tick(1);
    CHECK(!scv->harvest.cargo&&level.player_resources[0][1]==1016);
    return 0;
}
int main(void) {
    G_InitGame();P_InitThinkers();consoleplayer=0;
    level.width=level.height=32;level.blocked=calloc(1024,1);level.cell_solid=calloc(1024,1);
    if(combat_orders()||repairs()||cargo())return 1;
    puts("StarCraft orders: ok");return 0;
}
