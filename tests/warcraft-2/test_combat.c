#include "t_local.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#define CHECK(c) RTS_CHECK(c, "Warcraft II combat", #c)

/* An open 24x20 field of plain land, as the harvest tests use. */
static void fixture(void) {
    P_FreeLevel(&level);
    P_InitThinkers();
    G_InitGame();
    consoleplayer = 0;
    level.width = 24;
    level.height = 20;
    int cells = level.width * level.height;
    level.tile_ids = calloc((size_t)cells, sizeof(*level.tile_ids));
    level.blocked = calloc((size_t)cells, 1);
    level.cell_solid = calloc((size_t)cells, 1);
    level.cell_terrain = calloc((size_t)cells, 1);
    level.speeds = calloc(1, sizeof(*level.speeds));
    level.speeds->class_count = 2;
    level.speeds->terrain[1][0] = 100;
}

/* Units stand at a cell centre; structures at the centre of their footprint
 * from a top-left cell, as w2_spawn_units places them. */
static mobj_t *spawn(int type, int x, int y, int owner) {
    isize2_t foot = mobjinfo[type].w2.footprint;
    bool structure = (mobjinfo[type].w2.flags & W2_STRUCTURE) != 0;
    fixed2_t at = structure ? (fixed2_t){FIXED_FROM_INT(x) + foot.w * (FIXED_ONE / 2), FIXED_FROM_INT(y) + foot.h * (FIXED_ONE / 2)} : fixed2_cell_center((ivec2_t){x, y});
    mobj_t *unit = P_SpawnMobj(fixed3_from_fixed2(at, 0), (uint16_t)type);
    assert(unit);
    unit->owner = (uint8_t)owner;
    unit->team = (uint8_t)(owner < 8 ? owner : 8);
    unit->allegiance = owner == 0 ? ALLEGIANCE_PLAYER : owner >= 8 ? ALLEGIANCE_NEUTRAL : ALLEGIANCE_ENEMY;
    if (structure) w2_mark_footprint(x, y, foot);
    return unit;
}

static void tick(int count) { while (count-- > 0) P_Ticker(); }

/* Any actor still in the world, corpses included (P_MobjById skips the dead). */
static mobj_t *find_id(uint32_t id) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *unit = (mobj_t *)th;
        if (th->function == P_MobjThinker && unit->id == id && !unit->remove) return unit;
    }
    return NULL;
}

static int group_of(const mobj_t *unit) { return states[unit->core.state_id].group; }

static bool attack_order(mobj_t *unit, const mobj_t *target) {
    ticcmd_t cmd = {.order = TC_ATTACK, .count = 1, .units = {unit->id}, .target = target->id,
                    .position = target->core.position};
    G_RunTiccmd(unit->owner, &cmd);
    return unit->attack.target == target;
}

