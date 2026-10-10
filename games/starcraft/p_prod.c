#include "sc_local.h"
#include <math.h>
#include <string.h>
/* Stargus command cards, with native DAT costs and build times.
 * Orders, queues, payment, placement and spawning belong to the engine. */
/* Retail tech tree: an add-on prerequisite whose building is the maker must
 * be attached to that maker (a Siege Tank needs its Factory's Machine Shop). */
static const struct { mobjtype_id_t type, maker, prerequisite, also; } recipes[] = {
    {MT_SCV, MT_COMMAND_CENTER, MT_NONE, MT_NONE},
    {MT_MARINE, MT_BARRACKS, MT_NONE, MT_NONE},
    {MT_FIREBAT, MT_BARRACKS, MT_ACADEMY, MT_NONE},
    {MT_GHOST, MT_BARRACKS, MT_ACADEMY, MT_COVERT_OPS},
    {MT_COMMAND_CENTER, MT_SCV, MT_NONE, MT_NONE},
    {MT_SUPPLY_DEPOT, MT_SCV, MT_NONE, MT_NONE},
    {MT_REFINERY, MT_SCV, MT_NONE, MT_NONE},
    {MT_BARRACKS, MT_SCV, MT_COMMAND_CENTER, MT_NONE},
    {MT_ACADEMY, MT_SCV, MT_BARRACKS, MT_NONE},
    {MT_ENGINEERING_BAY, MT_SCV, MT_COMMAND_CENTER, MT_NONE},
    {MT_MISSILE_TURRET, MT_SCV, MT_ENGINEERING_BAY, MT_NONE},
    {MT_BUNKER, MT_SCV, MT_BARRACKS, MT_NONE},
    {MT_FACTORY, MT_SCV, MT_BARRACKS, MT_NONE},
    {MT_STARPORT, MT_SCV, MT_FACTORY, MT_NONE},
    {MT_SCIENCE_FACILITY, MT_SCV, MT_STARPORT, MT_NONE},
    {MT_ARMORY, MT_SCV, MT_FACTORY, MT_NONE},
    {MT_VULTURE, MT_FACTORY, MT_NONE, MT_NONE},
    {MT_SIEGE_TANK, MT_FACTORY, MT_MACHINE_SHOP, MT_NONE},
    {MT_GOLIATH, MT_FACTORY, MT_ARMORY, MT_NONE},
    {MT_WRAITH, MT_STARPORT, MT_NONE, MT_NONE},
    {MT_DROPSHIP, MT_STARPORT, MT_CONTROL_TOWER, MT_NONE},
    {MT_SCIENCE_VESSEL, MT_STARPORT, MT_CONTROL_TOWER, MT_SCIENCE_FACILITY},
    {MT_BATTLECRUISER, MT_STARPORT, MT_CONTROL_TOWER, MT_PHYSICS_LAB},
    /* Add-ons are built by their building, beside it. */
    {MT_COMSAT_STATION, MT_COMMAND_CENTER, MT_ACADEMY, MT_NONE},
    {MT_NUCLEAR_SILO, MT_COMMAND_CENTER, MT_COVERT_OPS, MT_NONE},
    {MT_MACHINE_SHOP, MT_FACTORY, MT_NONE, MT_NONE},
    {MT_CONTROL_TOWER, MT_STARPORT, MT_NONE, MT_NONE},
    {MT_COVERT_OPS, MT_SCIENCE_FACILITY, MT_NONE, MT_NONE},
    {MT_PHYSICS_LAB, MT_SCIENCE_FACILITY, MT_NONE, MT_NONE},
    /* What a hangar holds is made where it is kept. */
    {MT_NUCLEAR_MISSILE, MT_NUCLEAR_SILO, MT_NONE, MT_NONE},
    {MT_INTERCEPTOR, MT_CARRIER, MT_NONE, MT_NONE},
    {MT_SCARAB, MT_REAVER, MT_NONE, MT_NONE},
    {MT_NEXUS, MT_PROBE, MT_NONE, MT_NONE},
    {MT_PYLON, MT_PROBE, MT_NONE, MT_NONE},
    {MT_ASSIMILATOR, MT_PROBE, MT_NONE, MT_NONE},
    {MT_GATEWAY, MT_PROBE, MT_NEXUS, MT_NONE},
    {MT_FORGE, MT_PROBE, MT_NEXUS, MT_NONE},
    {MT_PHOTON_CANNON, MT_PROBE, MT_FORGE, MT_NONE},
    {MT_CYBERNETICS_CORE, MT_PROBE, MT_GATEWAY, MT_NONE},
    {MT_SHIELD_BATTERY, MT_PROBE, MT_GATEWAY, MT_NONE},
    {MT_ROBOTICS_FACILITY, MT_PROBE, MT_CYBERNETICS_CORE, MT_NONE},
    {MT_STARGATE, MT_PROBE, MT_CYBERNETICS_CORE, MT_NONE},
    {MT_CITADEL_OF_ADUN, MT_PROBE, MT_CYBERNETICS_CORE, MT_NONE},
    {MT_ROBOTICS_SUPPORT_BAY, MT_PROBE, MT_ROBOTICS_FACILITY, MT_NONE},
    {MT_FLEET_BEACON, MT_PROBE, MT_STARGATE, MT_NONE},
    {MT_TEMPLAR_ARCHIVES, MT_PROBE, MT_CITADEL_OF_ADUN, MT_NONE},
    {MT_OBSERVATORY, MT_PROBE, MT_ROBOTICS_FACILITY, MT_NONE},
    {MT_ARBITER_TRIBUNAL, MT_PROBE, MT_TEMPLAR_ARCHIVES, MT_NONE},
    {MT_PROBE, MT_NEXUS, MT_NONE, MT_NONE},
    {MT_ZEALOT, MT_GATEWAY, MT_NONE, MT_NONE},
    {MT_DRAGOON, MT_GATEWAY, MT_CYBERNETICS_CORE, MT_NONE},
    {MT_HIGH_TEMPLAR, MT_GATEWAY, MT_TEMPLAR_ARCHIVES, MT_NONE},
    {MT_SHUTTLE, MT_ROBOTICS_FACILITY, MT_NONE, MT_NONE},
    {MT_REAVER, MT_ROBOTICS_FACILITY, MT_ROBOTICS_SUPPORT_BAY, MT_NONE},
    {MT_OBSERVER, MT_ROBOTICS_FACILITY, MT_OBSERVATORY, MT_NONE},
    {MT_SCOUT, MT_STARGATE, MT_NONE, MT_NONE},
    {MT_CARRIER, MT_STARGATE, MT_FLEET_BEACON, MT_NONE},
    {MT_ARBITER, MT_STARGATE, MT_ARBITER_TRIBUNAL, MT_NONE},
    {MT_HATCHERY, MT_DRONE, MT_NONE, MT_NONE},
    {MT_CREEP_COLONY, MT_DRONE, MT_NONE, MT_NONE},
    {MT_EXTRACTOR, MT_DRONE, MT_NONE, MT_NONE},
    {MT_SPAWNING_POOL, MT_DRONE, MT_HATCHERY, MT_NONE},
    {MT_EVOLUTION_CHAMBER, MT_DRONE, MT_HATCHERY, MT_NONE},
    {MT_HYDRALISK_DEN, MT_DRONE, MT_SPAWNING_POOL, MT_NONE},
    {MT_SPIRE, MT_DRONE, MT_LAIR, MT_NONE},
    {MT_QUEENS_NEST, MT_DRONE, MT_LAIR, MT_NONE},
    {MT_NYDUS_CANAL, MT_DRONE, MT_HIVE, MT_NONE},
    {MT_ULTRALISK_CAVERN, MT_DRONE, MT_HIVE, MT_NONE},
    {MT_DEFILER_MOUND, MT_DRONE, MT_HIVE, MT_NONE},
    /* Zerg morphs: a building or unit turns into the product where it is.
     * Larva products are offered on the hatchery too (see init_products). */
    {MT_LAIR, MT_HATCHERY, MT_SPAWNING_POOL, MT_NONE},
    {MT_HIVE, MT_LAIR, MT_QUEENS_NEST, MT_NONE},
    {MT_GREATER_SPIRE, MT_SPIRE, MT_HIVE, MT_NONE},
    {MT_SUNKEN_COLONY, MT_CREEP_COLONY, MT_SPAWNING_POOL, MT_NONE},
    {MT_SPORE_COLONY, MT_CREEP_COLONY, MT_EVOLUTION_CHAMBER, MT_NONE},
    {MT_DRONE, MT_LARVA, MT_NONE, MT_NONE},
    {MT_ZERGLING, MT_LARVA, MT_SPAWNING_POOL, MT_NONE},
    {MT_OVERLORD, MT_LARVA, MT_NONE, MT_NONE},
    {MT_HYDRALISK, MT_LARVA, MT_HYDRALISK_DEN, MT_NONE},
    {MT_MUTALISK, MT_LARVA, MT_SPIRE, MT_NONE},
    {MT_SCOURGE, MT_LARVA, MT_SPIRE, MT_NONE},
    {MT_QUEEN, MT_LARVA, MT_QUEENS_NEST, MT_NONE},
    {MT_ULTRALISK, MT_LARVA, MT_ULTRALISK_CAVERN, MT_NONE},
    {MT_DEFILER, MT_LARVA, MT_DEFILER_MOUND, MT_NONE},
    {MT_GUARDIAN, MT_MUTALISK, MT_GREATER_SPIRE, MT_NONE},
};
/* Researching buildings (upgrades.dat names no maker), Stargus's command
 * cards; Brood War research is left out. Levels 2 and 3 need a later
 * building, as in retail: a Science Facility for Terran, Lair and Hive for
 * Zerg, Templar Archives, Fleet Beacon or a Cybernetics Core for Protoss. */
