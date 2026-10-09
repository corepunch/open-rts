#include "sc_local.h"
#include <string.h>
/* Stargus command cards, with native DAT costs and build times.
 * Orders, queues, payment, placement and spawning belong to the engine. */
static const struct { mobjtype_id_t type, maker, prerequisite; } recipes[] = {
    {MT_SCV, MT_COMMAND_CENTER, MT_NONE},
    {MT_MARINE, MT_BARRACKS, MT_NONE},
    {MT_FIREBAT, MT_BARRACKS, MT_ACADEMY},
    {MT_GHOST, MT_BARRACKS, MT_SCIENCE_FACILITY},
    {MT_COMMAND_CENTER, MT_SCV, MT_NONE},
    {MT_SUPPLY_DEPOT, MT_SCV, MT_NONE},
    {MT_REFINERY, MT_SCV, MT_NONE},
    {MT_BARRACKS, MT_SCV, MT_COMMAND_CENTER},
    {MT_ACADEMY, MT_SCV, MT_BARRACKS},
    {MT_ENGINEERING_BAY, MT_SCV, MT_COMMAND_CENTER},
    {MT_MISSILE_TURRET, MT_SCV, MT_ENGINEERING_BAY},
    {MT_BUNKER, MT_SCV, MT_BARRACKS},
    {MT_FACTORY, MT_SCV, MT_BARRACKS},
    {MT_STARPORT, MT_SCV, MT_FACTORY},
    {MT_SCIENCE_FACILITY, MT_SCV, MT_STARPORT},
    {MT_ARMORY, MT_SCV, MT_FACTORY},
    {MT_VULTURE, MT_FACTORY, MT_NONE},
    {MT_SIEGE_TANK, MT_FACTORY, MT_NONE},
    {MT_GOLIATH, MT_FACTORY, MT_ARMORY},
    {MT_WRAITH, MT_STARPORT, MT_NONE},
    {MT_DROPSHIP, MT_STARPORT, MT_NONE},
    {MT_SCIENCE_VESSEL, MT_STARPORT, MT_SCIENCE_FACILITY},
    {MT_BATTLECRUISER, MT_STARPORT, MT_SCIENCE_FACILITY},
    {MT_NEXUS, MT_PROBE, MT_NONE},
    {MT_PYLON, MT_PROBE, MT_NONE},
    {MT_ASSIMILATOR, MT_PROBE, MT_NONE},
    {MT_GATEWAY, MT_PROBE, MT_NEXUS},
    {MT_FORGE, MT_PROBE, MT_NEXUS},
    {MT_PHOTON_CANNON, MT_PROBE, MT_FORGE},
    {MT_CYBERNETICS_CORE, MT_PROBE, MT_GATEWAY},
    {MT_SHIELD_BATTERY, MT_PROBE, MT_GATEWAY},
    {MT_ROBOTICS_FACILITY, MT_PROBE, MT_CYBERNETICS_CORE},
    {MT_STARGATE, MT_PROBE, MT_CYBERNETICS_CORE},
    {MT_CITADEL_OF_ADUN, MT_PROBE, MT_CYBERNETICS_CORE},
    {MT_ROBOTICS_SUPPORT_BAY, MT_PROBE, MT_ROBOTICS_FACILITY},
    {MT_FLEET_BEACON, MT_PROBE, MT_STARGATE},
    {MT_TEMPLAR_ARCHIVES, MT_PROBE, MT_CITADEL_OF_ADUN},
    {MT_OBSERVATORY, MT_PROBE, MT_ROBOTICS_FACILITY},
    {MT_ARBITER_TRIBUNAL, MT_PROBE, MT_TEMPLAR_ARCHIVES},
    {MT_PROBE, MT_NEXUS, MT_NONE},
    {MT_ZEALOT, MT_GATEWAY, MT_NONE},
    {MT_DRAGOON, MT_GATEWAY, MT_CYBERNETICS_CORE},
    {MT_HIGH_TEMPLAR, MT_GATEWAY, MT_TEMPLAR_ARCHIVES},
    {MT_SHUTTLE, MT_ROBOTICS_FACILITY, MT_NONE},
    {MT_REAVER, MT_ROBOTICS_FACILITY, MT_ROBOTICS_SUPPORT_BAY},
    {MT_OBSERVER, MT_ROBOTICS_FACILITY, MT_OBSERVATORY},
    {MT_SCOUT, MT_STARGATE, MT_NONE},
    {MT_CARRIER, MT_STARGATE, MT_FLEET_BEACON},
    {MT_ARBITER, MT_STARGATE, MT_ARBITER_TRIBUNAL},
    {MT_HATCHERY, MT_DRONE, MT_NONE},
    {MT_CREEP_COLONY, MT_DRONE, MT_NONE},
    {MT_EXTRACTOR, MT_DRONE, MT_NONE},
    {MT_SPAWNING_POOL, MT_DRONE, MT_HATCHERY},
    {MT_EVOLUTION_CHAMBER, MT_DRONE, MT_HATCHERY},
    {MT_HYDRALISK_DEN, MT_DRONE, MT_SPAWNING_POOL},
    {MT_SPIRE, MT_DRONE, MT_LAIR},
    {MT_QUEENS_NEST, MT_DRONE, MT_LAIR},
    {MT_NYDUS_CANAL, MT_DRONE, MT_HIVE},
    {MT_ULTRALISK_CAVERN, MT_DRONE, MT_HIVE},
    {MT_DEFILER_MOUND, MT_DRONE, MT_HIVE},
    /* Zerg morphs: a building or unit turns into the product where it is.
     * Larva products are offered on the hatchery too (see init_products). */
    {MT_LAIR, MT_HATCHERY, MT_SPAWNING_POOL},
    {MT_HIVE, MT_LAIR, MT_QUEENS_NEST},
    {MT_GREATER_SPIRE, MT_SPIRE, MT_HIVE},
    {MT_SUNKEN_COLONY, MT_CREEP_COLONY, MT_SPAWNING_POOL},
    {MT_SPORE_COLONY, MT_CREEP_COLONY, MT_EVOLUTION_CHAMBER},
    {MT_DRONE, MT_LARVA, MT_NONE},
    {MT_ZERGLING, MT_LARVA, MT_SPAWNING_POOL},
    {MT_OVERLORD, MT_LARVA, MT_NONE},
    {MT_HYDRALISK, MT_LARVA, MT_HYDRALISK_DEN},
    {MT_MUTALISK, MT_LARVA, MT_SPIRE},
    {MT_SCOURGE, MT_LARVA, MT_SPIRE},
    {MT_QUEEN, MT_LARVA, MT_QUEENS_NEST},
    {MT_ULTRALISK, MT_LARVA, MT_ULTRALISK_CAVERN},
    {MT_DEFILER, MT_LARVA, MT_DEFILER_MOUND},
    {MT_GUARDIAN, MT_MUTALISK, MT_GREATER_SPIRE},
};
/* Researching buildings (upgrades.dat names no maker). Stargus's command
 * cards; Brood War and add-on research are left out. */