static int test_tables(void) {
    fixture();
    const mobjtype_t *footman = &actor_types[MT_FOOTMAN - 1];
    const mobjtype_t *peasant = &actor_types[MT_PEASANT - 1];
    CHECK((footman->traits & (MF_ATTACK | MF_NOAUTOTARGET)) == MF_ATTACK);
    CHECK((peasant->traits & (MF_ATTACK | MF_NOAUTOTARGET)) == (MF_ATTACK | MF_NOAUTOTARGET));
    CHECK(!(actor_types[MT_FARM - 1].traits & MF_ATTACK));
    CHECK(actor_types[MT_HUMAN_GUARD_TOWER - 1].traits & MF_ATTACK);
    CHECK(!(actor_types[MT_HUMAN_TRANSPORT - 1].traits & MF_ATTACK));
    CHECK(footman->attack.range == FIXED_ONE && actor_types[MT_ARCHER - 1].attack.range == 4 * FIXED_ONE);
    CHECK(footman->footprint.w == 1 && actor_types[MT_FARM - 1].footprint.w == 2 &&
          actor_types[MT_TOWN_HALL - 1].footprint.h == 4);
    /* State rows follow the retail GRP layout and the Wargus waits. */
    CHECK(mobjinfo[MT_FOOTMAN].missilestate == W2_ATTACK_STATE(0));
    CHECK(mobjinfo[MT_FOOTMAN].deathstate == W2_DEATH_STATE(0));
    CHECK(states[W2_ATTACK_STATE(0)].frame == 5 && states[W2_ATTACK_STATE(0)].count == 3 &&
          states[W2_ATTACK_STATE(0)].tics == 3 && states[W2_ATTACK_STATE(0)].group == W2_GROUP_ATTACK);
    CHECK(states[W2_ATTACK_STATE(0) + 1].frame == 8 && states[W2_ATTACK_STATE(0) + 1].action == A_W2_Attack);
    CHECK(states[W2_ATTACK_STATE(0) + 2].nextstate == mobjinfo[MT_FOOTMAN].spawnstate);
    CHECK(states[W2_DEATH_STATE(0)].frame == 9 && states[W2_DEATH_STATE(0)].count == 2 &&
          states[W2_DEATH_STATE(0) + 1].frame == 11 && states[W2_DEATH_STATE(0) + 1].tics == 100 &&
          states[W2_DEATH_STATE(0) + 1].nextstate == 0);
    CHECK(states[W2_ATTACK_STATE(8)].frame == 5 && states[W2_ATTACK_STATE(8) + 1].frame == 6 &&
          states[W2_ATTACK_STATE(8) + 2].tics == 44 && states[W2_DEATH_STATE(8)].frame == 7);
    CHECK(states[W2_DEATH_STATE(6) + 2].frame == 12 && states[W2_DEATH_STATE(6) + 2].count == 2);
    CHECK(states[W2_DEATH_STATE(2)].frame == 10 && states[W2_ATTACK_STATE(2) + 2].frame == 9);
    CHECK(mobjinfo[MT_BALLISTA].deathstate == 0 && mobjinfo[MT_BALLISTA].missilestate == W2_ATTACK_STATE(4));
    CHECK(states[W2_DEATH_STATE(58)].action == A_W2_Collapse && mobjinfo[MT_FARM].deathstate == W2_DEATH_STATE(58));
    CHECK(mobjinfo[MT_FARM].missilestate == 0);
    CHECK(states[mobjinfo[MT_FOOTMAN].spawnstate].action == A_Look &&
          !states[mobjinfo[MT_FARM].spawnstate].action);
    return 0;
}

/* Stratagus CalculateDamageStats with research applied. */
static int test_damage_roll(void) {
    fixture();
    mobj_t *footman = spawn(MT_FOOTMAN, 4, 4, 0);
    mobj_t *grunt = spawn(MT_GRUNT, 6, 4, 1);
    mobj_t *archer = spawn(MT_ARCHER, 4, 8, 0);
    CHECK(W2_PiercingDamage(footman) == 3 && W2_Armor(grunt) == 2);
    static const int rolls[5] = {7, 6, 5, 4, 7};
    for (int roll = 0; roll < 5; ++roll) CHECK(W2_AttackDamage(footman, grunt, (uint32_t)roll) == rolls[roll]);
    level.upgrades[MT_FOOTMAN][0].weapon = 1;
    CHECK(W2_PiercingDamage(footman) == 5 && W2_AttackDamage(footman, grunt, 0) == 9 &&
          W2_AttackDamage(footman, grunt, 4) == 5);
    level.upgrades[MT_FOOTMAN][0].weapon = 2;
    CHECK(W2_PiercingDamage(footman) == 7 && W2_AttackDamage(footman, grunt, 0) == 11 &&
          W2_AttackDamage(footman, grunt, 5) == 6);
    level.upgrades[MT_GRUNT][1].armor = 1;
    CHECK(W2_Armor(grunt) == 4 && W2_AttackDamage(footman, grunt, 0) == 9);
    level.upgrades[MT_GRUNT][1].armor = 2;
    CHECK(W2_Armor(grunt) == 6 && W2_AttackDamage(footman, grunt, 0) == 8 && W2_AttackDamage(footman, grunt, 4) == 4);
    level.upgrades[MT_FOOTMAN][0].weapon = 0;
    level.upgrades[MT_GRUNT][1].armor = 0;
    /* Another player's research never leaks over. */
    level.upgrades[MT_FOOTMAN][1].weapon = 2;
    CHECK(W2_PiercingDamage(footman) == 3);
    int base = W2_AttackDamage(archer, grunt, 0);
    level.upgrades[MT_ARCHER][0].weapon = 1;
    CHECK(W2_AttackDamage(archer, grunt, 0) == base + 1);
    /* Armor can only ever leave one point of basic damage. */
    mobj_t *farm = spawn(MT_FARM, 10, 10, 1);
    CHECK(W2_AttackDamage(footman, farm, 0) == 4 && W2_AttackDamage(footman, farm, 2) == 2);
    /* The synced roll replays from its seed. */
    W2_SeedCombat(7);
    uint32_t first[4];
    for (int i = 0; i < 4; ++i) first[i] = W2_SyncRand();
    W2_SeedCombat(7);
    for (int i = 0; i < 4; ++i) CHECK(W2_SyncRand() == first[i]);
    CHECK(first[0] != first[1]);
    return 0;
}

