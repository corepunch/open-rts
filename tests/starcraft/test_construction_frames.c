#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <stdlib.h>

#define CHECK(c) RTS_CHECK(c,"StarCraft construction frames",#c)

/* Expected values follow OpenBW initialize_unit_type and the per-frame
 * updates in resume_building_unit, order_IncompleteWarping and
 * order_IncompleteMorphing, worked out by hand in 1/256 HP:
 *   Barracks 1000 HP, 1200 frames: start 25600, +192 per frame.
 *   Pylon 300 HP/300 shields, 450 frames: start 7680, HP +154, shields +153.
 *   Spawning Pool 750 HP, 1200 frames: start 19200, +144 per frame. */

static mobj_t *spawn(int type,fixed2_t at,int owner) {
    mobj_t *unit=P_SpawnMobj(fixed3_from_fixed2(at,0),(uint16_t)type);
    if(unit)unit->owner=unit->team=(uint8_t)owner;
    return unit;
}
static void tick(void) { P_Ticker(); G_ProductionTicker(RTS_TICK_MS); }
static int64_t raw(const mobj_t *unit) { return (int64_t)unit->hp*256-unit->sc.wound; }

static mobj_t *site_of(uint16_t type,const mobj_t *morphing) {
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        mobj_t *unit=(mobj_t *)th;
        if(th->function==P_MobjThinker&&!unit->remove&&unit->type_id==type&&
           unit->production&&unit->production->placed&&(!morphing||unit==morphing))return unit;
    }
    return NULL;
}
static mobj_t *reach_site(uint16_t type,mobj_t *morphing) {
    for(int i=0;i<1000;i++) {
        mobj_t *site=site_of(type,morphing);
        if(site)return site;
        tick();
    }
    return NULL;
}
/* Tick until the site has this many build frames left; false if it never does. */
static bool until_frames(mobj_t *site,int frames) {
    for(int i=0;i<4000;i++) {
        if(!site->production||site->sc.construction.frames<=frames)break;
        tick();
    }
    return site->production&&site->sc.construction.frames==frames;
}
static void reset(void) {
    P_FreeThinkers();P_InitThinkers();
    memset(level.cell_solid,0,(size_t)level.width*level.height);
    memset(level.blocked,0,(size_t)level.width*level.height);
    level.player_resources[0][0]=1000;
}
static mobj_t *terran_site(mobj_t **scv_out,const StaticProductDefinition **product_out) {
    reset();
    spawn(MT_COMMAND_CENTER,FIXED2_LIT(4.5,4.5),0);
    mobj_t *scv=spawn(MT_SCV,FIXED2_LIT(7.5,7.5),0);
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,MT_BARRACKS);
    if(!scv||!product||!G_PlaceProduct(scv,product,(ivec2_t){18,12}))return NULL;
    *scv_out=scv;*product_out=product;
    return reach_site(MT_BARRACKS,NULL);
}
/* Frames between two game tics, as the engine counts them. */
static int frames_between(int from,int to) { return to*24/RTS_TICRATE-from*24/RTS_TICRATE; }

static int terran_hp_steps(void) {
    mobj_t *scv;const StaticProductDefinition *product;
    mobj_t *site=terran_site(&scv,&product);
    CHECK(site&&site->target==scv);
    /* The foundation is created and stepped in the same tic, so at most one
     * frame has run when the test first sees it. */
    int done=1200-site->sc.construction.frames;
    CHECK(done>=0&&done<=1&&raw(site)==25600+192*done);
    CHECK(until_frames(site,1100));
    CHECK(raw(site)==25600+192*100&&site->hp==175&&site->sc.wound==0);
    CHECK(until_frames(site,600));
    CHECK(raw(site)==25600+192*600&&site->hp==550);
    /* The shared timer mirrors the frames for the status bar. */
    CHECK(site->production->time_left_ms==(600*1000+23)/24);
    return 0;
}

static int terran_completion_frame_count(void) {
    mobj_t *scv;const StaticProductDefinition *product;
    mobj_t *site=terran_site(&scv,&product);
    CHECK(site);
    int first=1200-site->sc.construction.frames;
    int start=leveltime;
    uint32_t id=site->id;
    for(int i=0;i<4000&&site->production;i++)tick();
    CHECK(!site->production&&P_MobjById(id)==site);
    /* resume_building_unit adds HP before it tests the timer, so the building
     * finishes on frame 1201 with the full 1000 HP (the step overshoots). */
    CHECK(frames_between(start,leveltime)+first==1201);
    CHECK(site->hp==1000&&site->sc.wound==0);
    return 0;
}