static const struct { int upgrade; mobjtype_id_t maker; } research[] = {
    {7,MT_ENGINEERING_BAY},{0,MT_ENGINEERING_BAY},{8,MT_ARMORY},{1,MT_ARMORY},{9,MT_ARMORY},{2,MT_ARMORY},
    {16,MT_ACADEMY},
    {10,MT_EVOLUTION_CHAMBER},{11,MT_EVOLUTION_CHAMBER},{3,MT_EVOLUTION_CHAMBER},{12,MT_SPIRE},{4,MT_SPIRE},
    {27,MT_SPAWNING_POOL},{29,MT_HYDRALISK_DEN},{30,MT_HYDRALISK_DEN},
    {13,MT_FORGE},{5,MT_FORGE},{15,MT_FORGE},{14,MT_CYBERNETICS_CORE},{6,MT_CYBERNETICS_CORE},
    {33,MT_CYBERNETICS_CORE},{34,MT_CITADEL_OF_ADUN},
};
enum { SC_RECIPES = sizeof(recipes)/sizeof(*recipes), SC_RESEARCH = sizeof(research)/sizeof(*research),
       SC_UPGRADE_UI = 1000 };
/* One product per level: ui id 1000 + upgrade*4 + the level it starts from. */
static StaticProductDefinition products[SC_RECIPES+SC_RESEARCH*3];
static int product_count;
static bool upgrade_product(const StaticProductDefinition *p,int *upgrade,int *tier) {
    if(!p||p->product_class!=RTS_PRODUCT_UPGRADE)return false;
    *upgrade=(p->product_type-SC_UPGRADE_UI)/4; *tier=(p->product_type-SC_UPGRADE_UI)%4;
    return *upgrade>=0&&*upgrade<SC_UPGRADES;
}
/* level.upgrades[upgrade][owner].weapon holds the level for every upgrades.dat
 * id, so it is saved and hashed with the level; a level load clears it. */