/* Two melee units beside each other on the diagonal find each other, trade
 * blows through the attack rows, and the loser falls, rests, and is removed. */
static int test_melee_and_death(void) {
    fixture();
    mobj_t *footman = spawn(MT_FOOTMAN, 5, 5, 0);
    mobj_t *grunt = spawn(MT_GRUNT, 6, 6, 1);
    uint32_t footman_id = footman->id, grunt_id = grunt->id;
    bool attacked = false;
    int first_hit = 0;
    for (int i = 0; i < 60 && grunt->hp == grunt->max_hp; ++i) {
        tick(1);
        attacked |= group_of(footman) == W2_GROUP_ATTACK;
        first_hit = i + 1;
    }
    CHECK(attacked && grunt->hp < grunt->max_hp);
    CHECK(first_hit >= 10 && first_hit <= 20); /* Four tics of looking, nine of windup. */
    CHECK(grunt->max_hp - grunt->hp >= 4 && grunt->max_hp - grunt->hp <= 7);
    CHECK(footman->attack.target == grunt);
    CHECK(grunt->attack.target == footman); /* Doom's retaliation. */
    mobj_t *dead = NULL, *alive = NULL;
    for (int i = 0; i < 2000 && !dead; ++i) {
        tick(1);
        if (footman->hp <= 0) { dead = footman; alive = grunt; }
        else if (grunt->hp <= 0) { dead = grunt; alive = footman; }
    }
    CHECK(dead && alive);
    uint32_t dead_id = dead == footman ? footman_id : grunt_id;
    CHECK(dead->hp == 0 && !(dead->traits & (MF_SELECTABLE | MF_ATTACK | MF_MOBILE)));
    CHECK(group_of(dead) == W2_GROUP_DEATH);
    CHECK(dead->core.state_id == W2_DEATH_STATE(dead->type_id - 1));
    int falling = 0;
    while (falling < 8 && dead->core.state_id == W2_DEATH_STATE(dead->type_id - 1)) { tick(1); ++falling; }
    CHECK(falling >= 5 && falling <= 7); /* Two frames of three tics each. */
    CHECK(find_id(dead_id) == dead && dead->core.state_id == W2_DEATH_STATE(dead->type_id - 1) + 1);
    CHECK(!P_MobjById(dead_id)); /* No longer a live target. */
    tick(12); /* The winner finishes its attack row and finds nothing else. */
    CHECK(alive->hp > 0 && group_of(alive) == W2_GROUP_STAND);
    CHECK(!alive->attack.target || alive->attack.target == dead);
    int hp = alive->hp;
    tick(78);
    CHECK(find_id(dead_id) == dead && alive->hp == hp); /* Rests on the ground for a hundred tics. */
    tick(20);
    CHECK(!find_id(dead_id));
    CHECK(P_MobjById(dead == footman ? grunt_id : footman_id) == alive && alive->hp > 0);
    CHECK(!alive->attack.target); /* Removal drops the last reference. */
    CHECK(group_of(alive) == W2_GROUP_STAND);
    return 0;
}