static const struct { int upgrade; mobjtype_id_t maker, level2, level3; } research[] = {
    {7,MT_ENGINEERING_BAY,MT_ARMORY,MT_ARMORY},{0,MT_ENGINEERING_BAY,MT_ARMORY,MT_ARMORY},
    {8,MT_ARMORY,MT_SCIENCE_FACILITY,MT_SCIENCE_FACILITY},{1,MT_ARMORY,MT_SCIENCE_FACILITY,MT_SCIENCE_FACILITY},
    {9,MT_ARMORY,MT_SCIENCE_FACILITY,MT_SCIENCE_FACILITY},{2,MT_ARMORY,MT_SCIENCE_FACILITY,MT_SCIENCE_FACILITY},
    {16,MT_ACADEMY,MT_NONE,MT_NONE},
    {10,MT_EVOLUTION_CHAMBER,MT_LAIR,MT_HIVE},{11,MT_EVOLUTION_CHAMBER,MT_LAIR,MT_HIVE},{3,MT_EVOLUTION_CHAMBER,MT_LAIR,MT_HIVE},
    {12,MT_SPIRE,MT_LAIR,MT_HIVE},{4,MT_SPIRE,MT_LAIR,MT_HIVE},
    {27,MT_SPAWNING_POOL,MT_NONE,MT_NONE},{29,MT_HYDRALISK_DEN,MT_NONE,MT_NONE},{30,MT_HYDRALISK_DEN,MT_NONE,MT_NONE},
    {13,MT_FORGE,MT_TEMPLAR_ARCHIVES,MT_TEMPLAR_ARCHIVES},{5,MT_FORGE,MT_TEMPLAR_ARCHIVES,MT_TEMPLAR_ARCHIVES},
    {15,MT_FORGE,MT_CYBERNETICS_CORE,MT_CYBERNETICS_CORE},
    {14,MT_CYBERNETICS_CORE,MT_FLEET_BEACON,MT_FLEET_BEACON},{6,MT_CYBERNETICS_CORE,MT_FLEET_BEACON,MT_FLEET_BEACON},
    {33,MT_CYBERNETICS_CORE,MT_NONE,MT_NONE},{34,MT_CITADEL_OF_ADUN,MT_NONE,MT_NONE},
    {17,MT_MACHINE_SHOP,MT_NONE,MT_NONE},{22,MT_CONTROL_TOWER,MT_NONE,MT_NONE},{20,MT_COVERT_OPS,MT_NONE,MT_NONE},{21,MT_COVERT_OPS,MT_NONE,MT_NONE},{23,MT_PHYSICS_LAB,MT_NONE,MT_NONE},
    {19,MT_SCIENCE_FACILITY,MT_NONE,MT_NONE},{31,MT_QUEENS_NEST,MT_NONE,MT_NONE},{32,MT_DEFILER_MOUND,MT_NONE,MT_NONE},{40,MT_TEMPLAR_ARCHIVES,MT_NONE,MT_NONE},
    {35,MT_ROBOTICS_SUPPORT_BAY,MT_NONE,MT_NONE},{36,MT_ROBOTICS_SUPPORT_BAY,MT_NONE,MT_NONE},{43,MT_FLEET_BEACON,MT_NONE,MT_NONE},
};
/* Where techdata.dat abilities are researched. Stim Packs, Spider Mines,
 * Burrowing, Recall and Stasis Field have no effect here and are left out. */