int sc_upgrade_level(int owner,int upgrade) {
    return owner>=0&&owner<8&&upgrade>=0&&upgrade<SC_UPGRADES?level.upgrades[upgrade][owner].weapon:0;
}
static int hit_damage(const mobj_t *attacker,const weapondef_t *weapon,mobj_t *target,int damage,int divisor) {
    (void)weapon;
    if(attacker->type_id<1||attacker->type_id>SC_TYPES||target->type_id<1||target->type_id>SC_TYPES)return damage/divisor;
    const sc_unit_t *a=&sc_units[attacker->type_id-1],*t=&sc_units[target->type_id-1];
    damage+=a->damage_bonus*sc_upgrade_level(attacker->owner,a->weapon_upgrade);
    damage=damage/divisor-t->armor-sc_upgrade_level(target->owner,t->armor_upgrade);
    return damage<1?1:damage;
}
void sc_reset_upgrades(void) { game_info.hit_damage=hit_damage; }
static void init_products(void) {
    static bool initialized;
    if(initialized)return;
    product_count=0;
    for(unsigned i=0;i<sizeof(recipes)/sizeof(*recipes);i++) {
        mobjtype_id_t type=recipes[i].type,maker=recipes[i].maker; const sc_unit_t *u=&sc_units[type-1];
        StaticProductDefinition *p=&products[product_count++];
        *p=(StaticProductDefinition){.row_id=type,.ui_id=type,.label=u->name,
            .cost=u->minerals,.extra_costs={u->gas},.icon_frame=type-1,
            .product_class=(u->flags&1)?RTS_PRODUCT_BUILDING:RTS_PRODUCT_UNIT,
            .product_type=type,.makers={maker},.maker_count=1,
            .worker_build=(u->flags&1)&&(sc_units[maker-1].flags&8),
            .prerequisites={recipes[i].prerequisite},.prerequisite_count=recipes[i].prerequisite!=MT_NONE};
        /* Selecting a hatchery offers its larvae's card. */
        if(maker==MT_LARVA)
            memcpy(p->makers,(int[]){MT_LARVA,MT_HATCHERY,MT_LAIR,MT_HIVE},sizeof(int[4])),p->maker_count=4;
    }
    for(unsigned i=0;i<SC_RESEARCH;i++) {
        const sc_upgrade_t *u=&sc_upgrades[research[i].upgrade];
        for(int tier=0;tier<u->max_level&&tier<3;tier++) {
            int id=SC_UPGRADE_UI+research[i].upgrade*4+tier;
            products[product_count++]=(StaticProductDefinition){.row_id=id,.ui_id=id,.label=u->name,
                .cost=u->minerals+tier*u->mineral_factor,.extra_costs={u->gas+tier*u->gas_factor},
                .icon_frame=u->icon,.product_class=RTS_PRODUCT_UPGRADE,.product_type=id,
                .makers={research[i].maker,research[i].maker==MT_SPIRE?MT_GREATER_SPIRE:MT_NONE},
                .maker_count=research[i].maker==MT_SPIRE?2:1};
        }
    }
    initialized=true;
}
/* A Zerg product other than research or a drone's building takes the
 * maker's place: larva and mutalisk through an egg, buildings directly. */
static bool zerg_morph(const StaticProductDefinition *p) {
    return p&&p->product_class!=RTS_PRODUCT_UPGRADE&&!p->worker_build&&p->makers[0]>0&&
        p->makers[0]<=SC_TYPES&&(sc_units[p->makers[0]-1].race&1);
}
/* Zerglings and scourge hatch two to an egg (units.dat flag 0x400). */
static int per_egg(int type) {
    return type>0&&type<=SC_TYPES&&(sc_units[type-1].flags&0x400)?2:1;
}
bool sc_counts_as(uint16_t type,uint16_t as) {
    return type==as||(as==MT_HATCHERY&&(type==MT_LAIR||type==MT_HIVE))||(as==MT_LAIR&&type==MT_HIVE)||
        (as==MT_SPIRE&&type==MT_GREATER_SPIRE);
}
static bool owner_has(int owner,uint16_t type) {
    static const uint16_t higher[]={MT_LAIR,MT_HIVE,MT_GREATER_SPIRE};
    if(G_ModelHasActorType(NULL,owner,type))return true;
    for(unsigned i=0;i<sizeof(higher)/sizeof(*higher);i++)
        if(sc_counts_as(higher[i],type)&&G_ModelHasActorType(NULL,owner,higher[i]))return true;
    return false;
}
/* Only the next level is offered, and only while no building of the owner
 * is already researching that upgrade. */
