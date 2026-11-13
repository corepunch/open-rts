#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) RTS_CHECK(c,"StarCraft creep",#c)
/* Creep growth and recession against the OpenBW rule (spread_creep,
 * secondary_order_SpreadCreep, set_creep_building_tiles, remove_creep_provider,
 * recede_creep). Expected tiles were derived by hand for the first steps and
 * with an independent throwaway model of the same rule for the long runs; the
 * hand derivation is in the comments. All runs use the 64 by 64 bare map and a
 * fixed LCG seed, so every draw is reproducible. */
enum { W = 64, SEED = 12345 };
static sc_mission_t mission;
static int native; /* native 24 Hz frames passed since reset */

static void reset(uint32_t seed) {
    P_FreeThinkers();P_InitThinkers();
    memset(level.cell_solid,0,(size_t)W*W);
    free(mission.terrain);
    sc_creep_reset(&mission);
    memset(&mission,0,sizeof(mission));
    mission.rng=seed;
    level.mission=&mission;
    leveltime=0;native=0;level.next_mobj_id=0; /* ids are part of the hashed state */
}
static mobj_t *spawn(int type,fixed2_t at,int owner) {
    mobj_t *u=sc_spawn_actor((unsigned)type-1,(ivec2_t){at.x>>(FIXED_FRAC_BITS-5),at.y>>(FIXED_FRAC_BITS-5)},(uint8_t)owner);
    if(u)u->allegiance=owner==consoleplayer?ALLEGIANCE_PLAYER:ALLEGIANCE_ENEMY;
    return u;
}
static void tick(void) {
    P_Ticker();G_ProductionTicker(RTS_TICK_MS);
    native+=leveltime*24/RTS_TICRATE-(leveltime-1)*24/RTS_TICRATE;
}
/* Runs engine tics until the total reaches n native frames, stopping on the tic that completes frame n. */
static void until(int n) { while(native<n)tick(); }
static bool has(int x,int y) { return sc_creep_at((ivec2_t){x,y}); }
static int total(void) {
    int n=0;
    for(int y=0;y<W;y++)for(int x=0;x<W;x++)n+=has(x,y);
    return n;
}
/* Creep tiles as a bit per cell, for comparing two states. */
static void snapshot(uint8_t *out) { for(int y=0;y<W;y++)for(int x=0;x<W;x++)out[y*W+x]=(uint8_t)(has(x,y)|sc_creep_receding((ivec2_t){x,y})<<1); }
static bool same(const uint8_t *a,const uint8_t *b) { return !memcmp(a,b,(size_t)W*W); }
/* A 320 by 200 pixel ellipse around a centre in pixels: the tile test of
 * spread_creep, dx*dx*25 + dy*dy*64 <= 320*320*25 from the tile centre. */
static bool in_ellipse(int x,int y,int px,int py) {
    int dx=x*32+16-px,dy=y*32+16-py;
    return dx*dx*25+dy*dy*64<=320*320*25;
}
static void give_production(mobj_t *u,int type) {
    u->production=calloc(1,sizeof(*u->production));
    *u->production=(production_t){.actor_id=(uint16_t)type,.product_class=RTS_PRODUCT_BUILDING,
        .product_type=(uint16_t)type,.queue_count=1,.time_ms=100000000,.time_left_ms=100000000,.placed=true};
    u->sc.construction.frames=sc_units[type-1].build_time;
}

/* A completed Hatchery on open ground. Its centre is pixel (640, 656): 4 by 3
 * tiles at x 18..21, y 19..21. set_creep_building_tiles adds two tiles left
 * and right and one above and below, without the four corners: 36 tiles.
 * The first spread (frame 1) draws from the creep-free tiles with the most
 * creep neighbours (3): above (18..21,17) and below (18..21,23), the left
 * and right middle tiles (15,20) and (24,20), and the four corner notches
 * (16,18) (23,18) (16,22) (23,22), scanned row by row: 14 candidates. The
 * first LCG output of seed 12345 is 15301, 15301>>4 = 956, 956 % 14 = 4: the
 * fifth candidate, (16,18). */