static int terran_early_damage(void) {
    mobj_t *scv;const StaticProductDefinition *product;
    mobj_t *site=terran_site(&scv,&product);
    CHECK(site&&until_frames(site,1100));
    P_DamageMobj(site,NULL,50);
    CHECK(raw(site)==25600+192*100-50*256);
    CHECK(until_frames(site,500));
    CHECK(raw(site)==25600+192*700-50*256);
    for(int i=0;i<4000&&site->production;i++)tick();
    /* 25600+192*1201 less the 50 HP: 243392/256 = 950.75, kept as 951 and 64/256 wound. */
    CHECK(!site->production&&site->hp==951&&site->sc.wound==64);
    return 0;
}

static int terran_late_damage(void) {
    mobj_t *scv;const StaticProductDefinition *product;
    mobj_t *site=terran_site(&scv,&product);
    CHECK(site&&until_frames(site,3));
    int64_t before=raw(site);
    CHECK(before==25600+192*1197);
    P_DamageMobj(site,NULL,1);
    for(int i=0;i<200&&site->production;i++)tick();
    /* Four more steps of 192 leave 255936: the HP lost late is not healed on completion. */
    CHECK(!site->production&&raw(site)==255936&&site->hp==1000&&site->sc.wound==64);
    return 0;
}

static int terran_interrupted_and_resumed(void) {
    mobj_t *scv;const StaticProductDefinition *product;
    mobj_t *site=terran_site(&scv,&product);
    CHECK(site&&until_frames(site,800));
    CHECK(raw(site)==25600+192*400);
    ticcmd_t leave={.order=TC_MOVE,.count=1,.units={scv->id},
        .position=fixed3_from_fixed2(FIXED2_LIT(2,2),0)};
    G_RunTiccmd(0,&leave);
    tick();
    int64_t paused_hp=raw(site);
    int paused_frames=site->sc.construction.frames;
    CHECK(!site->target);
    for(int i=0;i<60;i++)tick();
    CHECK(raw(site)==paused_hp&&site->sc.construction.frames==paused_frames);
    mobj_t *other=spawn(MT_SCV,FIXED2_LIT(7.5,7.5),0);
    CHECK(other);
    G_RunTiccmd(0,&(ticcmd_t){.order=TC_CONSTRUCT,.product=MT_BARRACKS,.count=1,
        .units={other->id},.position=fixed3_from_fixed2(fixed2_cell_center((ivec2_t){18,12}),0)});
    CHECK(site->target==other);
    int resumed_frames=site->sc.construction.frames;
    int start=leveltime;
    for(int i=0;i<4000&&site->production;i++)tick();
    CHECK(!site->production&&site->hp==1000);
    /* Travel to the site is not building time: the remaining steps, plus the
     * completing one, are what the new SCV contributes. */
    CHECK(resumed_frames==paused_frames&&frames_between(start,leveltime)>=resumed_frames+1);
    return 0;
}

static int terran_cancel_refund(void) {
    mobj_t *scv;const StaticProductDefinition *product;
    mobj_t *site=terran_site(&scv,&product);
    CHECK(site&&until_frames(site,900));
    int before=level.player_resources[0][0];
    CHECK(sc_cancel_production(site,-1));
    CHECK(level.player_resources[0][0]==before+112&&product->cost==150);
    CHECK(!scv->target&&!scv->sc.queue.active);
    return 0;
}