static bool researching(int owner,int upgrade) {
    if(!thinkercap.next)return false;
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        const mobj_t *mo=(const mobj_t *)th;
        if(th->function!=P_MobjThinker||mo->owner!=owner||!mo->production||
           mo->production->product_class!=RTS_PRODUCT_UPGRADE||!mo->production->queue_count)continue;
        if((mo->production->product_type-SC_UPGRADE_UI)/4==upgrade)return true;
    }
    return false;
}
bool sc_upgrade_offered(int owner,const StaticProductDefinition *p) {
    int upgrade,tier;
    if(!upgrade_product(p,&upgrade,&tier))return true;
    return tier==sc_upgrade_level(owner,upgrade)&&tier<sc_upgrades[upgrade].max_level;
}
int G_ModelGetProducts(const RtsGameModel *m,int owner,StaticProductDefinition *out,int cap) {
    (void)m;(void)owner;init_products();int n=product_count;
    if(!out||cap<1)return 0;if(n>cap)n=cap;memcpy(out,products,(size_t)n*sizeof(*out));return n;
}
const StaticProductDefinition *G_ModelProductByUIId(const RtsGameModel *m,int id) {
    (void)m;init_products();for(int i=0;i<product_count;i++)if(products[i].ui_id==id)return &products[i];return NULL;
}
const StaticProductDefinition *G_ModelProductByClassType(const RtsGameModel *m,int cls,int type) {
    const StaticProductDefinition *p=G_ModelProductByUIId(m,type);return p&&(int)p->product_class==cls?p:NULL;
}
bool G_ModelProductAvailable(const RtsGameModel *m,int owner,const StaticProductDefinition *p) {
    if(!p)return false;
    (void)m;
    for(int i=0;i<p->prerequisite_count;i++)if(!owner_has(owner,p->prerequisites[i]))return false;
    int upgrade,tier;
    if(upgrade_product(p,&upgrade,&tier))
        return sc_upgrade_offered(owner,p)&&!researching(owner,upgrade)&&owner_has(owner,p->makers[0]);
    return true;
}
/* Research stays in its building: the engine's actor id is the maker's. */
uint16_t G_ModelActorIdForProduct(const StaticProductDefinition *p) {
    int upgrade,tier;
    if(upgrade_product(p,&upgrade,&tier))return (uint16_t)p->makers[0];
    return p?p->product_type:0;
}
int G_ModelBuildingFrameForProduct(const StaticProductDefinition *p) { (void)p;return 0; }
int G_ModelBuildingStateForProduct(const gameinfo_t *g,const StaticProductDefinition *p) { return p?g->mobjinfo[p->product_type].spawnstate:0; }
int G_ModelProductTrainingTimeMs(const StaticProductDefinition *p) {
    int upgrade,tier;
    if(upgrade_product(p,&upgrade,&tier)) {
        const sc_upgrade_t *u=&sc_upgrades[upgrade];
        return ((u->time+tier*u->time_factor)*1000+23)/24;
    }
    return p?(sc_units[p->product_type-1].build_time*1000+23)/24:0;
}
/* Finished research raises the owner's level and a Zerg morph turns the
 * egg or building into the product; both leave the queue here. An
 * unpowered Protoss building holds what it finished until power returns. */
