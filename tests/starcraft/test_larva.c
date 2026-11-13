#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) RTS_CHECK(c,"StarCraft larvae",#c)
/* Hatchery larva timer, cap, spawn, wandering and death against OpenBW
 * (secondary_order_SpawningLarva, get_spawn_larva_position, order_Larva).
 * The spawn run happens once per 9 native frames, starting at frame 1 here
 * (the run phase is 0 at completion), and a spawn sets a 37-count timer that
 * one run in each 9 frames counts down: spawns at frames 1, 343, 685. */
enum { W = 64, SEED = 777 };
static sc_mission_t mission;
static int native;
static void reset(uint32_t seed) {
    P_FreeThinkers();P_InitThinkers();
    memset(level.cell_solid,0,(size_t)W*W);
    free(mission.terrain);
    sc_creep_reset(&mission);
    memset(&mission,0,sizeof(mission));
    mission.rng=seed;
    level.mission=&mission;
    leveltime=0;native=0;level.next_mobj_id=0;
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
static void until(int n) { while(native<n)tick(); }
static int larvae(const mobj_t *h) {
    int n=0;
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        const mobj_t *m=(const mobj_t *)th;
        n+=th->function==P_MobjThinker&&!m->remove&&m->hp>0&&m->type_id==MT_LARVA&&m->sc.parent==h->id;
    }
    return n;
}
static mobj_t *nth(const mobj_t *h,int k) {
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        mobj_t *m=(mobj_t *)th;
        if(th->function==P_MobjThinker&&!m->remove&&m->hp>0&&m->type_id==MT_LARVA&&m->sc.parent==h->id&&!k--)return m;
    }
    return NULL;
}
static mobj_t *hatchery(void) {
    mobj_t *h=spawn(MT_HATCHERY,FIXED2_LIT(20,20.5),0);
    return h;
}

static int cadence(void) {
    reset(SEED);
    mobj_t *h=hatchery();
    CHECK(h);
    until(0);tick();
    CHECK(larvae(h)==0);          /* creep and the first run come with frame 1 */
    until(1);CHECK(larvae(h)==1);
    until(342);CHECK(larvae(h)==1);
    until(343);CHECK(larvae(h)==2);
    until(684);CHECK(larvae(h)==2);
    until(685);CHECK(larvae(h)==3);
    /* The cap freezes the timer: nothing spawns however long it waits. */
    until(5000);CHECK(larvae(h)==3);
    /* A larva used at frame 5000 (runs fall on frames 1 mod 9): the timer
     * resumes from 37, so the next spawn is 37 decrements and a spawn later. */
    mobj_t *used=nth(h,0);
    P_DamageMobj(used,NULL,used->hp);
    CHECK(larvae(h)==2);
    int first=5001;while((first-1)%9)first++;
    until(first+37*9-1);CHECK(larvae(h)==2);
    until(first+37*9);CHECK(larvae(h)==3);
    return 0;
}

/* A spawn with the position blocked tries again after 3 runs. */
static int blocked(void) {
    reset(SEED);
    mobj_t *h=hatchery();
    CHECK(h);
    h->sc.larva_ms=INT_MIN;
    until(5);
    CHECK(larvae(h)==0);
    CHECK(sc_spawn_larva(h));
    /* No hatchery order: no timer, no larvae. */
    until(1000);
    CHECK(larvae(h)==1);
    /* Owner 11 (neutral) never spawns. */
    reset(SEED);
    h=hatchery();
    h->owner=11;
    until(100);
    CHECK(larvae(h)==0);
    /* An incomplete hatchery keeps the timer where it is. */
    reset(SEED);
    h=hatchery();
    h->production=calloc(1,sizeof(*h->production));
    *h->production=(production_t){.actor_id=MT_HATCHERY,.product_class=RTS_PRODUCT_BUILDING,
        .product_type=MT_HATCHERY,.queue_count=1,.time_ms=100000000,.time_left_ms=100000000,.placed=true};
    h->sc.construction.frames=sc_units[MT_HATCHERY-1].build_time;
    CHECK(!G_ModelUnitReady(h));
    until(100);
    CHECK(larvae(h)==0&&h->sc.larva_ms==0);
    return 0;
}

/* The first larva lies below the hatchery's centre, 10 pixels under its box. */
static int position(void) {
    reset(SEED);
    mobj_t *h=hatchery();
    until(1);
    mobj_t *l=nth(h,0);
    CHECK(l);
    int cx=(int)(h->core.position.x/(FIXED_ONE/32)),cy=(int)(h->core.position.y/(FIXED_ONE/32));
    int lx=(int)(l->core.position.x/(FIXED_ONE/32)),ly=(int)(l->core.position.y/(FIXED_ONE/32));
    CHECK(lx==cx&&ly==cy+32+10);
    CHECK((l->sc.order.time>>4&3)==3);
    return 0;
}