static int protoss_steps_and_release(void) {
    reset();
    mobj_t *probe=spawn(MT_PROBE,FIXED2_LIT(7.5,22.5),0);
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,MT_PYLON);
    CHECK(probe&&product&&G_PlaceProduct(probe,product,(ivec2_t){18,24}));
    mobj_t *site=reach_site(MT_PYLON,NULL);
    CHECK(site);
    /* The Probe is free the moment the foundation exists. */
    CHECK(!probe->production&&!probe->target&&!probe->sc.queue.active);
    int first=450-site->sc.construction.frames;
    CHECK(first<=1&&raw(site)==7680+154*first&&site->sc.shields==7680+153*first);
    int start=leveltime;
    CHECK(until_frames(site,350));
    /* 100 frames: HP 7680+154*100 = 23080 (90.16 -> 91 and 216/256 wound);
     * shields gain 153 from construction plus 7 regenerated per frame. */
    CHECK(raw(site)==23080&&site->hp==91&&site->sc.wound==216);
    /* Regeneration runs from P_Ticker, one tic ahead of the production
     * ticker, so 99 or 100 of its frames have been added at this point. */
    int regen=site->sc.shields-(7680+153*100);
    CHECK(regen==7*99||regen==7*100);
    P_DamageMobj(site,NULL,50);
    CHECK(raw(site)==23080-50*256);
    for(int i=0;i<4000&&site->production;i++)tick();
    /* Warp-in (order_IncompleteWarping): frame 451 is T, the first that sees the
     * timer at zero, and steps. States 1 to 3 wait for the IScript signals, 70
     * frames in all (p_handover.c: 4 + 18 + 48), without steps; the completion
     * frame T + 70 = 521 steps once more (state 3 falls through to the step),
     * 452 steps in all. Shield regeneration refills the shield to 300, HP
     * keeps the damage. */
    CHECK(sc_hand_over(MT_PYLON)==70);
    CHECK(frames_between(start,leveltime)+first==521);
    CHECK(site->sc.shields==300*256);
    /* 7680+154*452-50*256 = 64488 = 252*256-24 */
    CHECK(raw(site)==7680+154*452-50*256&&site->hp==252&&site->sc.wound==24);
    return 0;
}

static int protoss_cancel(void) {
    reset();
    mobj_t *probe=spawn(MT_PROBE,FIXED2_LIT(7.5,22.5),0);
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,MT_PYLON);
    CHECK(probe&&product&&G_PlaceProduct(probe,product,(ivec2_t){18,24}));
    mobj_t *site=reach_site(MT_PYLON,NULL);
    CHECK(site&&until_frames(site,200));
    int before=level.player_resources[0][0];
    CHECK(sc_cancel_production(site,-1));
    CHECK(level.player_resources[0][0]==before+75&&product->cost==100);
    CHECK(probe->type_id==MT_PROBE&&!probe->remove);
    return 0;
}

static int zerg_steps_and_cancel(void) {
    reset();
    spawn(MT_HATCHERY,FIXED2_LIT(10.5,34.5),0);
    mobj_t *drone=spawn(MT_DRONE,FIXED2_LIT(13.5,34.5),0);
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,MT_SPAWNING_POOL);
    CHECK(drone&&product);
    drone->hp=17;
    CHECK(G_PlaceProduct(drone,product,(ivec2_t){16,34}));
    mobj_t *site=reach_site(MT_SPAWNING_POOL,drone);
    CHECK(site==drone&&site->sc.construction.worker_hp==17);
    CHECK(raw(site)==19200+144*(1200-site->sc.construction.frames));
    CHECK(until_frames(site,1100));
    /* 19200+144*100 = 33600: 131.25 HP, kept as 132 and 192/256 wound. */
    CHECK(raw(site)==33600&&site->hp==132&&site->sc.wound==192);
    int start=leveltime;
    P_DamageMobj(site,NULL,10);
    for(int i=0;i<4000&&site->production;i++)tick();
    /* order_IncompleteMorphing steps while the timer runs (1100 more frames) and
     * on the two frames after it (states 4 and 5); state 6 then waits for the
     * AlmostBuilt signal (sigorder 4 on call 6) and completes 7 frames after T,
     * the frame after the timer ends, without a step: 1100 + 1 + 7 = 1108. */
    CHECK(sc_hand_over(MT_SPAWNING_POOL)==7);
    CHECK(frames_between(start,leveltime)==1108);
    CHECK(raw(site)==19200+144*1202-10*256&&site->hp==742&&site->sc.wound==224);

    /* Cancelling a second pool returns the Drone at its old HP. */
    reset();
    spawn(MT_HATCHERY,FIXED2_LIT(10.5,34.5),0);
    drone=spawn(MT_DRONE,FIXED2_LIT(13.5,34.5),0);
    drone->hp=17;
    CHECK(G_PlaceProduct(drone,product,(ivec2_t){16,34}));
    site=reach_site(MT_SPAWNING_POOL,drone);
    CHECK(site&&until_frames(site,900));
    int before=level.player_resources[0][0];
    CHECK(sc_cancel_production(site,-1));
    CHECK(level.player_resources[0][0]==before+product->cost*3/4);
    CHECK(drone->type_id==MT_DRONE&&drone->hp==17&&drone->sc.wound==0);
    return 0;
}