bool G_ModelStartProductionRelease(RtsGameModel *m,mobj_t *u,const StaticProductDefinition *p,uint16_t id) {
    (void)m;(void)id;
    int upgrade,tier;
    if(!u)return false;
    if(!sc_powered(u)) { if(u->production)u->production->time_left_ms=0; return true; }
    if(zerg_morph(p)) {
        fvec2_t at=fixed3_xy_to_fvec2(u->core.position);
        if(!P_MorphMobj(u,(uint16_t)p->product_type))return false;
        if(per_egg(p->product_type)>1) {
            mobj_t *twin=sc_spawn_actor((unsigned)p->product_type-1,
                (ivec2_t){(int)(at.x*32)+12,(int)(at.y*32)+4},u->owner);
            if(twin) { twin->team=u->team; twin->allegiance=u->allegiance; twin->hp=twin->max_hp*u->hp/u->max_hp; }
        }
        S_Bark(&u,1,SE_READY,false);
    } else {
        if(!upgrade_product(p,&upgrade,&tier))return false;
        if(u->owner<8&&level.upgrades[upgrade][u->owner].weapon==tier)
            level.upgrades[upgrade][u->owner].weapon=(uint8_t)(tier+1);
        S_Bark(&u,1,SE_RESEARCH_COMPLETE,false);
    }
    if(u->production) {
        if(--u->production->queue_count>0)u->production->time_left_ms=u->production->time_ms;
        else P_FreeMobjProduction(u);
    }
    return true;
}
bool G_ModelSpecialReleaseSpawnPoint(const RtsGameModel *m,const mobj_t *u,const StaticProductDefinition *p,const mobj_t *n,float *x,float *y) { (void)m;(void)u;(void)p;(void)n;(void)x;(void)y; return false; }
void G_ModelBuildUIScript(const RtsGameModel *m,const RtsRenderSnapshot *s,char *out,size_t n) { (void)m;(void)s;if(n)*out=0; }
/* An order on a hatchery goes to the first of its larvae that is free. */
bool G_PlayerBuildProduct(mobj_t *u,const StaticProductDefinition *p) {
    if(u&&p&&p->makers[0]==MT_LARVA&&u->type_id!=MT_LARVA) {
        mobj_t *larva=NULL;
        for(thinker_t *th=thinkercap.next;th!=&thinkercap&&!larva;th=th->next) {
            mobj_t *mo=(mobj_t *)th;
            if(th->function==P_MobjThinker&&!mo->remove&&mo->hp>0&&mo->type_id==MT_LARVA&&
               mo->sc.parent==u->id&&!mo->production)larva=mo;
        }
        u=larva;
    }
    return G_QueueProduct(u,p);
}
/* Larvae alone take larva orders, and a morph is one order at a time. */
bool G_ModelProducerHasTech(const mobj_t *u,const StaticProductDefinition *p) {
    if(!u||!p||(p->makers[0]==MT_LARVA&&u->type_id!=MT_LARVA)||!sc_powered(u))return false;
    if(zerg_morph(p)&&u->production&&u->production->queue_count)return false;
    return G_ModelProductAvailable(NULL,u->owner,p)&&sc_supply_ok(u->owner,p,u);
}
int G_ModelRadarLevel(int owner) { (void)owner; return 2; }

/* Supply is stored in halves. 400 halves is the retail 200 cap. A queued unit
 * reserves its cost so a full queue cannot slip past the cap. */
void sc_supply_counts(int owner,int *used,int *provided) {
    int have=0,need=0;
    if(thinkercap.next) for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        const mobj_t *mo=(const mobj_t *)th;
        if(th->function!=P_MobjThinker||mo->owner!=owner||mo->remove||mo->hp<=0) continue;
        if(mo->type_id&&mo->type_id<=SC_TYPES) {
            have+=sc_units[mo->type_id-1].supply_provided;
            need+=sc_units[mo->type_id-1].supply_required;
        }
        if(mo->production&&mo->production->product_class==RTS_PRODUCT_UNIT&&
           mo->production->product_type>0&&mo->production->product_type<=SC_TYPES)
            need+=sc_units[mo->production->product_type-1].supply_required*mo->production->queue_count*
                per_egg(mo->production->product_type);
    }
    if(have>400) have=400;
    if(used) *used=need;
    if(provided) *provided=have;
}
/* A morph needs only what the product takes beyond its maker. */
bool sc_supply_ok(int owner,const StaticProductDefinition *product,const mobj_t *maker) {
    if(!product||product->product_class!=RTS_PRODUCT_UNIT) return true;
    int type=product->product_type-1;
    if(type<0||type>=SC_TYPES) return true;
    int cost=sc_units[type].supply_required*per_egg(product->product_type);
    if(maker&&zerg_morph(product)&&maker->type_id>0&&maker->type_id<=SC_TYPES)
        cost-=sc_units[maker->type_id-1].supply_required;
    if(cost<=0) return true;
    int used,have; sc_supply_counts(owner,&used,&have);
    return used+cost<=have;
}

static int sc_ai_level(const level_t *map,int owner) {
    (void)map;
    if(owner<0||owner>=8||sc_mission_result()) return AI_LEVEL_NONE;
    if(netgame?D_PlayerIsHuman(owner):owner==consoleplayer) return AI_LEVEL_NONE;
    int kind=sc_owner_kind(owner);
    if(kind==1||kind==5||sc_player_ai(owner)) return AI_LEVEL_NORMAL;
    return AI_LEVEL_NONE;
}
/* A race's computer player, in two parts as Blizzard's melee AI has them:
 * an opening of build/train lines (aiscript TMCu/ZMCu/PMCu) and a doctrine
 * that says, in numbers, what the race is good at. The shared AI turns the
 * doctrine into supply, workers, defenses, an army mix bent toward what it
 * scouts, and the call of when to attack or fall back. */