static const struct { int tech; mobjtype_id_t maker; } tech_research[] = {
    {SC_TECH_SIEGE_MODE,MT_MACHINE_SHOP},{SC_TECH_CLOAKING_FIELD,MT_CONTROL_TOWER},
    {SC_TECH_LOCKDOWN,MT_COVERT_OPS},{SC_TECH_PERSONNEL_CLOAKING,MT_COVERT_OPS},{SC_TECH_YAMATO_GUN,MT_PHYSICS_LAB},
    {SC_TECH_EMP,MT_SCIENCE_FACILITY},{SC_TECH_IRRADIATE,MT_SCIENCE_FACILITY},
    {SC_TECH_SPAWN_BROODLING,MT_QUEENS_NEST},{SC_TECH_ENSNARE,MT_QUEENS_NEST},
    {SC_TECH_PLAGUE,MT_DEFILER_MOUND},{SC_TECH_CONSUME,MT_DEFILER_MOUND},
    {SC_TECH_PSIONIC_STORM,MT_TEMPLAR_ARCHIVES},{SC_TECH_HALLUCINATION,MT_TEMPLAR_ARCHIVES},
};
enum { SC_RECIPES = sizeof(recipes)/sizeof(*recipes), SC_RESEARCH = sizeof(research)/sizeof(*research),
       SC_TECH_RESEARCH = sizeof(tech_research)/sizeof(*tech_research) };
/* One product per level: ui id 1000 + upgrade*4 + the level it starts from;
 * a tech is SC_TECH_UI + its techdata row. */
static StaticProductDefinition products[SC_RECIPES+SC_RESEARCH*3+SC_TECH_RESEARCH];
static int product_count;
static bool upgrade_product(const StaticProductDefinition *p,int *upgrade,int *tier) {
    if(!p||p->product_class!=RTS_PRODUCT_UPGRADE)return false;
    *upgrade=(p->product_type-SC_UPGRADE_UI)/4; *tier=(p->product_type-SC_UPGRADE_UI)%4;
    return *upgrade>=0&&*upgrade<SC_UPGRADES;
}
/* The techdata row a research product teaches, or -1. */
static int tech_product(const StaticProductDefinition *p) {
    return p&&p->product_class==RTS_PRODUCT_UPGRADE&&p->product_type>=SC_TECH_UI&&
        p->product_type<SC_TECH_UI+SC_TECHS?p->product_type-SC_TECH_UI:-1;
}
/* level.upgrades[upgrade][owner].weapon holds the level for every upgrades.dat
 * id, so it is saved and hashed with the level; a level load clears it. */
/* The catalog row that raises `upgrade` to `level`: one product per level. */
int sc_upgrade_product(int upgrade,int level) {
    return level>=1&&level<=3&&upgrade>=0&&upgrade<SC_UPGRADES?SC_UPGRADE_UI+upgrade*4+level-1:0;
}
int sc_upgrade_level(int owner,int upgrade) {
    return owner>=0&&owner<8&&upgrade>=0&&upgrade<SC_UPGRADES?level.upgrades[upgrade][owner].weapon:0;
}
static bool products_built;
static void init_products(void) {
    if(products_built)return;
    product_count=0;
    for(unsigned i=0;i<sizeof(recipes)/sizeof(*recipes);i++) {
        mobjtype_id_t type=recipes[i].type,maker=recipes[i].maker; const sc_unit_t *u=&sc_units[type-1];
        StaticProductDefinition *p=&products[product_count++];
        *p=(StaticProductDefinition){.row_id=type,.ui_id=type,.label=u->name,
            .cost=u->minerals,.extra_costs={u->gas},.icon_frame=type-1,
            .product_class=(u->flags&1)?RTS_PRODUCT_BUILDING:RTS_PRODUCT_UNIT,
            .product_type=type,.makers={maker},.maker_count=1,
            .worker_build=(u->flags&1)&&(sc_units[maker-1].flags&8),
            .prerequisites={recipes[i].prerequisite,recipes[i].also},
            .prerequisite_count=(recipes[i].prerequisite!=MT_NONE)+(recipes[i].also!=MT_NONE)};
        /* Selecting a hatchery offers its larvae's card. */
        if(maker==MT_LARVA)
            memcpy(p->makers,(int[]){MT_LARVA,MT_HATCHERY,MT_LAIR,MT_HIVE},sizeof(int[4])),p->maker_count=4;
    }
    for(unsigned i=0;i<SC_RESEARCH;i++) {
        const sc_upgrade_t *u=&sc_upgrades[research[i].upgrade];
        for(int tier=0;tier<u->max_level&&tier<3;tier++) {
            int id=SC_UPGRADE_UI+research[i].upgrade*4+tier;
            mobjtype_id_t later=tier==1?research[i].level2:tier==2?research[i].level3:MT_NONE;
            products[product_count++]=(StaticProductDefinition){.row_id=id,.ui_id=id,.label=u->name,
                .prerequisites={later},.prerequisite_count=later!=MT_NONE,
                .cost=u->minerals+tier*u->mineral_factor,.extra_costs={u->gas+tier*u->gas_factor},
                .icon_frame=u->icon,.product_class=RTS_PRODUCT_UPGRADE,.product_type=id,
                .makers={research[i].maker,research[i].maker==MT_SPIRE?MT_GREATER_SPIRE:MT_NONE},
                .maker_count=research[i].maker==MT_SPIRE?2:1};
        }
    }
    for(unsigned i=0;i<SC_TECH_RESEARCH;i++) {
        const sc_tech_t *t=&sc_techs[tech_research[i].tech]; int id=SC_TECH_UI+tech_research[i].tech;
        products[product_count++]=(StaticProductDefinition){.row_id=id,.ui_id=id,.label=t->name,
            .cost=t->minerals,.extra_costs={t->gas},.icon_frame=t->icon,.product_class=RTS_PRODUCT_UPGRADE,
            .product_type=id,.makers={tech_research[i].maker},.maker_count=1};
    }
    products_built=true;
}
/* The catalog prices come from the unit and upgrade tables: after a map's
 * overrides they are built again. */