/* The hand-over frames come from iscript.bin: 70 for every warped-in building (4 frames
 * for the Warp Anchor's SpecialState1 sigorder on call 4, 18 for the Warp Texture Init
 * sigorder on call 19, 48 for the warp flash's counter), 7 for every Zerg building (1 +
 * the AlmostBuilt sigorder on call 6), none where a script has no such path. */
static int hand_over_table(void) {
    static const int protoss[]={MT_NEXUS,MT_ROBOTICS_FACILITY,MT_PYLON,MT_ASSIMILATOR,MT_OBSERVATORY,MT_GATEWAY,
        MT_PHOTON_CANNON,MT_CITADEL_OF_ADUN,MT_CYBERNETICS_CORE,MT_TEMPLAR_ARCHIVES,MT_FORGE,MT_STARGATE,
        MT_FLEET_BEACON,MT_ARBITER_TRIBUNAL,MT_ROBOTICS_SUPPORT_BAY,MT_SHIELD_BATTERY};
    static const int zerg[]={MT_HATCHERY,MT_NYDUS_CANAL,MT_HYDRALISK_DEN,MT_DEFILER_MOUND,MT_GREATER_SPIRE,
        MT_QUEENS_NEST,MT_EVOLUTION_CHAMBER,MT_ULTRALISK_CAVERN,MT_SPIRE,MT_SPAWNING_POOL,MT_CREEP_COLONY,
        MT_SPORE_COLONY,MT_SUNKEN_COLONY,MT_EXTRACTOR};
    for(unsigned i=0;i<sizeof(protoss)/sizeof(*protoss);i++)CHECK(sc_hand_over(protoss[i])==70);
    for(unsigned i=0;i<sizeof(zerg)/sizeof(*zerg);i++)CHECK(sc_hand_over(zerg[i])==7);
    /* Terran buildings and units, and the Protoss buildings whose WarpIn overlays nothing, have none. */
    CHECK(!sc_hand_over(MT_BARRACKS)&&!sc_hand_over(MT_COMMAND_CENTER)&&!sc_hand_over(MT_MARINE)&&
          !sc_hand_over(MT_SCV)&&!sc_hand_over(169)&&!sc_hand_over(174)&&!sc_hand_over(0)&&!sc_hand_over(SC_TYPES+1));
    return 0;
}

/* The frame before completion is still a foundation; damage and cancellation work in the hand-over. */
static int protoss_hand_over(void) {
    reset();
    mobj_t *probe=spawn(MT_PROBE,FIXED2_LIT(7.5,22.5),0);
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,MT_PYLON);
    CHECK(probe&&product&&G_PlaceProduct(probe,product,(ivec2_t){18,24}));
    mobj_t *site=reach_site(MT_PYLON,NULL);
    CHECK(site&&until_frames(site,0));
    /* The timer ran out on frame A; T = A + 1 steps, frames T + 1 to T + 69 do not, and the
     * completing frame T + 70 = A + 71 steps once more. */
    /* HP is already full, so take some off to see the steps. */
    P_DamageMobj(site,NULL,50);
    int64_t at_timer_end=raw(site);
    int start=leveltime;
    uint32_t id=site->id;
    int done=0;
    for(int i=0;i<200&&site->production;i++) {
        tick();
        done=frames_between(start,leveltime);
        if(site->production&&done>=1)CHECK(raw(site)==at_timer_end+154);
    }
    CHECK(!site->production&&P_MobjById(id)==site&&done==71&&raw(site)==at_timer_end+2*154);
    /* Cancelling a foundation that is warping in refunds it like any other. */
    reset();
    probe=spawn(MT_PROBE,FIXED2_LIT(7.5,22.5),0);
    CHECK(probe&&G_PlaceProduct(probe,product,(ivec2_t){18,24}));
    site=reach_site(MT_PYLON,NULL);
    CHECK(site&&until_frames(site,0));
    for(int i=0;i<60;i++)tick();
    CHECK(site->production&&site->type_id==MT_PYLON);
    int before=level.player_resources[0][0];
    CHECK(sc_cancel_production(site,-1));
    CHECK(level.player_resources[0][0]==before+75);
    return 0;
}