/* Range is Warcraft's tile distance: a melee unit reaches neighbours only
 * and an archer four cells, an order walks a unit into range. */
static int test_range_and_orders(void) {
    fixture();
    mobj_t *footman = spawn(MT_FOOTMAN, 5, 5, 0);
    mobj_t *grunt = spawn(MT_GRUNT, 7, 5, 1);
    tick(60);
    CHECK(grunt->hp == grunt->max_hp && footman->hp == footman->max_hp);
    CHECK(!footman->attack.target && group_of(footman) == W2_GROUP_STAND);
    CHECK(!P_InAttackRange(footman, grunt));
    CHECK(attack_order(footman, grunt));
    for (int i = 0; i < 300 && grunt->hp == grunt->max_hp; ++i) tick(1);
    CHECK(grunt->hp < grunt->max_hp);
    ivec2_t a = fixed2_cell(fixed3_xy(footman->core.position));
    ivec2_t b = fixed2_cell(fixed3_xy(grunt->core.position));
    CHECK(abs(a.x - b.x) <= 1 && abs(a.y - b.y) <= 1);

    fixture();
    mobj_t *archer = spawn(MT_ARCHER, 5, 5, 0);
    mobj_t *far = spawn(MT_GRUNT, 10, 5, 1);
    tick(120);
    CHECK(far->hp == far->max_hp && !archer->attack.target);
    far->core.position = fixed3_from_fixed2(FIXED2_LIT(9.5, 1.5), 0);
    for (int i = 0; i < 120 && far->hp == far->max_hp; ++i) tick(1);
    CHECK(far->hp < far->max_hp);
    CHECK(archer->core.position.x == fixed3_from_fixed2(FIXED2_LIT(5.5, 5.5), 0).x); /* Shot from where it stood. */
    return 0;
}

/* A melee unit razes a structure from the cell beside its footprint. The
 * ground clears at once; the rubble (Wargus destroyed-place, two frames of
 * 200 cycles) lies there a while and then vanishes. */
static int test_building_destroyed(void) {
    fixture();
    mobj_t *grunt = spawn(MT_GRUNT, 7, 8, 0);
    mobj_t *farm = spawn(MT_FARM, 8, 8, 1);
    uint32_t farm_id = farm->id;
    farm->hp = 20;
    CHECK(level.cell_solid[L_Index(&level, 8, 8)] && level.blocked[L_Index(&level, 9, 9)]);
    CHECK(P_InAttackRange(grunt, farm));
    for (int i = 0; i < 400 && farm->hp > 0; ++i) tick(1);
    CHECK(farm->hp == 0 && find_id(farm_id) == farm);
    CHECK(farm->core.state_id == W2_DEATH_STATE(MT_FARM - 1) && group_of(farm) == W2_GROUP_DEATH);
    CHECK(farm->core.sprite_id == W2_SPRITE_RUBBLE && farm->core.frame == 0);
    CHECK(!strcmp(farm->core.sprite_name, "destroyed-site"));
    tick(1);
    CHECK(!level.cell_solid[L_Index(&level, 8, 8)] && !level.cell_solid[L_Index(&level, 9, 9)]);
    CHECK(!level.blocked[L_Index(&level, 8, 8)] && !level.blocked[L_Index(&level, 9, 9)]);
    CHECK(!P_MobjById(farm_id) && grunt->hp == grunt->max_hp);
    CHECK(!grunt->attack.target || grunt->attack.target == farm); /* A corpse, not a target. */
    tick(200);
    CHECK(find_id(farm_id) == farm && farm->core.frame == 1); /* The second rubble frame. */
    tick(190);
    CHECK(find_id(farm_id) == farm);
    tick(12);
    CHECK(!find_id(farm_id) && !grunt->attack.target);
    /* A one-cell footprint uses the small sheet; sea structures the water frames. */
    CHECK(states[W2_DEATH_STATE(MT_HUMAN_WALL - 1)].sprite == W2_SPRITE_SMALL_RUBBLE);
    CHECK(states[W2_DEATH_STATE(MT_HUMAN_OIL_PLATFORM - 1)].frame == 2 &&
          states[W2_DEATH_STATE(MT_HUMAN_OIL_PLATFORM - 1) + 1].frame == 3);
    CHECK(states[W2_DEATH_STATE(MT_TOWN_HALL - 1)].tics == 200 && states[W2_DEATH_STATE(MT_TOWN_HALL - 1) + 1].nextstate == 0);
    /* A diagonal neighbour of a four-cell hall reaches it too; two cells off does not. */
    mobj_t *hall = spawn(MT_TOWN_HALL, 12, 12, 1);
    mobj_t *ogre = spawn(MT_OGRE, 11, 11, 0);
    mobj_t *knight = spawn(MT_KNIGHT, 10, 14, 0);
    CHECK(P_InAttackRange(ogre, hall) && !P_InAttackRange(knight, hall));
    return 0;
}