typedef struct { mobjtype_id_t product; int count; } sc_step_t;
typedef struct {
    const sc_step_t *opening;
    int opening_count;
    int wave_interval_ms, wave_min_size, wave_max_size;
    AiDoctrine doctrine;
} sc_race_ai_t;
#define SC_OPENING(steps) .opening = steps, .opening_count = (int)(sizeof(steps) / sizeof(*steps))

/* Terran: turtles and pushes. Tanks and turrets hold the base, the army
 * leaves only with a clear edge and backs off before it is traded away. */
static const sc_step_t terran_opening[] = {
    {MT_SCV,9},{MT_SUPPLY_DEPOT,1},{MT_BARRACKS,1},{MT_SCV,11},{MT_REFINERY,1},{MT_MARINE,4},
    {MT_BARRACKS,2},{MT_ACADEMY,1},{MT_FACTORY,1},{MT_ENGINEERING_BAY,1},{MT_MARINE,8},
    {MT_ARMORY,1},{MT_FACTORY,2},{MT_STARPORT,1},{MT_SCIENCE_FACILITY,1},
};
/* Zerg: cheap, fast and many. A 9-pool zergling rush, a second hatchery
 * for larvae, waves that trade freely and come back often. */
static const sc_step_t zerg_opening[] = {
    {MT_DRONE,9},{MT_SPAWNING_POOL,1},{MT_ZERGLING,6},{MT_HATCHERY,2},{MT_DRONE,12},
    {MT_EXTRACTOR,1},{MT_HYDRALISK_DEN,1},{MT_DRONE,14},{MT_EVOLUTION_CHAMBER,1},
};
/* Protoss: few, expensive, strong. A gateway army on a teching base,
 * cannons at home, attacks once it out-trades what it has seen. */
static const sc_step_t protoss_opening[] = {
    {MT_PROBE,8},{MT_PYLON,1},{MT_GATEWAY,1},{MT_PROBE,10},{MT_ASSIMILATOR,1},{MT_ZEALOT,2},
    {MT_CYBERNETICS_CORE,1},{MT_GATEWAY,2},{MT_FORGE,1},{MT_DRAGOON,2},{MT_ROBOTICS_FACILITY,1},
    {MT_OBSERVATORY,1},{MT_GATEWAY,3},{MT_STARGATE,1},
};
static const sc_race_ai_t race_ai[3] = {
    [0] = { SC_OPENING(zerg_opening), .wave_interval_ms = 30000, .wave_min_size = 6, .wave_max_size = 32,
        .doctrine = { .workers = 16, .supply_buffer = 4, .counter = 60,
            .attack_ratio = 70, .retreat_ratio = 35,
            .roster = { {MT_DRONE,0},{MT_OVERLORD,0},{MT_ZERGLING,50},{MT_HYDRALISK,35},{MT_MUTALISK,20} },
            .roster_count = 5 } },
    [1] = { SC_OPENING(terran_opening), .wave_interval_ms = 60000, .wave_min_size = 14, .wave_max_size = 30,
        .doctrine = { .workers = 20, .supply_buffer = 6, .defenses = 2, .counter = 70,
            .attack_ratio = 140, .retreat_ratio = 70,
            .roster = { {MT_SCV,0},{MT_SUPPLY_DEPOT,0},{MT_MISSILE_TURRET,0},{MT_SCIENCE_VESSEL,0},
                        {MT_MARINE,40},{MT_FIREBAT,10},{MT_VULTURE,15},{MT_GOLIATH,20},{MT_SIEGE_TANK,25},
                        {MT_WRAITH,5},{MT_BATTLECRUISER,5} },
            .roster_count = 11 } },
    [2] = { SC_OPENING(protoss_opening), .wave_interval_ms = 45000, .wave_min_size = 8, .wave_max_size = 20,
        .doctrine = { .workers = 20, .supply_buffer = 8, .defenses = 1, .counter = 70,
            .attack_ratio = 110, .retreat_ratio = 60,
            .roster = { {MT_PROBE,0},{MT_PYLON,0},{MT_PHOTON_CANNON,0},{MT_OBSERVER,0},
                        {MT_ZEALOT,35},{MT_DRAGOON,45},{MT_SCOUT,10},{MT_ARBITER,5} },
            .roster_count = 8 } },
};
static bool sc_ai_plan(const level_t *map,int owner,int level,AiPlan *out) {
    (void)map;(void)level;
    int side=sc_player_side(owner);
    const sc_race_ai_t *race=&race_ai[side>=0&&side<3?side:1];
    out->wave_interval_ms=race->wave_interval_ms;
    out->wave_min_size=race->wave_min_size;
    out->wave_max_size=race->wave_max_size;
    out->doctrine=race->doctrine;
    for(int i=0;i<race->opening_count;i++) P_AiPlanAdd(out,race->opening[i].product,race->opening[i].count);
    return true;
}
/* What units.dat says beyond the engine's actor: shields, supply, cloaking
 * and casters; Brood War doubles zealots, firebats and mutalisks. */