static int hatchery_growth(void) {
    reset(SEED);
    mobj_t *h=spawn(MT_HATCHERY,FIXED2_LIT(20,20.5),0);
    CHECK(h&&total()==0);
    until(0);tick();
    CHECK(native==0&&total()==36);
    for(int y=19;y<=21;y++)for(int x=16;x<=23;x++)CHECK(has(x,y));
    for(int x=17;x<=22;x++)CHECK(has(x,18)&&has(x,22));
    CHECK(!has(16,18)&&!has(23,18)&&!has(16,22)&&!has(23,22)&&!has(15,20)&&!has(20,17));
    until(1);
    CHECK(total()==37&&has(16,18));
    /* A spread every 16 frames: the next at frame 17. */
    until(16);CHECK(total()==37);
    until(17);CHECK(total()==38&&has(19,17));
    /* Steps 3 to 6 from an independent model of the same rule (bucket of the
     * tiles with the most creep neighbours, scan order, (r>>4) % size). */
    until(33);CHECK(total()==39&&has(18,17));
    until(49);CHECK(total()==40&&has(20,17));
    until(65);CHECK(total()==41&&has(21,17));
    until(81);CHECK(total()==42&&has(17,17));
    until(80+16*400);
    /* Final footprint: every tile of the 20 by 12.5 tile ellipse, nothing else. */
    int count=0;
    for(int y=0;y<W;y++)for(int x=0;x<W;x++) {
        bool footprint=x>=18&&x<=21&&y>=19&&y<=21,ring=x>=16&&x<=23&&y>=18&&y<=22;
        CHECK(has(x,y)==(in_ellipse(x,y,640,656)||footprint||ring));
        count+=has(x,y);
    }
    CHECK(count==200&&total()==200);
    return 0;
}

/* A Creep Colony has no ring: its 2 by 2 footprint at x 29..30, y 29..30, then
 * eight tiles touch it with two neighbours: (29,28) (30,28) (28,29) (31,29)
 * (28,30) (31,30) (29,31) (30,31) in scan order. The first draw of seed 12345
 * is 15301, 15301>>4 = 956, 956 % 8 = 4: the fifth, (28,30). The colony spreads over
 * the Hive's ellipse like a Hatchery (spread_creep takes it from
 * secondary_order_SpreadCreep for every provider). */
static int colony_growth(void) {
    reset(SEED);
    mobj_t *c=spawn(MT_CREEP_COLONY,FIXED2_LIT(30,30),0);
    CHECK(c);
    tick();
    CHECK(total()==4&&has(29,29)&&has(30,29)&&has(29,30)&&has(30,30));
    until(1);
    CHECK(total()==5&&has(28,30));
    until(17);CHECK(total()==6&&has(28,29));
    until(33);CHECK(total()==7&&has(29,28));
    until(49);CHECK(total()==8&&has(28,28));
    until(48+16*400);
    for(int y=0;y<W;y++)for(int x=0;x<W;x++) {
        bool footprint=x>=29&&x<=30&&y>=29&&y<=30;
        CHECK(has(x,y)==(in_ellipse(x,y,960,960)||footprint));
    }
    CHECK(total()==196);
    /* A Sunken Colony morph spreads nothing new: StoppingCreepGrowth is ignored,
     * so a Sunken Colony created whole holds its footprint only. */
    reset(SEED);
    mobj_t *s=spawn(MT_SUNKEN_COLONY,FIXED2_LIT(30,30),0);
    CHECK(s);
    until(400);
    CHECK(total()==4);
    return 0;
}

/* Unbuildable tiles and the tile above them, a partly walkable megatile and a
 * building in the way are never given creep. */