/* Neutral mines and critters are left alone unless ordered; workers never
 * start fights but do what they are told. */
static int test_neutral_and_cowards(void) {
    fixture();
    mobj_t *footman = spawn(MT_FOOTMAN, 11, 12, 0);
    mobj_t *mine = spawn(MT_GOLD_MINE, 12, 12, 15);
    mobj_t *critter = spawn(MT_CRITTER, 11, 13, 15);
    uint32_t critter_id = critter->id;
    tick(100);
    CHECK(mine->hp == mine->max_hp && critter->hp == critter->max_hp && !footman->attack.target);
    CHECK(attack_order(footman, critter));
    /* Five hit points and no death art: one blow, and it is gone at once. */
    for (int i = 0; i < 100 && find_id(critter_id); ++i) tick(1);
    CHECK(!find_id(critter_id) && mine->hp == mine->max_hp);
    CHECK(attack_order(footman, mine));
    for (int i = 0; i < 100 && mine->hp == mine->max_hp; ++i) tick(1);
    CHECK(mine->hp < mine->max_hp);

    fixture();
    mobj_t *peasant = spawn(MT_PEASANT, 7, 8, 0);
    mobj_t *farm = spawn(MT_FARM, 8, 8, 1);
    tick(100);
    CHECK(farm->hp == farm->max_hp && !peasant->attack.target);
    CHECK(attack_order(peasant, farm));
    for (int i = 0; i < 100 && farm->hp == farm->max_hp; ++i) tick(1);
    CHECK(farm->hp < farm->max_hp && group_of(peasant) != W2_GROUP_WORK);
    return 0;
}

/* Losing the producer cancels its queue; a stop order drops the target. */
static int test_orders_and_losses(void) {
    fixture();
    mobj_t *barracks = spawn(MT_ORC_BARRACKS, 10, 4, 1);
    mobj_t *grunt = spawn(MT_GRUNT, 9, 4, 1);
    mobj_t *knight = spawn(MT_KNIGHT, 5, 5, 0);
    level.player_resources[1][0] = 600;
    CHECK(G_PlayerBuildProduct(barracks, G_ModelProductByUIId(NULL, 2)));
    CHECK(barracks->production && barracks->production->queue_count == 1);
    CHECK(attack_order(knight, grunt));
    tick(30);
    CHECK(P_HasMoveOrder(knight) || knight->attack.target == grunt);
    ticcmd_t stop = {.order = TC_STOP, .count = 1, .units = {knight->id}};
    G_RunTiccmd(0, &stop);
    CHECK(!knight->attack.target && !P_HasMoveOrder(knight));
    barracks->hp = 1;
    P_DamageMobj(barracks, knight, 1);
    CHECK(barracks->hp == 0);
    G_ProductionTicker(RTS_TICK_MS);
    CHECK(!barracks->production);
    return 0;
}

int main(void) {
    RTS_RUN(test_tables());
    RTS_RUN(test_damage_roll());
    RTS_RUN(test_melee_and_death());
    RTS_RUN(test_range_and_orders());
    RTS_RUN(test_building_destroyed());
    RTS_RUN(test_neutral_and_cowards());
    RTS_RUN(test_orders_and_losses());
    P_FreeLevel(&level);
    puts("PASS: attack rows, Stratagus damage with research, tile range, death and corpse removal, razing, neutrals and cowards");
    return 0;
}
