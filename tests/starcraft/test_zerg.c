#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c,"StarCraft Zerg and Protoss production",#c)
/* Larvae, eggs, in-place morphs, creep and psi on a bare 64 by 64 map. */
static mobj_t *spawn(int type, fvec2_t at, int owner) {
    mobj_t *u = sc_spawn_actor((unsigned)type - 1, (ivec2_t){(int)(at.x * 32), (int)(at.y * 32)}, (uint8_t)owner);
    if (u) u->allegiance = owner == consoleplayer ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY;
    return u;
}
static void tick(int n) { while (n--) { P_Ticker(); G_ProductionTicker(1.0f / RTS_TICRATE); } }
/* Retail frames at 24 per second, in engine tics of a whole 33 ms each. */
static int frames(int native) { return native * RTS_TICRATE / 24 + native / 40 + 2; }
static void build(mobj_t *u, int product) {
    ticcmd_t cmd = {.order = TC_BUILD, .count = 1, .units = {u->id}, .product = product};
    G_RunTiccmd(u->owner, &cmd);
}
static int count(int owner, int type) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (th->function == P_MobjThinker && !mo->remove && mo->hp > 0 && mo->owner == owner && mo->type_id == type) ++n;
    }
    return n;
}
static mobj_t *find(int owner, int type) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && !mo->remove && mo->hp > 0 && mo->owner == owner && mo->type_id == type) return mo;
    }
    return NULL;
}
static void fund(void) { level.player_resources[0][0] = level.player_resources[0][1] = 5000; }
static bool available(int type) { return G_ModelProductAvailable(NULL, 0, G_ModelProductByUIId(NULL, type)); }

static int larvae_and_eggs(mobj_t *hatchery) {
    tick(1);
    CHECK(count(0, MT_LARVA) == 1);
    tick(frames(342) * 2);
    CHECK(count(0, MT_LARVA) == 3);
    tick(frames(342) * 3);
    CHECK(count(0, MT_LARVA) == 3);
    /* The hatchery's card morphs one of its larvae: two zerglings for one price. */
    CHECK(!available(MT_ZERGLING));
    spawn(MT_SPAWNING_POOL, (fvec2_t){10, 13}, 0);
    CHECK(available(MT_ZERGLING));
    level.player_resources[0][0] = 100;
    build(hatchery, MT_ZERGLING);
    CHECK(level.player_resources[0][0] == 50 && !hatchery->production);
    tick(1);
    mobj_t *egg = find(0, MT_EGG);
    CHECK(egg && egg->hp == 200 && egg->production && count(0, MT_LARVA) == 2);
    int used, have;
    sc_supply_counts(0, &used, &have);
    CHECK(used == 2 && have == 2);
    CHECK(G_AiInterface()->owned(0, MT_ZERGLING) == 2);
    /* One supply is spent: a second pair does not fit until an overlord. */
    level.player_resources[0][0] = 100;
    build(hatchery, MT_ZERGLING);
    CHECK(level.player_resources[0][0] == 100);
    tick(frames(420));
    CHECK(count(0, MT_ZERGLING) == 2 && count(0, MT_EGG) == 0 && egg->type_id == MT_ZERGLING);
    CHECK(egg->traits & MF_MOBILE);
    /* An egg that dies loses its price. */
    spawn(MT_OVERLORD, (fvec2_t){20, 20}, 0);
    level.player_resources[0][0] = 50;
    build(hatchery, MT_DRONE);
    tick(1);
    egg = find(0, MT_EGG);
    CHECK(egg && level.player_resources[0][0] == 0);
    P_DamageMobj(egg, NULL, 1000);
    tick(frames(300));
    CHECK(count(0, MT_DRONE) == 0 && level.player_resources[0][0] == 0);
    /* Used larvae grow back to three. */
    tick(frames(342) * 2);
    CHECK(count(0, MT_LARVA) == 3);
    return 0;
}

static int hatchery_tiers(mobj_t *hatchery) {
    fund();
    CHECK(!available(MT_SPIRE) && !available(MT_HIVE));
    hatchery->hp = hatchery->max_hp / 2;
    build(hatchery, MT_LAIR);
    CHECK(hatchery->production && level.player_resources[0][0] == 5000 - 150);
    tick(frames(1500) / 2);
    /* Still a hatchery while it morphs: larvae and drop-off stay. */
    CHECK(hatchery->type_id == MT_HATCHERY && (hatchery->traits & MF_RESOURCE_BASE) && !available(MT_SPIRE));
    tick(frames(1500) / 2 + 2);
    CHECK(hatchery->type_id == MT_LAIR && hatchery->hp == 900 && !hatchery->production);
    CHECK(count(0, MT_LARVA) == 3 && available(MT_SPIRE) && available(MT_EVOLUTION_CHAMBER));
    CHECK(!available(MT_HIVE));
    spawn(MT_QUEENS_NEST, (fvec2_t){16, 13}, 0);
    CHECK(available(MT_HIVE) && available(MT_QUEEN));
    CHECK(G_AiInterface()->purchase(&level, 0, MT_HIVE) && hatchery->production);
    tick(frames(1800));
    CHECK(hatchery->type_id == MT_HIVE && available(MT_ULTRALISK_CAVERN) && available(MT_SPIRE));
    CHECK(G_AiInterface()->owned(0, MT_HATCHERY) == 1);
    /* Spire, Greater Spire, then a mutalisk cocoons into a guardian. */
    mobj_t *spire = spawn(MT_SPIRE, (fvec2_t){5, 13}, 0);
    CHECK(available(MT_MUTALISK) && available(MT_SCOURGE) && !available(MT_GUARDIAN));
    build(spire, MT_GREATER_SPIRE);
    tick(frames(1200));
    CHECK(spire->type_id == MT_GREATER_SPIRE && available(MT_MUTALISK) && available(MT_GUARDIAN));
    spawn(MT_OVERLORD, (fvec2_t){22, 20}, 0);
    mobj_t *muta = spawn(MT_MUTALISK, (fvec2_t){20, 4}, 0);
    build(muta, MT_GUARDIAN);
    tick(1);
    CHECK(muta->type_id == MT_COCOON && !(muta->traits & MF_MOBILE));
    tick(frames(600));
    CHECK(muta->type_id == MT_GUARDIAN && (muta->traits & MF_MOBILE) && (muta->traits & MF_ATTACK));
    return 0;
}