static int blocked_tiles(void) {
    reset(777);
    mission.terrain=calloc((size_t)W*W,sizeof(sc_terrain_t));
    CHECK(mission.terrain);
    for(int i=0;i<W*W;i++)for(int k=0;k<16;k++)mission.terrain[i].vf4[k]=1;
    for(int y=0;y<W;y++)for(int x=24;x<=25;x++)mission.terrain[y*W+x].cv5|=SC_TERRAIN_UNBUILDABLE;
    mission.terrain[25*W+20].cv5|=SC_TERRAIN_UNBUILDABLE;
    mission.terrain[20*W+14].vf4[0]=0; /* one minitile blocked: partly walkable */
    mobj_t *h=spawn(MT_HATCHERY,FIXED2_LIT(20,20.5),0);
    CHECK(h);
    sc_creep_initial(h);
    /* The wall x 24..25 is unbuildable and cuts the ellipse; (20,24) lies above
     * the unbuildable (20,25) and so cannot hold creep either. */
    CHECK(total()==147);
    CHECK(has(23,20)&&!has(24,20)&&!has(25,20)&&!has(26,20));
    CHECK(!has(20,25)&&!has(20,24)&&has(19,24)&&has(21,24));
    CHECK(!has(14,20)&&has(14,19)&&has(14,21)&&has(13,20));
    /* A map's own creep (CV5 flag 0x40) is present from the start. */
    reset(1);
    mission.terrain=calloc((size_t)W*W,sizeof(sc_terrain_t));
    CHECK(mission.terrain);
    for(int i=0;i<W*W;i++)for(int k=0;k<16;k++)mission.terrain[i].vf4[k]=1;
    mission.terrain[5*W+5].cv5|=SC_TERRAIN_CREEP;
    mission.terrain[W*(W-1)+5].cv5|=SC_TERRAIN_CREEP; /* the bottom row loses it */
    sc_creep_reset(&mission);
    CHECK(has(5,5)&&!has(6,5)&&!has(5,W-1)&&total()==1);
    /* A building placed before the Hatchery's start-of-map spread stands in
     * the way (create_initial_unit runs in CHK order); one placed after does
     * not. A Command Center at x 28..31, y 19..21 takes nine ellipse tiles. */
    reset(SEED);
    mobj_t *cc=spawn(MT_COMMAND_CENTER,FIXED2_LIT(30,20.5),0);
    h=spawn(MT_HATCHERY,FIXED2_LIT(20,20.5),0);
    CHECK(cc&&h);
    sc_creep_initial(h);
    CHECK(total()==194&&has(27,20)&&!has(28,20)&&!has(30,21)&&!has(31,19));
    reset(SEED);
    h=spawn(MT_HATCHERY,FIXED2_LIT(20,20.5),0);
    CHECK(h);
    sc_creep_initial(h);
    cc=spawn(MT_COMMAND_CENTER,FIXED2_LIT(30,20.5),0);
    CHECK(cc&&total()==200&&has(28,20));
    return 0;
}

/* A foundation covers only its footprint; once complete (or still morphing
 * after it was) the Hatchery gets its ring and spreads. */
static int incomplete_provider(void) {
    reset(SEED);
    mobj_t *h=spawn(MT_HATCHERY,FIXED2_LIT(20,20.5),0);
    CHECK(h);
    give_production(h,MT_HATCHERY);
    until(400);
    CHECK(!G_ModelUnitReady(h)&&total()==0);
    P_FreeMobjProduction(h);
    int start=native;
    tick();
    /* set_creep_building_tiles at completion: 36 tiles, then the first spread
     * on the first frame, one every 16 frames. */
    static const int checks[]={0,1,16,17,33,49};
    for(size_t i=0;i<sizeof(checks)/sizeof(checks[0]);i++) {
        int frames=checks[i];
        if(frames>=native-start)until(start+frames);
        int done=native-start;
        CHECK(done>=frames);
        CHECK(total()==36+(done?(done-1)/16+1:0));
    }
    return 0;
}