void sc_rebuild_products(void) { products_built=false; init_products(); }
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
bool sc_owner_has(int owner,uint16_t type) {
    static const uint16_t higher[]={MT_LAIR,MT_HIVE,MT_GREATER_SPIRE};
    if(G_ModelHasActorType(NULL,owner,type))return true;
    for(unsigned i=0;i<sizeof(higher)/sizeof(*higher);i++)
        if(sc_counts_as(higher[i],type)&&G_ModelHasActorType(NULL,owner,higher[i]))return true;
    return false;
}
/* Only the next level is offered, and only while no building of the owner
 * is already researching it. */
static bool researching(int owner,int product_type) {
    if(!thinkercap.next)return false;
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        const mobj_t *mo=(const mobj_t *)th;
        if(th->function!=P_MobjThinker||mo->owner!=owner||!mo->production||
           mo->production->product_class!=RTS_PRODUCT_UPGRADE||!mo->production->queue_count)continue;
        if(mo->production->product_type==product_type)return true;
    }
    return false;
}
bool sc_upgrade_offered(int owner,const StaticProductDefinition *p) {
    int upgrade,tier,tech=tech_product(p);
    if(tech>=0)return owner>=0&&owner<8&&!level.upgrades[SC_UPGRADES+tech][owner].weapon;
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
    if(p->product_class!=RTS_PRODUCT_UPGRADE&&sc_unit_unavailable(owner,p->product_type))return false;
    for(int i=0;i<p->prerequisite_count;i++)if(!sc_owner_has(owner,p->prerequisites[i]))return false;
    if(!R_RowsMet(owner,p))return false;
    if(p->product_class==RTS_PRODUCT_UPGRADE)
        return sc_upgrade_offered(owner,p)&&!researching(owner,p->product_type)&&sc_owner_has(owner,p->makers[0]);
    return true;
}
/* Research stays in its building: the engine's actor id is the maker's. */
uint16_t G_ModelActorIdForProduct(const StaticProductDefinition *p) {
    if(p&&p->product_class==RTS_PRODUCT_UPGRADE)return (uint16_t)p->makers[0];
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
    if(tech_product(p)>=0)return (sc_techs[tech_product(p)].time*1000+23)/24;
    return p?(sc_units[p->product_type-1].build_time*1000+23)/24:0;
}
/* Finished research raises the owner's level, a Zerg morph turns the egg
 * or building into the product, a hangar keeps what it made and an add-on
 * goes up beside its building; all leave the queue here. An unpowered
 * Protoss building, or an add-on whose place is taken, holds what it
 * finished until it can let it out. */