static void sc_ai_describe(uint16_t type,AiUnitInfo *info) {
    if(type<1||type>SC_TYPES) return;
    const sc_unit_t *u=&sc_units[type-1];
    info->hp+=u->shields;
    if(u->supply_provided>0) info->roles|=AI_ROLE_SUPPLY;
    if(u->flags&(0x200|0x400000)) info->roles|=AI_ROLE_CLOAKED;
    if((u->flags&0x200000)&&!(info->roles&AI_ROLE_FIGHTER)) info->roles|=AI_ROLE_SUPPORT;
}
/* Whole supply, counting what queues and walking workers will add. */
static bool sc_ai_supply(int owner,int *used,int *cap) {
    int need,have; sc_supply_counts(owner,&need,&have);
    if(thinkercap.next) for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        const mobj_t *mo=(const mobj_t *)th;
        if(th->function!=P_MobjThinker||mo->owner!=owner||mo->remove||mo->hp<=0||!mo->production) continue;
        int type=mo->production->actor_id;
        const StaticProductDefinition *p=G_ModelProductByUIId(NULL,mo->production->product_type);
        if(p&&zerg_morph(p)&&p->product_class!=RTS_PRODUCT_UNIT) continue;
        if(type>0&&type<=SC_TYPES) have+=sc_units[type-1].supply_provided*mo->production->queue_count;
    }
    *used=need/2; *cap=(have<400?have:400)/2;
    return have<400;
}
static int sc_ai_can_purchase(const level_t *map,int owner,int ui) {
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,ui);
    if(!product||!G_ModelProductAvailable(NULL,owner,product)||
       !(product->worker_build?G_FindProducer(owner,product):G_FindProducerBelow(owner,product,AI_QUEUE_DEPTH)))
        return AI_BUY_BLOCKED;
    if(product->extra_costs[0]>map->player_resources[owner][1]) return AI_BUY_BLOCKED;
    return map->player_resources[owner][0]<product->cost?AI_BUY_NEED_CREDITS:AI_BUY_OK;
}
static mobj_t *idle_maker(int owner,int maker) {
    if(!thinkercap.next) return NULL;
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        mobj_t *mo=(mobj_t *)th;
        if(th->function!=P_MobjThinker||mo->owner!=owner||mo->remove||mo->hp<=0) continue;
        if(mo->type_id==maker&&!mo->production) return mo;
    }
    return NULL;
}
/* A one-cell lane around the footprint, so builders reach their bays and
 * the army walks out of the base. */
static bool lane_clear(uint16_t type,ivec2_t cell) {
    isize2_t size=actor_types[type-1].footprint;
    for(int y=-1;y<=size.h;y++) for(int x=-1;x<=size.w;x++) {
        if(x>=0&&y>=0&&x<size.w&&y<size.h) continue;
        int cx=cell.x+x,cy=cell.y+y;
        if(!L_Contains(&level,cx,cy)||level.cell_solid[L_Index(&level,cx,cy)]) return false;
    }
    return true;
}
static bool site_near(uint16_t type,fvec2_t origin,int limit,const mobj_t *builder,ivec2_t *out) {
    for(int radius=2;radius<limit;radius++) for(int y=-radius;y<=radius;y++) for(int x=-radius;x<=radius;x++) {
        if(abs(x)!=radius&&abs(y)!=radius) continue;
        ivec2_t cell={(int)origin.x+x,(int)origin.y+y};
        if(P_CanPlaceBuilding(type,cell,builder)&&lane_clear(type,cell)) { *out=cell; return true; }
    }
    return false;
}
/* Rings out from the base; a building that needs creep or psi looks
 * around each hatchery or pylon first. */