/* A Creep Colony standing in the morph to a Sunken Colony and a Hatchery in
 * its Lair morph keep spreading: SpreadCreep is a secondary order that no
 * morph changes. */
static int morphing_provider(void) {
    reset(SEED);
    mobj_t *h=spawn(MT_HATCHERY,FIXED2_LIT(20,20.5),0);
    CHECK(h);
    until(33);
    int grown=total();
    CHECK(grown==36+3);
    h->production=calloc(1,sizeof(*h->production));
    *h->production=(production_t){.actor_id=MT_LAIR,.product_class=RTS_PRODUCT_BUILDING,
        .product_type=MT_LAIR,.queue_count=1,.time_ms=100000,.time_left_ms=100000};
    until(49);CHECK(total()==grown+1);
    until(65);CHECK(total()==grown+2);
    return 0;
}

/* Hatchery at (20, 20.5) from the game-start state: the full ellipse at once,
 * the same 200 tiles growth ends with. */
static int pre_placed(void) {
    reset(SEED);
    mobj_t *h=spawn(MT_HATCHERY,FIXED2_LIT(20,20.5),0);
    CHECK(h&&total()==0);
    sc_creep_initial(h);
    CHECK(total()==200);
    for(int y=0;y<W;y++)for(int x=0;x<W;x++)CHECK(has(x,y)==(in_ellipse(x,y,640,656)||(x>=18&&x<=21&&y>=19&&y<=21)));
    /* The first tic does not apply the completion ring again. */
    tick();until(100);
    CHECK(total()==200&&sc_creep_receding_count()==0);
    return 0;
}

/* Death of the only provider: every ellipse tile starts receding, and one
 * tile leaves every ten frames (recede_creep's period 9 plus the step), always
 * from the first non-empty list of tiles with the fewest creep neighbours.
 * The seed-12345 model gives (25,15) at frame 1, (28,23) at 11, (27,24) at
 * 21, (17,26) at 31, (12,16) at 41 and (11,17) at 51. */
static int recession(void) {
    reset(SEED);
    mobj_t *h=spawn(MT_HATCHERY,FIXED2_LIT(20,20.5),0);
    CHECK(h);
    sc_creep_initial(h);
    P_DamageMobj(h,NULL,h->hp);
    CHECK(h->hp<=0);
    tick();
    CHECK(native==0&&total()==200&&sc_creep_receding_count()==200);
    CHECK(sc_creep_receding((ivec2_t){20,15})&&has(25,15));
    static const struct { int frame, x, y; } order[]={{1,25,15},{11,28,23},{21,27,24},{31,17,26},{41,12,16},{51,11,17}};
    int gone=0;
    for(int i=0;i<6;i++) {
        until(order[i].frame-1);
        CHECK(has(order[i].x,order[i].y)&&total()==200-gone);
        /* The tile that goes has the fewest creep neighbours among all receding tiles. */
        int best=9,count=0,mine=-1;
        for(int y=0;y<W;y++)for(int x=0;x<W;x++)if(has(x,y)&&sc_creep_receding((ivec2_t){x,y})) {
            int n=0;
            for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++)n+=(dx||dy)&&has(x+dx,y+dy);
            if(n<best)best=n;
            if(x==order[i].x&&y==order[i].y)mine=n;
            ++count;
        }
        CHECK(count==200-gone&&mine==best);
        until(order[i].frame);
        CHECK(!has(order[i].x,order[i].y)&&total()==200-gone-1);
        ++gone;
    }
    /* No creep is left once every tile has had its turn: 200 tiles, 10 frames apart. */
    until(1990);
    CHECK(total()==1&&sc_creep_receding_count()==1);
    until(1991);
    CHECK(total()==0&&sc_creep_receding_count()==0);
    return 0;
}

/* Creep sustained by another provider or by a Zerg building's footprint never
 * recedes, and a building on receding creep takes no damage (creep is read
 * only by placement, larvae and movement in OpenBW). */