bool G_ModelStartProductionRelease(RtsGameModel *m,mobj_t *u,const StaticProductDefinition *p,uint16_t id) {
    (void)m;(void)id;
    int upgrade,tier,tech=tech_product(p);
    if(!u||!p)return false;
    bool addon=sc_addon_parent((uint16_t)p->product_type)==u->type_id;
    if(!sc_powered(u)||(addon&&!sc_attach_addon(u,(uint16_t)p->product_type))) {
        if(u->production)u->production->time_left_ms=0;
        return true;
    }
    if(addon) S_Bark(&u,1,SE_READY,false);
    else if(sc_hangar_type(u->type_id)==p->product_type) u->sc.hangar++;
    else if(tech>=0) {
        if(u->owner<8)level.upgrades[SC_UPGRADES+tech][u->owner].weapon=1;
        S_Bark(&u,1,SE_RESEARCH_COMPLETE,false);
    } else if(zerg_morph(p)) {
        fixed2_t at=fixed3_xy(u->core.position);
        if(!P_MorphMobj(u,(uint16_t)p->product_type))return false;
        if(per_egg(p->product_type)>1) {
            mobj_t *twin=sc_spawn_actor((unsigned)p->product_type-1,
                (ivec2_t){(at.x>>(FIXED_FRAC_BITS-5))+12,(at.y>>(FIXED_FRAC_BITS-5))+4},u->owner);
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
bool G_ModelSpecialReleaseSpawnPoint(const RtsGameModel *m,const mobj_t *u,const StaticProductDefinition *p,const mobj_t *n,fixed2_t *out) { (void)m;(void)u;(void)p;(void)n;(void)out; return false; }
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
/* Larvae alone take larva orders, and a morph or an add-on is one order at
 * a time. A building needs its own add-on for what that add-on unlocks, a
 * free place for a new one, and a hangar room for what it keeps. */
bool G_ModelProducerHasTech(const mobj_t *u,const StaticProductDefinition *p) {
    if(!u||!p||(p->makers[0]==MT_LARVA&&u->type_id!=MT_LARVA)||!sc_powered(u))return false;
    int queued=u->production?u->production->queue_count:0;
    bool addon=sc_addon_parent((uint16_t)p->product_type)==u->type_id;
    ivec2_t cell;
    if((zerg_morph(p)||addon)&&queued)return false;
    if(addon&&(sc_addon_of(u)||!sc_addon_site(u,(uint16_t)p->product_type,&cell)))return false;
    if(sc_hangar_type(u->type_id)==p->product_type&&sc_hangar_count(u)+queued>=sc_hangar_capacity(u))return false;
    for(int i=0;i<p->prerequisite_count;i++) {
        const mobj_t *own=sc_addon_of(u);
        if(sc_addon_parent((uint16_t)p->prerequisites[i])==u->type_id&&(!own||own->type_id!=p->prerequisites[i]))
            return false;
    }
    return G_ModelProductAvailable(NULL,u->owner,p)&&sc_supply_ok(u->owner,p,u);
}
int G_ModelRadarLevel(int owner) { (void)owner; return 2; }

/* Supply is stored in halves. 400 halves is the retail 200 cap. A queued unit
 * reserves its cost so a full queue cannot slip past the cap; a silo's nuke
 * takes its supply, a hallucination none. */
void sc_supply_counts(int owner,int *used,int *provided) {
    int have=0,need=0;
    if(thinkercap.next) for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        const mobj_t *mo=(const mobj_t *)th;
        if(th->function!=P_MobjThinker||mo->owner!=owner||mo->remove||mo->hp<=0||
           (mo->sc.flags&SC_HALLUCINATION)) continue;
        if(mo->type_id&&mo->type_id<=SC_TYPES) {
            have+=sc_units[mo->type_id-1].supply_provided;
            need+=sc_units[mo->type_id-1].supply_required;
        }
        if(mo->type_id==MT_NUCLEAR_SILO) need+=sc_units[MT_NUCLEAR_MISSILE-1].supply_required*mo->sc.hangar;
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
/* Brood War adds half a caster's energy to its strength, counts a Bunker as
 * what it holds, and a Carrier or Reaver as full as its hangar. */
static void sc_ai_describe_unit(const mobj_t *unit,AiUnitInfo *info) {
    const sc_unit_t *u=sc_unit(unit);
    if(!u) return;
    if(u->flags&SC_UNIT_SPELLCASTER) {
        int bonus=sc_energy(unit)/2;
        info->ground_strength+=bonus; info->air_strength+=bonus;
    }
    if(u->space_provided&&(u->flags&SC_UNIT_BUILDING)) {
        info->ground_strength=info->air_strength=0;
        for(int i=0;i<4;i++) {
            const mobj_t *in=P_MobjById(unit->sc.cargo[i]);
            if(!in||!(in->sc.flags&SC_LOADED)||in->sc.parent!=unit->id) continue;
            AiUnitInfo held; P_AiUnitInfo(NULL,in->type_id,&held);
            info->ground_strength+=held.ground_strength; info->air_strength+=held.air_strength;
        }
    }
    /* The Carrier's weapon counts its first four interceptors; a Reaver
     * fights while it has a scarab. */
    if(unit->type_id==MT_CARRIER||unit->type_id==MT_REAVER) {
        int full=sc_hangar_count(unit),of=unit->type_id==MT_CARRIER?4:1;
        if(full>of&&unit->type_id==MT_REAVER) full=of;
        info->ground_strength=info->ground_strength*full/of; info->air_strength=info->air_strength*full/of;
    }
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
/* A town is a town hall and the patches around it, out to a geyser some
 * maps set apart from the fields. */
#define SC_TOWN_RADIUS_CELLS 16
#define SC_TOWN_RADIUS FIXED_FROM_INT(SC_TOWN_RADIUS_CELLS)
/* Whether owner has a building of type with add-on attached. */
static bool has_attached(int owner,uint16_t type,uint16_t addon) {
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        const mobj_t *mo=(const mobj_t *)th,*own;
        if(th->function==P_MobjThinker&&!mo->remove&&mo->hp>0&&mo->owner==owner&&mo->type_id==type&&
           (own=sc_addon_of(mo))&&own->type_id==addon) return true;
    }
    return false;
}
/* The add-on a product still waits for, once everything else it needs
 * stands: the computer player builds that add-on first. */
static uint16_t missing_addon(int owner,const StaticProductDefinition *p) {
    for(int i=0;i<p->prerequisite_count;i++)
        if(!sc_addon_parent((uint16_t)p->prerequisites[i])&&!sc_owner_has(owner,(uint16_t)p->prerequisites[i])) return MT_NONE;
    for(int i=0;i<p->prerequisite_count;i++) {
        uint16_t addon=(uint16_t)p->prerequisites[i],parent=sc_addon_parent(addon);
        if(!parent||(parent==p->makers[0]?has_attached(owner,parent,addon):sc_owner_has(owner,addon))) continue;
        return sc_owner_has(owner,parent)?addon:MT_NONE;
    }
    return MT_NONE;
}
/* Puts the missing add-on up on a free building; true while one is coming. */
static bool sc_ai_develop(level_t *map,int owner,int ui) {
    (void)map;
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,ui);
    uint16_t addon=product?missing_addon(owner,product):MT_NONE;
    if(!addon) return false;
    const StaticProductDefinition *build=G_ModelProductByUIId(NULL,addon);
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        mobj_t *mo=(mobj_t *)th;
        if(th->function!=P_MobjThinker||mo->remove||mo->hp<=0||mo->owner!=owner||mo->type_id!=sc_addon_parent(addon))
            continue;
        if(mo->production&&mo->production->product_type==addon) return true;
        if(!sc_addon_of(mo)&&G_QueueProduct(mo,build)) return true;
    }
    return false;
}
/* A building of ours still without its add-on, busy with a queue: an add-on
 * for it is worth waiting for, as for credits, rather than more units. */
static bool addon_waits(int owner,uint16_t addon) {
    uint16_t parent=sc_addon_parent(addon);
    for(thinker_t *th=thinkercap.next;parent&&th!=&thinkercap;th=th->next) {
        const mobj_t *mo=(const mobj_t *)th;
        if(th->function==P_MobjThinker&&mo->owner==owner&&!mo->remove&&mo->hp>0&&mo->type_id==parent&&
           !sc_addon_of(mo)&&mo->production) return true;
    }
    return false;
}
static int sc_ai_can_purchase(const level_t *map,int owner,int ui) {
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,ui);
    if(!product) return AI_BUY_BLOCKED;
    bool available=G_ModelProductAvailable(NULL,owner,product);
    if(!available||!(product->worker_build?G_FindProducer(owner,product):G_FindProducerBelow(owner,product,AI_QUEUE_DEPTH))) {
        if(available&&addon_waits(owner,(uint16_t)product->product_type)) return AI_BUY_NEED_CREDITS;
        return missing_addon(owner,product)?AI_BUY_NEED_TECH:AI_BUY_BLOCKED;
    }
    /* Short of gas: saved for while a refinery of ours draws it. */
    if(product->extra_costs[0]>map->player_resources[owner][1])
        return G_ModelHasActorType(NULL,owner,MT_REFINERY)||G_ModelHasActorType(NULL,owner,MT_EXTRACTOR)||
            G_ModelHasActorType(NULL,owner,MT_ASSIMILATOR)?AI_BUY_NEED_CREDITS:AI_BUY_BLOCKED;
    return map->player_resources[owner][0]<product->cost?AI_BUY_NEED_CREDITS:AI_BUY_OK;
}
/* A worker for a new building, the one nearest our first town hall: not
 * one already on a job, nor one walking somewhere other than the minerals
 * (the scout, a worker sent home). */
static mobj_t *idle_maker(int owner,int maker) {
    mobj_t *best=NULL;
    fixed2_t home={0,0};
    int64_t best_d=0;
    bool found=false;
    if(!thinkercap.next) return NULL;
    for(thinker_t *th=thinkercap.next;th!=&thinkercap&&!found;th=th->next) {
        const mobj_t *mo=(const mobj_t *)th;
        if(th->function==P_MobjThinker&&mo->owner==owner&&!mo->remove&&mo->hp>0&&(mo->traits&MF_RESOURCE_BASE))
            home=fixed3_xy(mo->core.position),found=true;
    }
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        mobj_t *mo=(mobj_t *)th;
        if(th->function!=P_MobjThinker||mo->owner!=owner||mo->remove||mo->hp<=0||mo->type_id!=maker||mo->production||
           (mo->harvest.phase==HARVEST_PHASE_NONE&&P_HasMoveOrder(mo))) continue;
        int64_t d=fixed2_distance_squared64(fixed3_xy(mo->core.position),home);
        if(!best||d<best_d) { best=mo; best_d=d; }
    }
    return best;
}
/* Cells between two footprints along the axis where they are furthest apart. */
static int footprint_gap(ivec2_t a,isize2_t as,ivec2_t b,isize2_t bs) {
    int dx=b.x-(a.x+as.w),dy=b.y-(a.y+as.h);
    if(a.x-(b.x+bs.w)>dx) dx=a.x-(b.x+bs.w);
    if(a.y-(b.y+bs.h)>dy) dy=a.y-(b.y+bs.h);
    return dx>dy?dx:dy;
}
/* Retail keeps a town hall three cells from minerals and geysers; the
 * computer keeps every other building out of that mining lane too. */