static bool find_site(const mobj_t *builder,uint16_t type,ivec2_t *out) {
    uint32_t flags=sc_units[type-1].flags;
    fvec2_t origin={level.width*0.5f,level.height*0.5f};
    bool found=false;
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        const mobj_t *mo=(const mobj_t *)th;
        if(th->function!=P_MobjThinker||mo->owner!=builder->owner||mo->hp<=0||mo->remove) continue;
        fvec2_t at=fixed3_xy_to_fvec2(mo->core.position);
        if(!found) { origin=at; found=true; }
        if((((flags&0x20000)&&sc_counts_as(mo->type_id,MT_HATCHERY))||((flags&0x80000)&&mo->type_id==MT_PYLON))&&
           site_near(type,at,12,builder,out)) return true;
    }
    return site_near(type,origin,48,builder,out);
}
static bool sc_ai_purchase(level_t *map,int owner,int ui) {
    (void)map;
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,ui);
    if(!product) return false;
    if(!product->worker_build) return G_AiCatalogPurchase(map,owner,ui);
    mobj_t *worker=idle_maker(owner,product->makers[0]);
    ivec2_t cell;
    return worker&&find_site(worker,G_ModelActorIdForProduct(product),&cell)&&
        G_PlaceProduct(worker,product,cell);
}
/* Workers spread over the patches, nearest first: three to a mineral
 * field or a refinery, the retail saturation. */
static bool sc_ai_assign_harvester(level_t *map,int owner,mobj_t *unit) {
    enum { SATURATION=3 };
    int workers[map->resource_vent_count>0?map->resource_vent_count:1];
    memset(workers,0,sizeof(workers));
    if(thinkercap.next) for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        const mobj_t *mo=(const mobj_t *)th;
        if(th->function!=P_MobjThinker||mo==unit||mo->owner!=owner||mo->remove||mo->hp<=0) continue;
        if(mo->harvest.phase!=HARVEST_PHASE_NONE&&mo->harvest.target>=0&&mo->harvest.target<map->resource_vent_count)
            workers[mo->harvest.target]++;
    }
    fvec2_t at=fixed3_xy_to_fvec2(unit->core.position);
    for(int load=0;load<SATURATION;load++) {
        int best=-1; float best_d=0;
        for(int i=0;i<map->resource_vent_count;i++) {
            const resourcevent_t *vent=&map->resource_vents[i];
            if(workers[i]!=load||!P_VentOpenTo(map,vent,unit)) continue;
            float d=fvec2_distance_squared(at,vent->attachment);
            if(best<0||d<best_d) { best=i; best_d=d; }
        }
        if(best>=0&&P_HarvestUnitTo(map,unit,map->resource_vents[best].attachment)) return true;
    }
    return false;
}
/* Alive and ordered, counting a Lair or Hive as a Hatchery and an egg
 * of zerglings as two; a morphing building counts as what it was. */
static int sc_ai_owned(int owner,int ui) {
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,ui);
    if(!product) return 0;
    uint16_t type=G_ModelActorIdForProduct(product);
    int count=0;
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        const mobj_t *mo=(const mobj_t *)th;
        if(th->function!=P_MobjThinker||mo->owner!=owner||mo->remove||mo->hp<=0) continue;
        if(sc_counts_as(mo->type_id,type)) ++count;
        else if(mo->production&&sc_counts_as(mo->production->actor_id,type))
            count+=mo->production->queue_count*per_egg(mo->production->actor_id);
    }
    return count;
}
/* Buildings anchor a base; larvae and eggs do not. */
static bool sc_ai_anchor(const mobj_t *unit) {
    return unit->type_id>0&&unit->type_id<=SC_TYPES&&(sc_units[unit->type_id-1].flags&1)&&G_AiIsStructure(unit);
}
static bool sc_ai_busy(const mobj_t *unit) {
    return unit&&unit->production&&unit->production->placed;
}
static const AiGameInterface sc_ai={
    .name="starcraft",.features=AI_FEATURE_ALL,.player_level=sc_ai_level,.plan=sc_ai_plan,
    .owned=sc_ai_owned,.can_purchase=sc_ai_can_purchase,.purchase=sc_ai_purchase,
    .is_anchor=sc_ai_anchor,.is_busy=sc_ai_busy,
    .assign_harvester=sc_ai_assign_harvester,
    .product_actor=G_AiCatalogActor,.describe=sc_ai_describe,.supply=sc_ai_supply,
};
const AiGameInterface *G_AiInterface(void) { return &sc_ai; }