static int sustained_creep(void) {
    reset(5);
    mobj_t *a=spawn(MT_HATCHERY,FIXED2_LIT(20,20.5),0);
    sc_creep_initial(a);
    mobj_t *b=spawn(MT_HATCHERY,FIXED2_LIT(30,20.5),0);
    sc_creep_initial(b);
    CHECK(a&&b&&total()==322);
    /* A Spawning Pool at x 11..13, y 19..20 stands on creep only the first
     * Hatchery covers; its footprint sustains six tiles that would recede. */
    mobj_t *pool=spawn(MT_SPAWNING_POOL,FIXED2_LIT(12.5,20),0);
    CHECK(pool);
    int hp=pool->hp;
    P_DamageMobj(a,NULL,a->hp);
    tick();
    CHECK(sc_creep_receding_count()==116);
    /* No tile of the second Hatchery's ellipse is ever marked. */
    for(int y=0;y<W;y++)for(int x=0;x<W;x++)
        if(in_ellipse(x,y,960,656)||(x>=28&&x<=31&&y>=19&&y<=21))CHECK(!sc_creep_receding((ivec2_t){x,y}));
    until(1500);
    CHECK(sc_creep_receding_count()==0&&total()==206);
    for(int y=0;y<W;y++)for(int x=0;x<W;x++)
        CHECK(has(x,y)==(in_ellipse(x,y,960,656)||(x>=28&&x<=31&&y>=19&&y<=21)||(x>=11&&x<=13&&y>=19&&y<=20)));
    CHECK(pool->hp==hp&&pool->hp>0);
    /* A Hatchery on receding creep: a foundation placed over tiles that are
     * leaving keeps them (add_creep_provider). */
    reset(SEED);
    a=spawn(MT_HATCHERY,FIXED2_LIT(20,20.5),0);
    sc_creep_initial(a);
    P_DamageMobj(a,NULL,a->hp);
    until(20);
    CHECK(has(11,17)&&has(12,17)&&has(11,18)&&has(12,18)&&sc_creep_receding((ivec2_t){11,17}));
    mobj_t *colony=spawn(MT_CREEP_COLONY,FIXED2_LIT(12,18),0);
    CHECK(colony);
    give_production(colony,MT_CREEP_COLONY);
    tick();
    CHECK(!sc_creep_receding((ivec2_t){11,17})&&!sc_creep_receding((ivec2_t){12,18}));
    until(250);
    CHECK(has(11,17)&&has(12,17)&&has(11,18)&&has(12,18)&&!sc_creep_receding((ivec2_t){12,18})&&
          sc_creep_receding_count()>0&&total()>4);
    /* A foundation that dies leaves its footprint to recede like any other creep. */
    P_DamageMobj(colony,NULL,colony->hp);
    until(2400);
    CHECK(total()==0&&sc_creep_receding_count()==0);
    return 0;
}

/* Placement reads the tile flag: a footprint wholly on creep, growing or
 * receding, is allowed for a building that needs creep and denied for one that
 * does not. */