static bool clear_of_resources(uint16_t type,ivec2_t cell) {
    isize2_t size=actor_types[type-1].footprint;
    for(int i=0;i<level.resource_vent_count;i++) {
        const resourcevent_t *vent=&level.resource_vents[i];
        if(vent->amount>0&&footprint_gap(cell,size,vent->cell,vent->footprint)<3) return false;
    }
    return true;
}
/* A lane of width cells around a footprint, so builders reach their bays
 * and the army walks out of the base. */
static bool lanes_clear(irect_t foot,int width) {
    for(int y=-width;y<foot.h+width;y++) for(int x=-width;x<foot.w+width;x++) {
        if(x>=0&&y>=0&&x<foot.w&&y<foot.h) continue;
        int cx=foot.x+x,cy=foot.y+y;
        if(!L_Contains(&level,cx,cy)||level.cell_solid[L_Index(&level,cx,cy)]) return false;
    }
    return true;
}
/* One cell around a building; two around the add-on it will get, which
 * goes up later beside it with no lane of its own. */
static bool lane_clear(uint16_t type,ivec2_t cell) {
    uint16_t addon; irect_t place;
    if(sc_addon_place(type,cell,&addon,&place)&&!lanes_clear(place,2)) return false;
    return lanes_clear((irect_t){cell.x,cell.y,actor_types[type-1].footprint.w,actor_types[type-1].footprint.h},1);
}
/* Room for add-ons: a building's own add-on place is free, and nothing
 * covers the place of an add-on another building, standing or about to
 * be built, has still to get. */
static bool addon_room(uint16_t type,ivec2_t cell,const mobj_t *builder) {
    uint16_t addon;
    irect_t place,foot={cell.x,cell.y,actor_types[type-1].footprint.w,actor_types[type-1].footprint.h};
    if(sc_addon_place(type,cell,&addon,&place)&&!P_CanPlaceBuilding(addon,(ivec2_t){place.x,place.y},builder)) return false;
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        const mobj_t *mo=(const mobj_t *)th;
        if(th->function!=P_MobjThinker||mo->owner!=builder->owner||mo->remove||mo->hp<=0||sc_addon_of(mo)) continue;
        irect_t at=P_MobjCells(mo);
        bool planned=mo->production&&mo->production->placed;
        if(sc_addon_place(planned?mo->production->actor_id:mo->type_id,planned?mo->production->cell:(ivec2_t){at.x,at.y},
                          &addon,&place)&&
           place.x<foot.x+foot.w&&foot.x<place.x+place.w&&place.y<foot.y+foot.h&&foot.y<place.y+place.h) return false;
    }
    return true;
}
static bool site_near(uint16_t type,fixed2_t origin,int limit,const mobj_t *builder,ivec2_t *out) {
    for(int radius=2;radius<limit;radius++) for(int y=-radius;y<=radius;y++) for(int x=-radius;x<=radius;x++) {
        if(abs(x)!=radius&&abs(y)!=radius) continue;
        ivec2_t cell={fixed2_cell(origin).x+x,fixed2_cell(origin).y+y};
        /* A refinery sits on its geyser among the fields; anything else keeps its lanes. */
        if(P_CanPlaceBuilding(type,cell,builder)&&(actor_types[type-1].build_on_type||
           (lane_clear(type,cell)&&addon_room(type,cell,builder)&&clear_of_resources(type,cell)))) { *out=cell; return true; }
    }
    return false;
}
/* Rings out from the base; a building that needs creep or psi looks
 * around each hatchery or pylon first, and a refinery takes a geyser by
 * one of our town halls. */
static bool find_site(const mobj_t *builder,uint16_t type,ivec2_t *out) {
    uint32_t flags=sc_units[type-1].flags;
    bool refinery=actor_types[type-1].build_on_type!=0;
    fixed2_t origin={level.width*(FIXED_ONE/2),level.height*(FIXED_ONE/2)};
    bool found=false;
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        const mobj_t *mo=(const mobj_t *)th;
        if(th->function!=P_MobjThinker||mo->owner!=builder->owner||mo->hp<=0||mo->remove) continue;
        fixed2_t at=fixed3_xy(mo->core.position);
        if(!found) { origin=at; found=true; }
        if((((flags&0x20000)&&sc_counts_as(mo->type_id,MT_HATCHERY))||((flags&0x80000)&&mo->type_id==MT_PYLON)||
            (refinery&&(mo->traits&MF_RESOURCE_BASE)))&&site_near(type,at,refinery?SC_TOWN_RADIUS_CELLS:12,builder,out)) return true;
    }
    return !refinery&&site_near(type,origin,48,builder,out);
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
/* Alive and ordered, counting a Lair or Hive as a Hatchery and an egg
 * of zerglings as two; a morphing building counts as what it was.
 * Research counts one once it is done or under way. */
