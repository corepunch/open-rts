#include "t_local.h"
#include "sc_local.h"
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c,"StarCraft projectiles",#c)

static const char *map="data/STARCRAFT/install/campaign/terran/terran01/staredit/scenario.chk";
static mobj_t *spawn(int type,fixed2_t at,int owner) {
    mobj_t *u=P_SpawnMobj(fixed3_from_fixed2(at,0),(uint16_t)type);
    if(u){u->owner=u->team=owner;u->allegiance=owner?ALLEGIANCE_ENEMY:ALLEGIANCE_PLAYER;}
    return u;
}
static mobj_t *fire(mobj_t *source,mobj_t *target) {
    ((sc_mission_t *)level.mission)->rng=2;
    uint32_t id=level.next_mobj_id+1;
    if(!sc_projectile_fire(source,&source->info->attack,target))return NULL;
    return P_MobjById(id);
}
static void flight(mobj_t *shot,int ticks) {
    while(ticks--&&!shot->remove){++leveltime;gameinfo->mobj_ticker(shot);}
}
static int scenario(void) {
    CHECK(sc_projectile_weapon(4)&&sc_projectile_weapon(16)&&sc_projectile_weapon(19));
    CHECK(!sc_projectile_weapon(25)&&sc_projectile_weapon(48));
    CHECK(sc_weapons[4].flingy==145&&sc_weapons[4].speed==8533&&sc_weapons[4].acceleration==667);
    mobj_t *source=spawn(MT_VULTURE,FIXED2_LIT(8,8),0),*target=spawn(MT_ZEALOT,FIXED2_LIT(12,8),1);
    CHECK(source&&target); source->core.angle=0; sc_start(target);
    sc_mission_t *mission=level.mission;
    if(!mission->terrain)mission->terrain=calloc((size_t)level.width*level.height,sizeof(*mission->terrain));
    CHECK(mission->terrain);
    sc_terrain_t *high=&mission->terrain[L_Index(&level,12,8)];
    sc_terrain_t *low=&mission->terrain[L_Index(&level,8,8)];
    uint16_t original_low=low->vf4[0];low->vf4[0]=0;
    uint16_t original=high->vf4[0],cover=high->cv5;
    high->vf4[0]=SC_TERRAIN_HIGH; mission->rng=1; /* next roll 90 */
    CHECK(sc_projectile_fire(source,&source->info->attack,target));
    mobj_t *probe=P_MobjById(level.next_mobj_id);CHECK(probe&&probe->sc.projectile.missed);P_RemoveMobj(probe);
    high->vf4[0]=0;high->cv5=SC_TERRAIN_COVER;mission->rng=1;
    CHECK(sc_projectile_fire(source,&source->info->attack,target));
    probe=P_MobjById(level.next_mobj_id);CHECK(probe&&probe->sc.projectile.missed);P_RemoveMobj(probe);
    high->cv5=0;mission->rng=0; /* zero roll also misses at zero chance */
    CHECK(sc_projectile_fire(source,&source->info->attack,target));
    probe=P_MobjById(level.next_mobj_id);CHECK(probe&&probe->sc.projectile.missed);P_RemoveMobj(probe);
    high->vf4[0]=original;high->cv5=cover;low->vf4[0]=original_low;
    int shields=target->sc.shields;
    mobj_t *shot=fire(source,target); CHECK(shot&&shot->type_id==MT_SC_PROJECTILE);
    CHECK(target->sc.shields==shields&&shot->sc.projectile.speed==0);
    fixed2_t initial=fixed3_xy(shot->core.position),aim=shot->movement.goal;
    while(shot->missile.age<1){++leveltime;gameinfo->mobj_ticker(shot);}
    CHECK(fixed2_equal(initial,fixed3_xy(shot->core.position)));
    while(shot->missile.age<2){++leveltime;gameinfo->mobj_ticker(shot);}
    CHECK(shot->sc.projectile.speed==667 && shot->core.position.x-initial.x==667*8);
    target->core.position=fixed3_from_fixed2(FIXED2_LIT(16,8),0);
    level.upgrades[sc_weapons[4].upgrade][0].weapon=2; /* evaluated at impact */
    flight(shot,RTS_TICRATE*3);
    CHECK(shot->remove && fixed2_equal(shot->movement.goal,aim));
    CHECK(target->sc.shields==shields-(24<<8));
    CHECK(target->attack.target==source); /* Shield-only hits still provoke. */

    target->sc.shields=0; target->hp=10;
    shot=fire(source,target); CHECK(shot);
    uint32_t shot_id=shot->id,target_id=target->id;
    P_RemoveMobj(source); CHECK(!shot->target);
    app_t app={0};AiContext ai={0};hudtext_t hud={0};void *saved=NULL;size_t length=0;
    uint32_t hash=G_Consistency();
    CHECK(G_SaveGameBlob(&saved,&length,"projectile",&app,&ai,&hud));
    P_FreeLevel(&level);P_InitThinkers();CHECK(G_DoLoadLevel(map,&level)&&P_InitSight());
    CHECK(G_LoadGameBlob(saved,length,&app,&ai,&hud));free(saved);
    CHECK(G_Consistency()==hash);
    shot=P_MobjById(shot_id);target=P_MobjById(target_id);
    CHECK(shot&&target&&!shot->target&&shot->attack.target==target);
    flight(shot,RTS_TICRATE*5);
    CHECK(shot->remove&&target->hp<=0&&target->sc.attacker==0);

    source=spawn(MT_VULTURE,FIXED2_LIT(8,8),0);target=spawn(MT_MARINE,FIXED2_LIT(12,8),1);
    mobj_t *cloud=spawn(MT_DARK_SWARM,FIXED2_LIT(12,8),0);
    CHECK(source&&target&&cloud);
    cloud->sc.order.tech=SC_TECH_DARK_SWARM;cloud->sc.timers[SC_TIMER_LIFE]=100;
    int hp=target->hp;
    shot=fire(source,target);CHECK(shot&&shot->sc.projectile.missed);
    cloud->core.position=fixed3_from_fixed2(FIXED2_LIT(20,20),0);
    flight(shot,RTS_TICRATE*5);CHECK(shot->remove&&target->hp==hp);
    target->sc.flags|=SC_INVINCIBLE;
    shot=fire(source,target);CHECK(shot&&!shot->sc.projectile.missed);
    flight(shot,RTS_TICRATE*5);CHECK(shot->remove&&target->hp==hp);

    source=spawn(MT_VULTURE,(fixed2_t){FIXED_FROM_INT(level.width)-SC_PIXELS(21),FIXED_FROM_INT(12)},0);
    target=spawn(MT_MARINE,(fixed2_t){FIXED_FROM_INT(level.width-4),FIXED_FROM_INT(12)},1);
    CHECK(source&&target);source->core.angle=0;
    shot=fire(source,target);CHECK(shot);
    for(int t=0;t<RTS_TICRATE&&!shot->remove&&!shot->missile.wait;t++)flight(shot,1);
    CHECK(!shot->remove&&shot->missile.wait&&shot->core.position.x==FIXED_FROM_INT(level.width)-SC_PIXELS(1));
    int old_age=shot->missile.age;
    while(!shot->remove&&shot->missile.age==old_age)flight(shot,1);
    CHECK(shot->remove); /* bounds expiry enters Death on the next native frame */
    int life=sc_weapons[4].lifetime;sc_weapons[4].lifetime=2;
    source=spawn(MT_VULTURE,FIXED2_LIT(4,4),0);target=spawn(MT_MARINE,FIXED2_LIT(20,4),1);
    CHECK(source&&target);source->core.angle=0;
    shot=fire(source,target);CHECK(shot);
    while(shot->missile.age<3)flight(shot,1);
    fixed2_t before=fixed3_xy(shot->core.position);
    while(!shot->remove)flight(shot,1);
    CHECK(shot->missile.age==4 && shot->core.position.x>before.x);
    sc_weapons[4].lifetime=life;
    P_FreeLevel(&level);
    return 0;
}
static int bounce(void) {
    CHECK(G_DoLoadLevel(map,&level)&&P_InitSight());
    mobj_t *muta=spawn(MT_MUTALISK,FIXED2_LIT(5,5),0);
    mobj_t *a=spawn(MT_MARINE,FIXED2_LIT(7,5),1),*b=spawn(MT_MARINE,FIXED2_LIT(8,5),1),
           *c=spawn(MT_MARINE,FIXED2_LIT(9,5),1);
    CHECK(muta&&a&&b&&c);P_UpdateSight();
    mobj_t *shot=fire(muta,a);CHECK(shot&&!shot->sc.projectile.missed);
    int hp=a->hp;
    for(int t=0;t<RTS_TICRATE*5&&!shot->remove&&shot->missile.phase==0;t++)flight(shot,1);
    CHECK(!shot->remove&&shot->missile.phase==1&&a->hp==hp-9&&b->hp==hp&&c->hp==hp);
    for(int t=0;t<RTS_TICRATE*5&&!shot->remove&&shot->missile.phase==1;t++)flight(shot,1);
    CHECK(!shot->remove&&shot->missile.phase==2&&b->hp==hp-3&&c->hp==hp);
    flight(shot,RTS_TICRATE*5);CHECK(shot->remove&&c->hp==hp-1);
    P_FreeLevel(&level);return 0;
}
static int homing(void) {
    CHECK(G_DoLoadLevel(map,&level)&&P_InitSight());
    mobj_t *source=spawn(MT_GOLIATH,FIXED2_LIT(5,5),0),*target=spawn(MT_BATTLECRUISER,FIXED2_LIT(8,5),1);
    CHECK(source&&target);source->core.angle=0;
    ((sc_mission_t *)level.mission)->rng=2;
    uint32_t first=level.next_mobj_id+1;
    CHECK(sc_projectile_fire(source,&source->info->air_attack,target));
    mobj_t *a=P_MobjById(first),*b=P_MobjById(first+1);
    CHECK(a&&b&&a->core.angle!=b->core.angle&&!a->sc.projectile.missed&&!b->sc.projectile.missed);
    CHECK(sc_weapons[8].behavior==1&&sc_weapons[8].spin==64);
    fixed2_t destination=FIXED2_LIT(9,6);target->core.position=fixed3_from_fixed2(destination,0);
    int hp=target->hp;
    for(int t=0;t<RTS_TICRATE*5&&(!a->remove||!b->remove);t++) {
        ++leveltime;if(!a->remove)gameinfo->mobj_ticker(a);if(!b->remove)gameinfo->mobj_ticker(b);
    }
    CHECK(a->remove&&b->remove&&fixed2_equal(a->movement.goal,destination)&&fixed2_equal(b->movement.goal,destination));
    CHECK(target->hp==hp-2*(10-sc_units[MT_BATTLECRUISER-1].armor));
    P_FreeLevel(&level);return 0;
}
static int bounce_lifetimes(void) {
    CHECK(G_DoLoadLevel(map,&level)&&P_InitSight());
    mobj_t *source=spawn(MT_MUTALISK,FIXED2_LIT(5,5),0),*target=spawn(MT_MARINE,FIXED2_LIT(7,5),1),
           *next=spawn(MT_MARINE,FIXED2_LIT(8,5),1);
    CHECK(source&&target&&next);P_UpdateSight();
    mobj_t *shot=fire(source,target);CHECK(shot&&!shot->sc.projectile.missed);
    P_RemoveMobj(target);CHECK(!shot->attack.target);
    for(int t=0;t<RTS_TICRATE*5&&!shot->remove&&shot->missile.phase==0;t++)flight(shot,1);
    CHECK(!shot->remove&&shot->missile.phase==1&&shot->attack.target==next);
    flight(shot,RTS_TICRATE*5);CHECK(shot->remove&&next->hp<next->max_hp);
    P_FreeLevel(&level);P_InitThinkers();CHECK(G_DoLoadLevel(map,&level)&&P_InitSight());
    source=spawn(MT_MUTALISK,FIXED2_LIT(5,5),0);target=spawn(MT_MARINE,FIXED2_LIT(7,5),1);
    next=spawn(MT_MARINE,FIXED2_LIT(8,5),1);CHECK(source&&target&&next);P_UpdateSight();
    shot=fire(source,target);CHECK(shot&&!shot->sc.projectile.missed);
    P_RemoveMobj(source);CHECK(!shot->target);flight(shot,RTS_TICRATE*5);
    CHECK(shot->remove&&target->hp<target->max_hp&&next->hp==next->max_hp);
    P_FreeLevel(&level);return 0;
}
int main(void) {
    G_InitGame();P_InitThinkers();CHECK(G_DoLoadLevel(map,&level)&&P_InitSight());
    CHECK(!scenario());P_InitThinkers();CHECK(!bounce());P_InitThinkers();CHECK(!homing());
    P_InitThinkers();CHECK(!bounce_lifetimes());
    puts("PASS: native flight delay, acceleration, fixed aim, upgrades, dead shooter and save");
    return 0;
}