static int placement(void) {
    reset(SEED);
    CHECK(actor_types[MT_SPAWNING_POOL-1].footprint.w==3&&actor_types[MT_SPAWNING_POOL-1].footprint.h==2&&
          actor_types[MT_CREEP_COLONY-1].footprint.w==2&&actor_types[MT_SUPPLY_DEPOT-1].footprint.h==2);
    mobj_t *h=spawn(MT_HATCHERY,FIXED2_LIT(20,20.5),0);
    CHECK(h);
    tick();
    /* Spawning Pool 3 by 2 tiles: inside the ring (16..18,19..20), straddling its edge, outside it. */
    CHECK(sc_ground_ok(MT_SPAWNING_POOL,(ivec2_t){16,19},0));
    CHECK(sc_ground_ok(MT_SPAWNING_POOL,(ivec2_t){21,19},0));
    CHECK(!sc_ground_ok(MT_SPAWNING_POOL,(ivec2_t){14,19},0));
    CHECK(!sc_ground_ok(MT_SPAWNING_POOL,(ivec2_t){13,19},0));
    /* A Supply Depot (no creep) is denied where there is creep and allowed off it. */
    CHECK(!sc_ground_ok(MT_SUPPLY_DEPOT,(ivec2_t){17,19},0));
    CHECK(sc_ground_ok(MT_SUPPLY_DEPOT,(ivec2_t){5,5},0));
    CHECK(sc_ground_ok(MT_HATCHERY,(ivec2_t){17,19},0)); /* A Zerg depot may stand on creep. */
    /* Growth opens tiles: a Creep Colony at (16,18) needs the notch tile
     * (16,18), which the first spread adds. */
    CHECK(!sc_ground_ok(MT_CREEP_COLONY,(ivec2_t){16,18},0));
    until(1);
    CHECK(sc_ground_ok(MT_CREEP_COLONY,(ivec2_t){16,18},0));
    /* Receding creep still holds a building until its own tile goes. */
    reset(SEED);
    h=spawn(MT_HATCHERY,FIXED2_LIT(20,20.5),0);
    sc_creep_initial(h);
    P_DamageMobj(h,NULL,h->hp);
    until(50);
    CHECK(sc_creep_receding((ivec2_t){11,17})&&sc_ground_ok(MT_CREEP_COLONY,(ivec2_t){11,17},0));
    CHECK(!sc_ground_ok(MT_SUPPLY_DEPOT,(ivec2_t){11,17},0));
    until(51);
    CHECK(!has(11,17)&&has(12,17)&&!sc_ground_ok(MT_CREEP_COLONY,(ivec2_t){11,17},0));
    /* Once the creep is gone a Supply Depot may take the tile. */
    until(1991);
    CHECK(total()==0&&sc_ground_ok(MT_SUPPLY_DEPOT,(ivec2_t){11,17},0));
    return 0;
}

static uint32_t run_hash(uint32_t seed,int frames) {
    reset(seed);
    mobj_t *h=spawn(MT_HATCHERY,FIXED2_LIT(20,20.5),0);
    CHECK(h);
    until(frames);
    return G_ConsistencyExtra(0);
}
static int determinism(void) {
    uint32_t a=run_hash(SEED,300),b=run_hash(SEED,300),c=run_hash(SEED+1,300);
    CHECK(a==b&&a!=c);
    uint8_t x[W*W],y[W*W];
    run_hash(SEED,300);snapshot(x);
    run_hash(SEED,300);snapshot(y);
    CHECK(same(x,y));
    /* The state is hashed: one more spread changes the consistency hash. */
    CHECK(run_hash(SEED,300)!=run_hash(SEED,317));
    return 0;
}

/* The state saved mid-growth and mid-recession resumes to the same result. */
static int save_load(void) {
    uint8_t whole[W*W],resumed[W*W];
    for(int phase=0;phase<2;phase++) {
        reset(SEED);
        mobj_t *h=spawn(MT_HATCHERY,FIXED2_LIT(20,20.5),0);
        CHECK(h);
        if(phase) { sc_creep_initial(h);P_DamageMobj(h,NULL,h->hp); }
        until(200);
        size_t size=G_SaveExtraSize();
        void *blob=malloc(size);
        CHECK(blob);
        G_SaveExtra(blob);
        int saved_time=leveltime,saved_native=native;
        uint32_t saved_hash=G_ConsistencyExtra(0);
        uint8_t mid[W*W];snapshot(mid);
        until(500);
        snapshot(whole);
        uint32_t end_hash=G_ConsistencyExtra(0);
        CHECK(!same(mid,whole)&&end_hash!=saved_hash);
        CHECK(G_LoadExtra(blob,size));
        uint8_t back[W*W];snapshot(back);
        CHECK(same(mid,back)&&G_ConsistencyExtra(0)==saved_hash);
        leveltime=saved_time;native=saved_native;
        until(500);
        snapshot(resumed);
        CHECK(same(whole,resumed)&&G_ConsistencyExtra(0)==end_hash);
        free(blob);
    }
    return 0;
}