/* With no iscript.bin the signals are taken as instant: the Pylon completes on the frame
 * after T without a step (the first batch's 452). */
static int unmodelled(void) {
    sc_handover_load("data/none",NULL);
    CHECK(!sc_hand_over(MT_PYLON)&&!sc_hand_over(MT_SPAWNING_POOL));
    reset();
    mobj_t *probe=spawn(MT_PROBE,FIXED2_LIT(7.5,22.5),0);
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,MT_PYLON);
    CHECK(probe&&product&&G_PlaceProduct(probe,product,(ivec2_t){18,24}));
    mobj_t *site=reach_site(MT_PYLON,NULL);
    CHECK(site);
    int first=450-site->sc.construction.frames,start=leveltime;
    for(int i=0;i<4000&&site->production;i++)tick();
    CHECK(frames_between(start,leveltime)+first==452);
    sc_handover_load("data/STARCRAFT",sc_iscript_bin());
    CHECK(sc_hand_over(MT_PYLON)==70);
    return 0;
}

static int extractor_cancel(void) {
    reset();
    free(level.resource_vents);
    level.resource_vents=calloc(1,sizeof(*level.resource_vents));
    level.resource_vent_count=1;
    spawn(MT_HATCHERY,FIXED2_LIT(10.5,34.5),0);
    ivec2_t cell={20,34};
    fixed2_t at=P_BuildingPosition(MT_EXTRACTOR,cell);
    mobj_t *geyser=spawn(MT_VESPENE_GEYSER,at,11);
    const mobjtype_t *geyser_type=P_ActorType(MT_VESPENE_GEYSER);
    CHECK(geyser&&geyser_type);
    ivec2_t geyser_cell=fixed2_cell(at);
    level.resource_vents[0]=(resourcevent_t){.cell=geyser_cell,.footprint=geyser_type->footprint,
        .attachment=fixed2_cell_center((ivec2_t){geyser_cell.x+geyser_type->footprint.w/2,
                                                  geyser_cell.y+geyser_type->footprint.h/2}),
        .resource_type=1,.amount=80,.rate=8,.source_id=geyser->id,.active=true};
    mobj_t *drone=spawn(MT_DRONE,FIXED2_LIT(15.5,34.5),0);
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,MT_EXTRACTOR);
    CHECK(drone&&product);
    drone->hp=9;
    CHECK(G_PlaceProduct(drone,product,cell));
    mobj_t *site=reach_site(MT_EXTRACTOR,drone);
    CHECK(site==drone&&site->production->placed);
    int steps=600-site->sc.construction.frames;
    CHECK(steps<=1&&raw(site)==(int64_t)site->max_hp*256/10+(int64_t)steps*
          (((int64_t)site->max_hp*256*9/10+599)/600));
    int before=level.player_resources[0][0];
    CHECK(sc_cancel_production(site,-1));
    /* 50 minerals refund 37; the Drone returns at full HP, not the 9 it had. */
    CHECK(level.player_resources[0][0]==before+37&&product->cost==50&&drone->type_id==MT_DRONE&&
          drone->hp==drone->max_hp&&drone->sc.wound==0);
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
    CHECK(sc_iscript_load("data/STARCRAFT")&&sc_hand_over(MT_PYLON)>0);
    RTS_RUN(terran_hp_steps());RTS_RUN(terran_completion_frame_count());
    RTS_RUN(terran_early_damage());RTS_RUN(terran_late_damage());
    RTS_RUN(terran_interrupted_and_resumed());RTS_RUN(terran_cancel_refund());
    RTS_RUN(hand_over_table());RTS_RUN(protoss_steps_and_release());RTS_RUN(protoss_cancel());RTS_RUN(protoss_hand_over());
    RTS_RUN(zerg_steps_and_cancel());RTS_RUN(extractor_cancel());RTS_RUN(unmodelled());
    P_FreeLevel(&level);
    puts("PASS: StarCraft construction HP and shields follow OpenBW per-frame steps");
    return 0;
}