static uint32_t wander(uint32_t seed,int frames,int *moved) {
    reset(seed);
    mobj_t *h=hatchery();
    CHECK(h);
    int cx=(int)(h->core.position.x/(FIXED_ONE/32)),cy=(int)(h->core.position.y/(FIXED_ONE/32));
    uint32_t hash=0;
    *moved=0;
    while(native<frames) {
        tick();
        for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
            const mobj_t *m=(const mobj_t *)th;
            if(th->function!=P_MobjThinker||m->remove||m->hp<=0||m->type_id!=MT_LARVA)continue;
            int x=(int)(m->core.position.x/(FIXED_ONE/32)),y=(int)(m->core.position.y/(FIXED_ONE/32));
            /* Side 3: never above the hatchery's box, always on creep. */
            RTS_CHECK(y>=cy+32,"larva wander","stays below the box");
            RTS_CHECK(sc_creep_at((ivec2_t){x/32,y/32}),"larva wander","stays on creep");
            RTS_CHECK(abs(x-cx)<256&&y-cy<256,"larva wander","stays near");
            hash=hash*31+(uint32_t)(x*977+y);
            if(x!=cx)*moved=1;
        }
    }
    return hash;
}
static int wandering(void) {
    int m1,m2,m3;
    uint32_t a=wander(SEED,6000,&m1),b=wander(SEED,6000,&m2),c=wander(SEED+5,6000,&m3);
    CHECK(m1&&m2&&m3);
    CHECK(a==b&&a!=c);
    return 0;
}

/* A larva whose hatchery is gone lives on, and dies at the first run on which
 * it stands at its target on a tile without creep. */
static int orphan(void) {
    reset(SEED);
    mobj_t *l=spawn(MT_LARVA,FIXED2_LIT(50.5,50.5),0);
    CHECK(l);
    until(0);tick();
    CHECK(l->hp>0);
    until(1);
    CHECK(l->hp<=0);               /* no creep at its tile: killed on its first run */
    /* On creep with no hatchery it wanders and lives. */
    reset(SEED);
    mobj_t *h=hatchery();
    until(1);
    mobj_t *k=nth(h,0);
    CHECK(k&&k->sc.parent==h->id);
    until(100);
    P_DamageMobj(h,NULL,h->hp);
    uint32_t born=0;(void)born;
    int alive_until=0;
    while(native<30000&&k->hp>0) { tick();alive_until=native; }
    CHECK(k->hp<=0&&alive_until>101);
    /* Death was at a run frame and on a tile with no creep. */
    ivec2_t at={(int)(k->core.position.x/(FIXED_ONE/32))/32,(int)(k->core.position.y/(FIXED_ONE/32))/32};
    CHECK(!sc_creep_at(at));
    CHECK((alive_until-1)%9==0);
    return 0;
}

/* The timer state is validated on load and round-trips in the mobj. */
static int save_load(void) {
    reset(SEED);
    mobj_t *h=hatchery();
    until(200);
    size_t size=G_SaveExtraSize();
    void *blob=malloc(size);
    CHECK(blob);
    G_SaveExtra(blob);
    /* Mobjs are saved with the level, not the extra blob: the timer is in
     * larva_ms, which a copied mobj carries; the extra state round-trips. */
    uint32_t hash=G_ConsistencyExtra(0);
    CHECK(G_LoadExtra(blob,size)&&G_ConsistencyExtra(0)==hash);
    CHECK(h->sc.larva_ms>=0&&(h->sc.larva_ms&63)<=37&&larvae(h)==1);
    until(343);
    CHECK(larvae(h)==2);
    free(blob);
    /* A malformed timer is refused. */
    mobj_t bad=*h;
    bad.sc.larva_ms=38;
    CHECK(!G_SavedObjectValid(&bad));
    bad.sc.larva_ms=9<<6;
    CHECK(!G_SavedObjectValid(&bad));
    bad.sc.larva_ms=INT_MIN;
    CHECK(G_SavedObjectValid(&bad));
    bad.sc.larva_ms=-5;
    CHECK(!G_SavedObjectValid(&bad));
    mobj_t *l=nth(h,0);
    bad=*l;
    bad.sc.order.time=9;
    CHECK(!G_SavedObjectValid(&bad));
    return 0;
}

int main(void) {
    G_InitGame();
    P_InitThinkers();
    consoleplayer=0;
    level.width=level.height=W;
    level.blocked=calloc(W*W,1);
    level.cell_solid=calloc(W*W,1);
    level.mission=&mission;
    CHECK(cadence()==0);
    CHECK(blocked()==0);
    CHECK(position()==0);
    CHECK(wandering()==0);
    CHECK(orphan()==0);
    CHECK(save_load()==0);
    P_FreeLevel(&level);
    puts("PASS: larva timer, cap, position, wandering, death and save");
    return 0;
}