/* The saved record is checked field by field and a bad one is refused whole. */
static int malformed(void) {
    reset(SEED);
    mobj_t *a=spawn(MT_HATCHERY,FIXED2_LIT(20,20.5),0),*b=spawn(MT_HATCHERY,FIXED2_LIT(30,20.5),0);
    CHECK(a&&b);
    sc_creep_initial(a);
    sc_creep_initial(b);
    P_DamageMobj(a,NULL,a->hp);
    until(120);
    size_t size=G_SaveExtraSize(),creep=sc_creep_save_size();
    CHECK(creep&&creep<size);
    uint8_t *blob=malloc(size),*copy=malloc(size);
    CHECK(blob&&copy);
    G_SaveExtra(blob);
    uint32_t hash=G_ConsistencyExtra(0);
    CHECK(G_LoadExtra(blob,size)&&G_ConsistencyExtra(0)==hash);
    /* Creep record layout: w, h, recede_timer, 9 list counts, tracked, 1024
     * listed tiles, 1024 tracked buildings of 16 bytes (id, type, x, y,
     * flags, order, timer, padding), then one byte a tile. */
    uint8_t *rec=blob+size-creep;
    enum { TIMER=8,COUNTS=12,TRACKED=48,LISTED=52,BUILDINGS=LISTED+4096,TILES=BUILDINGS+16*1024 };
    uint32_t counts[9],tracked;
    memcpy(counts,rec+COUNTS,sizeof(counts));
    memcpy(&tracked,rec+TRACKED,4);
    CHECK(tracked==1);
    int filled=-1;
    for(int i=0;i<9&&filled<0;i++)if(counts[i])filled=i;
    CHECK(filled>=0);
    size_t first_listed=0;
    for(int i=0;i<filled;i++)first_listed+=counts[i];
    uint32_t listed;
    memcpy(&listed,rec+LISTED+4*first_listed,4);
    memcpy(copy,blob,size);
    struct { size_t at; uint32_t value; int bytes; } bad[]={
        {0,W+1,4},{4,W-1,4},                                  /* size of another map */
        {TIMER,10,4},{TIMER,(uint32_t)-1,4},                  /* recede timer */
        {COUNTS+4*(size_t)filled,counts[filled]+1,4},         /* a list longer than its tiles */
        {COUNTS+4*(size_t)filled,1025,4},                     /* more than the 1024 entries */
        {TRACKED,1025,4},{TRACKED,2,4},                       /* too many, or an empty entry */
        {LISTED+4*first_listed,W*W,4},                        /* a tile off the map */
        {LISTED+4*first_listed,listed+1,4},                   /* a tile not receding or in the wrong list */
        {BUILDINGS+4,0,2},                                    /* type 0 */
        {BUILDINGS+6,W*32,2},                                 /* off the map */
        {BUILDINGS+10,4,1},{BUILDINGS+11,3,1},{BUILDINGS+12,16,1},{BUILDINGS+13,1,1}, /* flags, order, timer, padding */
        {BUILDINGS+16*5,1,4},                                 /* data past the tracked count */
        {TILES+3,4,1},                                        /* an undefined tile bit */
        {TILES+0,2,1},                                        /* receding without creep */
    };
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        memcpy(blob,copy,size);
        memcpy(rec+bad[i].at,&bad[i].value,(size_t)bad[i].bytes);
        CHECK(!G_LoadExtra(blob,size));
        CHECK(G_ConsistencyExtra(0)==hash);
    }
    memcpy(blob,copy,size);
    CHECK(!G_LoadExtra(blob,size-1));
    CHECK(G_LoadExtra(blob,size)&&G_ConsistencyExtra(0)==hash);
    free(blob);free(copy);
    return 0;
}