static int sc_ai_owned(int owner,int ui) {
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,ui);
    int upgrade,tier,tech=tech_product(product),count=0;
    if(!product) return 0;
    if(tech>=0) return sc_has_tech(owner,tech)||researching(owner,ui);
    if(upgrade_product(product,&upgrade,&tier)) return sc_upgrade_level(owner,upgrade)>tier||researching(owner,ui);
    uint16_t type=G_ModelActorIdForProduct(product);
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        const mobj_t *mo=(const mobj_t *)th;
        if(th->function!=P_MobjThinker||mo->owner!=owner||mo->remove||mo->hp<=0) continue;
        if(sc_counts_as(mo->type_id,type)) ++count;
        else if(mo->production&&sc_counts_as(mo->production->actor_id,type))
            count+=mo->production->queue_count*per_egg(mo->production->actor_id);
    }
    return count;
}
static bool near_hall(const mobj_t *mo,fixed2_t at) {
    fixed2_t hall;
    if(mo->traits&MF_RESOURCE_BASE) hall=fixed3_xy(mo->core.position);
    else if(mo->production&&mo->production->placed&&mo->production->actor_id>0&&mo->production->actor_id<=SC_TYPES&&
            (sc_units[mo->production->actor_id-1].flags&0x1000))
        hall=P_BuildingPosition(mo->production->actor_id,mo->production->cell);
    else return false;
    return fixed2_distance_squared64(hall,at)<fixed_sq64(SC_TOWN_RADIUS);
}
/* Workers spread over the patches of our towns, nearest first: three to a
 * refinery before anything else, as tech waits on gas, then up to three to
 * a mineral field, the retail saturation. A patch no town hall of ours
 * stands by is left alone: nobody walks across the map to mine. */
static bool sc_ai_assign_harvester(level_t *map,int owner,mobj_t *unit) {
    enum { SATURATION=3 };
    int count=map->resource_vent_count>0?map->resource_vent_count:1,workers[count];
    bool town[count];
    memset(workers,0,sizeof(workers)); memset(town,0,sizeof(town));
    if(thinkercap.next) for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        const mobj_t *mo=(const mobj_t *)th;
        if(th->function!=P_MobjThinker||mo==unit||mo->owner!=owner||mo->remove||mo->hp<=0) continue;
        if(mo->harvest.phase!=HARVEST_PHASE_NONE&&mo->harvest.target>=0&&mo->harvest.target<map->resource_vent_count)
            workers[mo->harvest.target]++;
        if(mo->traits&MF_RESOURCE_BASE)
            for(int i=0;i<map->resource_vent_count;i++) town[i]|=near_hall(mo,map->resource_vents[i].attachment);
    }
    fixed2_t at=fixed3_xy(unit->core.position);
    for(int load=-1;load<SATURATION;load++) {
        int best=-1; int64_t best_d=0;
        for(int i=0;i<map->resource_vent_count;i++) {
            const resourcevent_t *vent=&map->resource_vents[i];
            int has=vent->resource_type?(workers[i]<SATURATION?-1:SATURATION):workers[i];
            if(has!=load||!town[i]||!P_VentOpenTo(map,vent,unit)) continue;
            int64_t d=fixed2_distance_squared64(at,vent->attachment);
            if(best<0||d<best_d) { best=i; best_d=d; }
        }
        if(best>=0&&P_HarvestUnitTo(map,unit,map->resource_vents[best].attachment)) return true;
    }
    return false;
}
/* Each race's town hall, by CHK side. */
static const mobjtype_id_t town_hall[3]={MT_HATCHERY,MT_COMMAND_CENTER,MT_NEXUS};
/* A resource site: the fields and geysers within a few cells of one
 * another, grown from seed; returns their centre. */
static fixed2_t resource_site(const level_t *map,int seed,bool *member) {
    int count=map->resource_vent_count,n=0;
    fixed2_t centre={0,0};
    memset(member,0,(size_t)count*sizeof(*member));
    member[seed]=true;
    for(bool grew=true;grew;) {
        grew=false;
        for(int i=0;i<count;i++) for(int j=0;j<count&&!member[i]&&map->resource_vents[i].amount>0;j++)
            if(member[j]&&fixed2_distance_squared64(map->resource_vents[i].attachment,map->resource_vents[j].attachment)<fixed_sq64(FIXED_FROM_INT(6)))
                member[i]=grew=true;
    }
    for(int i=0;i<count;i++) if(member[i]) { centre=fixed2_add(centre,map->resource_vents[i].attachment); ++n; }
    return (fixed2_t){centre.x/n,centre.y/n};
}
/* The free cell for a town hall nearest a site's patches, keeping the
 * retail three-cell gap to every field and geyser. */
static bool town_site(const level_t *map,uint16_t hall,const bool *member,fixed2_t centre,const mobj_t *builder,ivec2_t *out) {
    isize2_t size=actor_types[hall-1].footprint;
    int64_t best=0; bool found=false;
    for(int y=-10;y<=10;y++) for(int x=-10;x<=10;x++) {
        ivec2_t cell={fixed2_cell(centre).x-size.w/2+x,fixed2_cell(centre).y-size.h/2+y};
        fixed2_t at=P_BuildingPosition(hall,cell);
        int64_t score=0;
        for(int i=0;i<map->resource_vent_count;i++)
            if(member[i]) score+=fixed2_length(fixed2_sub(map->resource_vents[i].attachment,at));
        if((found&&score>=best)||!clear_of_resources(hall,cell)||!P_CanPlaceBuilding(hall,cell,builder)) continue;
        best=score; *out=cell; found=true;
    }
    return found;
}
/* A new town at the free resource site nearest the base that a worker can
 * walk to; one town at a time. */