static int colonies(void) {
    fund();
    mobj_t *colony = spawn(MT_CREEP_COLONY, (fvec2_t){5, 4}, 0);
    mobj_t *other = spawn(MT_CREEP_COLONY, (fvec2_t){3, 8}, 0);
    CHECK(available(MT_SUNKEN_COLONY) && !available(MT_SPORE_COLONY));
    build(colony, MT_SUNKEN_COLONY);
    tick(frames(600));
    CHECK(colony->type_id == MT_SUNKEN_COLONY);
    spawn(MT_EVOLUTION_CHAMBER, (fvec2_t){22, 13}, 0);
    build(other, MT_SPORE_COLONY);
    tick(frames(600));
    CHECK(other->type_id == MT_SPORE_COLONY && (other->traits & MF_DETECTOR));
    AiUnitInfo sunken, spore;
    P_AiUnitInfo(NULL, MT_SUNKEN_COLONY, &sunken);
    P_AiUnitInfo(NULL, MT_SPORE_COLONY, &spore);
    CHECK((sunken.roles & AI_ROLE_DEFENSE) && (sunken.roles & AI_ROLE_HITS_GROUND));
    CHECK((spore.roles & AI_ROLE_DEFENSE) && (spore.roles & AI_ROLE_HITS_AIR) && !(spore.roles & AI_ROLE_HITS_GROUND));
    mobj_t *enemy = spawn(MT_ZERGLING, (fvec2_t){5, 9}, 1);
    int hp = enemy->hp;
    tick(RTS_TICRATE * 2);
    CHECK(enemy->hp < hp || !find(1, MT_ZERGLING));
    return 0;
}

static int creep_and_psi(void) {
    mobj_t *drone = spawn(MT_DRONE, (fvec2_t){30, 30}, 0), *probe = spawn(MT_PROBE, (fvec2_t){50, 50}, 0);
    mobj_t *enemy = spawn(MT_PROBE, (fvec2_t){52, 52}, 1);
    /* Creep: around the hatchery at (10, 8), not out at (40, 20). */
    CHECK(P_CanPlaceBuilding(MT_HYDRALISK_DEN, (ivec2_t){14, 2}, drone));
    CHECK(!P_CanPlaceBuilding(MT_HYDRALISK_DEN, (ivec2_t){40, 20}, drone));
    CHECK(!P_CanPlaceBuilding(MT_CREEP_COLONY, (ivec2_t){40, 20}, drone));
    CHECK(P_CanPlaceBuilding(MT_HATCHERY, (ivec2_t){40, 20}, drone));
    /* Psi: a gateway needs its owner's pylon; a nexus and a pylon do not. */
    CHECK(!P_CanPlaceBuilding(MT_GATEWAY, (ivec2_t){44, 44}, probe));
    CHECK(P_CanPlaceBuilding(MT_NEXUS, (ivec2_t){50, 40}, probe));
    CHECK(P_CanPlaceBuilding(MT_PYLON, (ivec2_t){44, 44}, probe));
    mobj_t *pylon = spawn(MT_PYLON, (fvec2_t){46, 46}, 0);
    CHECK(P_CanPlaceBuilding(MT_GATEWAY, (ivec2_t){48, 44}, probe));
    CHECK(!P_CanPlaceBuilding(MT_GATEWAY, (ivec2_t){48, 44}, enemy));
    CHECK(!P_CanPlaceBuilding(MT_GATEWAY, (ivec2_t){54, 44}, probe));
    CHECK(!P_CanPlaceBuilding(MT_GATEWAY, (ivec2_t){48, 50}, probe));
    /* A gateway that loses its pylon holds what it trained. */
    mobj_t *gateway = spawn(MT_GATEWAY, (fvec2_t){50, 45.5f}, 0);
    fund();
    CHECK(G_QueueProduct(gateway, G_ModelProductByUIId(NULL, MT_ZEALOT)));
    P_DamageMobj(pylon, NULL, 10000);
    tick(frames(600));
    CHECK(count(0, MT_ZEALOT) == 0 && gateway->production);
    CHECK(!G_FindProducer(0, G_ModelProductByUIId(NULL, MT_ZEALOT)));
    spawn(MT_PYLON, (fvec2_t){46, 46}, 0);
    tick(2);
    CHECK(count(0, MT_ZEALOT) == 1);
    return 0;
}

int main(void) {
    G_InitGame();
    P_InitThinkers();
    consoleplayer = 0;
    level.width = level.height = 64;
    level.blocked = calloc(64 * 64, 1);
    level.cell_solid = calloc(64 * 64, 1);
    mobj_t *hatchery = spawn(MT_HATCHERY, (fvec2_t){10, 8}, 0);
    CHECK(hatchery);
    CHECK(larvae_and_eggs(hatchery) == 0);
    CHECK(hatchery_tiers(hatchery) == 0);
    CHECK(colonies() == 0);
    CHECK(creep_and_psi() == 0);
    P_FreeLevel(&level);
    puts("PASS: larvae and eggs, hatchery tiers, colonies, guardians, creep and psi");
    return 0;
}