static void put_u16(uint8_t *p,unsigned v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8); }
static void put_u32(uint8_t *p,unsigned v) { put_u16(p,v);put_u16(p+2,v>>16); }
static int chunk(FILE *f,const char *tag,const void *data,size_t size) {
    uint8_t head[8];
    memcpy(head,tag,4);put_u32(head+4,(unsigned)size);
    return fwrite(head,1,8,f)!=8||fwrite(data,1,size,f)!=size;
}
/* A CHK with a Hatchery and a Command Center, in the given UNIT order. */
static int write_map(const char *path,bool center_first) {
    FILE *f=fopen(path,"wb");
    if(!f)return 1;
    uint8_t dim[4],era[2]={0},owners[12]={6,5},units[2*36]={0};
    static uint16_t terrain[W*W];
    put_u16(dim,W);put_u16(dim+2,W);
    for(int i=0;i<W*W;i++)terrain[i]=32;
    uint8_t *hatchery=units+(center_first?36:0),*center=units+(center_first?0:36);
    put_u16(hatchery+4,640);put_u16(hatchery+6,656);put_u16(hatchery+8,MT_HATCHERY-1);hatchery[16]=0;
    put_u16(center+4,960);put_u16(center+6,656);put_u16(center+8,MT_COMMAND_CENTER-1);center[16]=0;
    int failed=chunk(f,"DIM ",dim,4)|chunk(f,"ERA ",era,2)|chunk(f,"MTXM",terrain,sizeof(terrain))|
        chunk(f,"OWNR",owners,12)|chunk(f,"UNIT",units,sizeof(units));
    return failed|(fclose(f)!=0);
}
/* A map's UNIT chunk is spawned in file order and each Hatchery gets create_initial_unit's
 * creep at once: before any tic, and only around what was placed before it. */
static int chk_pre_placed(void) {
    free(mission.terrain);
    sc_creep_reset(&mission);
    level.mission=NULL;
    P_FreeThinkers();P_InitThinkers();
    P_FreeLevel(&level);
    for(int center_first=0;center_first<2;center_first++) {
        const char *path="/tmp/open-rts-sc-creep-initial.chk";
        CHECK(!write_map(path,center_first));
        CHECK(G_DoLoadLevel(path,&level)&&G_LoadLevelData("data/STARCRAFT",&level)&&P_InitSight());
        CHECK(P_LoadThings(level.map_path)==2);
        int count=0;
        for(int y=0;y<W;y++)for(int x=0;x<W;x++) {
            bool center_tile=x>=28&&x<=31&&y>=19&&y<=21;
            bool wanted=(in_ellipse(x,y,640,656)||(x>=18&&x<=21&&y>=19&&y<=21))&&!(center_first&&center_tile);
            CHECK(has(x,y)==wanted);
            count+=has(x,y);
        }
        CHECK(count==(center_first?194:200));
        P_FreeThinkers();P_InitThinkers();
        P_FreeLevel(&level);
        remove(path);
    }
    return 0;
}

int main(void) {
    G_InitGame();
    P_InitThinkers();
    consoleplayer=0;
    level.width=level.height=W;
    level.blocked=calloc((size_t)W*W,1);
    level.cell_solid=calloc((size_t)W*W,1);
    CHECK(level.blocked&&level.cell_solid&&P_InitSight());
    CHECK(hatchery_growth()==0);
    CHECK(colony_growth()==0);
    CHECK(blocked_tiles()==0);
    CHECK(incomplete_provider()==0);
    CHECK(morphing_provider()==0);
    CHECK(pre_placed()==0);
    CHECK(recession()==0);
    CHECK(sustained_creep()==0);
    CHECK(placement()==0);
    CHECK(determinism()==0);
    CHECK(save_load()==0);
    CHECK(malformed()==0);
    CHECK(chk_pre_placed()==0);
    puts("PASS: creep growth, ellipse footprint, blocked tiles, completion, game-start state, recession, sustained creep, placement, determinism, save and malformed state");
    return 0;
}