static int sc_ai_expand(level_t *map,int owner) {
    int side=sc_player_side(owner),count=map->resource_vent_count;
    uint16_t hall=town_hall[side>=0&&side<3?side:1];
    const StaticProductDefinition *product=G_ModelProductByUIId(NULL,hall);
    fixed2_t base={0,0}; bool has_base=false;
    for(thinker_t *th=thinkercap.next;th!=&thinkercap;th=th->next) {
        const mobj_t *mo=(const mobj_t *)th;
        if(th->function!=P_MobjThinker||mo->owner!=owner||mo->remove||mo->hp<=0) continue;
        if(mo->production&&mo->production->placed&&mo->production->actor_id==hall) return AI_BUY_BLOCKED;
        if(!has_base&&(mo->traits&MF_RESOURCE_BASE)) { base=fixed3_xy(mo->core.position); has_base=true; }
    }
    mobj_t *worker=idle_maker(owner,product?product->makers[0]:MT_NONE);
    if(!has_base||!worker||count<=0||!G_ModelProductAvailable(NULL,owner,product)) return AI_BUY_BLOCKED;
    bool tried[count],member[count];
    memset(tried,0,sizeof(tried));
    for(int tries=0;tries<4;) {
        int seed=-1; int64_t seed_d=0;
        for(int i=0;i<count;i++) {
            const resourcevent_t *vent=&map->resource_vents[i];
            int64_t d=fixed2_distance_squared64(vent->attachment,base);
            if(tried[i]||vent->resource_type!=0||vent->amount<=0||(seed>=0&&d>=seed_d)) continue;
            seed=i; seed_d=d;
        }
        if(seed<0) break;
        fixed2_t centre=resource_site(map,seed,member);
        bool claimed=false;
        for(int i=0;i<count;i++) {
            tried[i]|=member[i];
            for(thinker_t *th=thinkercap.next;th!=&thinkercap&&member[i]&&!claimed;th=th->next)
                claimed=th->function==P_MobjThinker&&!((mobj_t *)th)->remove&&((mobj_t *)th)->hp>0&&
                    near_hall((mobj_t *)th,map->resource_vents[i].attachment);
        }
        ivec2_t near,cell;
        if(claimed||!P_NavNearestReachable(map,P_MobjMoveClass(worker),fixed2_cell(fixed3_xy(worker->core.position)),
                                           fixed2_cell(centre),4,&near)) continue;
        ++tries;
        if(map->player_resources[owner][0]<product->cost) return AI_BUY_NEED_CREDITS;
        if(town_site(map,hall,member,centre,worker,&cell)&&G_PlaceProduct(worker,product,cell)) return AI_BUY_OK;
    }
    return AI_BUY_BLOCKED;
}
/* CHK start locations: Start Location rows of the UNIT section. */
static int sc_ai_starts(const level_t *map,fixed2_t *out,int cap) {
    const blob_t *file=map->mission;
    int n=0;
    for(size_t at=0;file&&at+8<=file->size;) {
        const uint8_t *tag=file->bytes+at;
        size_t size=read_u32_le(tag+4);
        if(size>file->size-at-8) break;
        if(!memcmp(tag,"UNIT",4)) for(size_t i=0;i+36<=size&&n<cap;i+=36) {
            const uint8_t *u=tag+8+i;
            if(read_u16_le(u+8)+1==MT_START_LOCATION) out[n++]=(fixed2_t){(int32_t)read_u16_le(u+4)*(FIXED_ONE/32),(int32_t)read_u16_le(u+6)*(FIXED_ONE/32)};
        }
        at+=8+size;
    }
    return n;
}
/* The next step toward ui: ui itself once it can be bought, else (through
 * the same rule) the building or researcher it still lacks; 0 while that
 * is already on its way or nothing is left. */
static int unlock(int owner,int ui,int depth) {
    const StaticProductDefinition *p=G_ModelProductByUIId(NULL,ui);
    if(!p||depth>6) return 0;
    if(G_ModelProductAvailable(NULL,owner,p)&&(p->worker_build||sc_owner_has(owner,(uint16_t)p->makers[0]))) return ui;
    if(sc_ai_owned(owner,ui)) return 0;
    for(int i=0;i<p->prerequisite_count;i++)
        if(!sc_owner_has(owner,(uint16_t)p->prerequisites[i]))
            return sc_ai_owned(owner,p->prerequisites[i])?0:unlock(owner,p->prerequisites[i],depth+1);
    if(!sc_owner_has(owner,(uint16_t)p->makers[0]))
        return sc_ai_owned(owner,p->makers[0])?0:unlock(owner,p->makers[0],depth+1);
    return 0;
}
/* The research that takes a roster unit further, as Brood War's AI keeps
 * up its army: what unlocks the unit, then the next level of its weapons,
 * armor and shields, then the abilities it casts. A Carrier's weapon is
 * its interceptors', a Reaver's its scarabs'. */
static int sc_ai_advance(const level_t *map,int owner,int ui) {
    (void)map;
    const StaticProductDefinition *p=G_ModelProductByUIId(NULL,ui);
    if(!p||p->product_class!=RTS_PRODUCT_UNIT) return 0;
    if(!G_ModelProductAvailable(NULL,owner,p)) return unlock(owner,ui,0);
    uint16_t type=(uint16_t)p->product_type,armed=sc_hangar_type(type)?sc_hangar_type(type):type;
    const sc_unit_t *u=&sc_units[type-1];
    const sc_weapon_t *ground=sc_weapon(sc_units[armed-1].ground_weapon),*air=sc_weapon(sc_units[armed-1].air_weapon);
    int upgrades[4]={ground?ground->upgrade:-1,air?air->upgrade:-1,u->armor_upgrade,u->shields?15:-1};
    for(int i=0;i<4;i++) {
        int level=sc_upgrade_level(owner,upgrades[i]);
        if(upgrades[i]<0||upgrades[i]>=SC_UPGRADES||level>=sc_upgrades[upgrades[i]].max_level||
           !G_ModelProductByUIId(NULL,sc_upgrade_product(upgrades[i],level+1))) continue;
        int step=unlock(owner,sc_upgrade_product(upgrades[i],level+1),0);
        if(step) return step;
    }
    int techs[8],n=sc_unit_techs(type,techs,8);
    for(int i=0;i<n;i++) {
        if(sc_has_tech(owner,techs[i])||!G_ModelProductByUIId(NULL,SC_TECH_UI+techs[i])) continue;
        int step=unlock(owner,SC_TECH_UI+techs[i],0);
        if(step) return step;
    }
    return 0;
}
/* Buildings anchor a base; larvae and eggs do not. */
static bool sc_ai_anchor(const mobj_t *unit) {
    return unit->type_id>0&&unit->type_id<=SC_TYPES&&(sc_units[unit->type_id-1].flags&1)&&G_AiIsStructure(unit);
}
static bool sc_ai_busy(const mobj_t *unit) {
    return unit&&unit->production&&unit->production->placed;
}
static const AiGameInterface sc_ai={
    .name="starcraft",.features=AI_FEATURE_ALL,.player_level=sc_ai_level,
    .owned=sc_ai_owned,.can_purchase=sc_ai_can_purchase,.purchase=sc_ai_purchase,
    .is_anchor=sc_ai_anchor,.is_busy=sc_ai_busy,
    .assign_harvester=sc_ai_assign_harvester,
    .describe_unit=sc_ai_describe_unit,.supply=sc_ai_supply,
    .develop=sc_ai_develop,.tactics=sc_ai_tactics,.advance=sc_ai_advance,.expand=sc_ai_expand,.starts=sc_ai_starts,
};
const AiGameInterface *G_AiInterface(void) { return &sc_ai; }
