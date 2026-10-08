#include "t_local.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#define CHECK(c) RTS_CHECK(c, "Warcraft II gameplay", #c)

static void fixture(void) {
    P_FreeLevel(&level); P_InitThinkers(); G_InitGame();
    consoleplayer = 0; leveltime = 1;
    level.width = level.height = 32;
    level.tile_ids = calloc(1024, sizeof(*level.tile_ids));
    level.blocked = calloc(1024, 1); level.cell_solid = calloc(1024, 1); level.cell_terrain = calloc(1024, 1);
    level.speeds = calloc(1, sizeof(*level.speeds));
    level.speeds->class_count = 5;
    level.speeds->terrain[1][0] = level.speeds->terrain[2][0] = 100;
    for (int r = 0; r < 3; ++r) level.player_resources[0][r] = 10000;
}

static mobj_t *spawn(int type, fvec2_t at, int owner) {
    mobj_t *unit = P_SpawnMobj(fixed3_from_fvec2(at, 0), type);
    assert(unit);
    unit->owner = unit->team = owner;
    unit->allegiance = owner ? ALLEGIANCE_ENEMY : ALLEGIANCE_PLAYER;
    unit->traits |= MF_NOAUTOTARGET;
    return unit;
}

static void tick(int count) { while (count-- > 0) P_Ticker(); }
static mobj_t *effect(int kind) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (th->function == P_MobjThinker && !u->remove && u->type_id == MT_W2_EFFECT && u->w2.fx.kind == kind) return u;
    }
    return NULL;
}
static void command(mobj_t *unit, ticorder_t order, mobj_t *target) {
    ticcmd_t cmd = {.order = order, .count = 1, .units = {unit->id}, .target = target ? target->id : 0};
    G_RunTiccmd(unit->owner, &cmd);
}

static int repair_and_fire(void) {
    fixture();
    mobj_t *worker = spawn(MT_PEASANT, (fvec2_t){5.5f, 5.5f}, 0);
    mobj_t *farm = spawn(MT_FARM, (fvec2_t){7, 5}, 0);
    P_DamageMobj(farm, NULL, 100); /* Exactly 75%: no fire. */
    CHECK(!effect(W2_FX_SMALL_FIRE));
    P_DamageMobj(farm, NULL, 1);
    mobj_t *fire = effect(W2_FX_SMALL_FIRE);
    CHECK(fire && farm->w2.fire == fire->id && fire->core.position.z == FIXED_ONE);
    uint32_t fire_id = fire->id;
    P_DamageMobj(farm, NULL, 99); /* Exactly 50%: still small. */
    tick(12); CHECK(effect(W2_FX_SMALL_FIRE) && !effect(W2_FX_BIG_FIRE));
    P_DamageMobj(farm, NULL, 1);
    tick(12); CHECK(effect(W2_FX_BIG_FIRE) && farm->w2.fire == fire_id);
    command(worker, TC_REPAIR, farm);
    CHECK(worker->w2.repair.target == farm->id);
    int gold = level.player_resources[0][0], wood = level.player_resources[0][1], hp = farm->hp;
    tick(24); CHECK(farm->hp == hp);
    tick(1); CHECK(farm->hp == hp + 4);
    CHECK(level.player_resources[0][0] == gold - 1 && level.player_resources[0][1] == wood - 1);
    tick(1400); CHECK(farm->hp == farm->max_hp && !worker->w2.repair.target && !P_MobjById(fire_id));
    farm->hp -= 3;
    level.player_resources[0][1] = 0;
    command(worker, TC_REPAIR, farm); tick(30);
    CHECK(farm->hp == farm->max_hp - 3 && !worker->w2.repair.target);
    CHECK(level.player_resources[0][0] == gold - 51); /* 201 HP repaired in 51 paid cycles. */
    level.player_resources[0][1] = 5;
    CHECK(W2_RepairOrder(worker, farm)); command(worker, TC_STOP, NULL);
    CHECK(!worker->w2.repair.target);
    mobj_t *enemy = spawn(MT_FARM, (fvec2_t){9, 5}, 1); enemy->hp -= 1;
    CHECK(!W2_RepairOrder(worker, enemy));
    mobj_t *infantry = spawn(MT_FOOTMAN, (fvec2_t){5, 7}, 0); infantry->hp -= 1;
    CHECK(!W2_RepairOrder(worker, infantry));
    CHECK(W2_RepairOrder(worker, farm));
    level.player_resources[0][1] = 500;
    CHECK(W2_ConstructOrder(worker, MT_FARM, (ivec2_t){12, 5}) && !worker->w2.repair.target);
    mobj_t *ship = spawn(MT_HUMAN_TRANSPORT, (fvec2_t){15, 15}, 0);
    CHECK(W2_BoardOrder(worker, ship) && worker->w2.build_phase == W2_BUILD_NONE);
    CHECK(W2_RepairOrder(worker, farm) && !worker->w2.carrier);
    return 0;
}

static int projectiles(void) {
    fixture();
    mobj_t *archer = spawn(MT_ARCHER, (fvec2_t){4.5f, 4.5f}, 0);
    mobj_t *enemy = spawn(MT_GRUNT, (fvec2_t){8.5f, 4.5f}, 1);
    archer->attack.target = enemy;
    int hp = enemy->hp;
    A_W2_Attack(archer);
    CHECK(enemy->hp == hp && effect(W2_FX_ARROW));
    archer->attack.target = NULL;
    tick(3); CHECK(enemy->hp == hp);
    tick(1); CHECK(enemy->hp < hp && !effect(W2_FX_ARROW));
    mobj_t *ballista = spawn(MT_BALLISTA, (fvec2_t){4.5f, 12.5f}, 0);
    mobj_t *farm = spawn(MT_FARM, (fvec2_t){10, 12}, 1);
    ballista->attack.target = farm;
    ballista->w2.buffs[W2_BUFF_BLOODLUST] = 100;
    A_W2_Attack(ballista);
    CHECK(effect(W2_FX_BOLT) && farm->hp == farm->max_hp);
    CHECK(effect(W2_FX_BOLT)->w2.fx.basic == 2 * mobjinfo[MT_BALLISTA].w2.basic_damage);
    P_RemoveMobj(ballista); /* Shooter's removal cannot erase the in-flight shot. */
    tick(30); CHECK(farm->hp < farm->max_hp);
    mobj_t *foot = spawn(MT_FOOTMAN, (fvec2_t){5, 5}, 0);
    CHECK(!w2_fire_projectile(foot, enemy));
    mobj_t *mage = spawn(MT_MAGE, (fvec2_t){4, 8}, 0);
    CHECK(w2_fire_projectile(mage, enemy) && effect(W2_FX_LIGHTNING)); /* Effect zero is a real projectile. */
    mobj_t *flyer = spawn(MT_DRAGON, (fvec2_t){6, 5}, 1);
    CHECK(!P_CanTarget(foot, flyer) && P_CanTarget(archer, flyer));
    mobj_t *sub = spawn(MT_GNOMISH_SUBMARINE, (fvec2_t){15, 15}, 1);
    CHECK(!W2_VisibleTo(sub, 0));
    spawn(MT_FLYING_MACHINE, (fvec2_t){16, 15}, 0);
    CHECK(W2_VisibleTo(sub, 0));
    return 0;
}

static int assist_construction(void) {
    fixture();
    mobj_t *builder = spawn(MT_PEASANT, (fvec2_t){5.5f, 5.5f}, 0);
    CHECK(W2_ConstructOrder(builder, MT_FARM, (ivec2_t){7, 5}));
    for (int i = 0; i < 300 && !builder->w2.site; ++i) tick(1);
    mobj_t *site = P_MobjById(builder->w2.site);
    CHECK(site && site->w2.build_tics == 600);
    mobj_t *helper = spawn(MT_PEASANT, (fvec2_t){6.5f, 6.5f}, 0);
    CHECK(W2_RepairOrder(helper, site));
    int gold = level.player_resources[0][0], wood = level.player_resources[0][1];
    tick(100);
    CHECK(W2_BuildProgress(site) > 25 && W2_BuildProgress(site) < 40);
    tick(225);
    CHECK(!W2_UnderConstruction(site) && site->hp == site->max_hp);
    CHECK(!builder->w2.site && !helper->w2.repair.target && (builder->traits & MF_SELECTABLE));
    CHECK(level.player_resources[0][0] == gold && level.player_resources[0][1] == wood);
    return 0;
}

static int upgrades(void) {
    fixture();
    mobj_t *archer = spawn(MT_ARCHER, (fvec2_t){3, 3}, 0);
    uint32_t id = archer->id;
    W2_ApplyUpgrade(0, W2_UPGRADE_RANGER);
    CHECK(archer->id == id && archer->type_id == MT_RANGER);
    W2_ApplyUpgrade(0, W2_UPGRADE_LONGBOW); W2_ApplyUpgrade(0, W2_UPGRADE_RANGER_SCOUTING);
    W2_ApplyUpgrade(0, W2_UPGRADE_MARKSMANSHIP);
    CHECK(W2_AttackRange(archer) == 5 && W2_SightRange(archer) == 10 && W2_PiercingDamage(archer) == 9);
    mobj_t *next = spawn(MT_ARCHER, (fvec2_t){5, 3}, 0); tick(1);
    CHECK(next->type_id == MT_RANGER);
    mobj_t *ship = spawn(MT_BATTLESHIP, (fvec2_t){10, 10}, 0);
    int damage = W2_PiercingDamage(ship), armor = W2_Armor(ship);
    W2_ApplyUpgrade(0, W2_UPGRADE_HUMAN_CANNON1); W2_ApplyUpgrade(0, W2_UPGRADE_HUMAN_CANNON2);
    W2_ApplyUpgrade(0, W2_UPGRADE_HUMAN_SHIP_ARMOR1);
    CHECK(W2_PiercingDamage(ship) == damage + 10 && W2_Armor(ship) == armor + 5);
    mobj_t *hero = spawn(MT_TURALYON, (fvec2_t){20, 20}, 0);
    damage = W2_PiercingDamage(hero);
    W2_ApplyUpgrade(0, W2_UPGRADE_SWORD1);
    CHECK(W2_PiercingDamage(hero) == damage + 2);
    mobj_t *smith = spawn(MT_HUMAN_BLACKSMITH, (fvec2_t){20, 4}, 0);
    const StaticProductDefinition *upgrade = G_ModelProductByClassType(NULL, RTS_PRODUCT_UPGRADE, W2_UPGRADE_SWORD2);
    int gold = level.player_resources[0][0];
    CHECK(G_PlayerBuildProduct(smith, upgrade) && !G_ModelProductAvailable(NULL, 0, upgrade));
    command(smith, TC_CANCEL_PRODUCTION, NULL);
    CHECK(!smith->production && level.player_resources[0][0] == gold && G_ModelProductAvailable(NULL, 0, upgrade));
    uint32_t checksum = G_Consistency(); W2_SyncRand(); CHECK(G_Consistency() != checksum);
    return 0;
}

static int cast(mobj_t *unit, int spell, mobj_t *target) {
    ticcmd_t cmd = {.order = TC_SPELL, .count = 1, .units = {unit->id}, .product = spell,
        .target = target ? target->id : 0, .position = target ? target->core.position : unit->core.position};
    G_RunTiccmd(unit->owner, &cmd);
    CHECK(unit->w2.cast.spell == spell);
    tick(15);
    return 0;
}

static int spells(void) {
    fixture();
    mobj_t *paladin = spawn(MT_PALADIN, (fvec2_t){4, 4}, 0);
    mobj_t *friend = spawn(MT_FOOTMAN, (fvec2_t){6, 4}, 0);
    friend->hp -= 20;
    CHECK(!W2_CanCast(paladin, W2_SPELL_HEAL));
    W2_ApplyUpgrade(0, W2_UPGRADE_HEALING);
    paladin->w2.mana = 120;
    RTS_RUN(cast(paladin, W2_SPELL_HEAL, friend));
    CHECK(friend->hp == friend->max_hp && paladin->w2.mana == 0);
    mobj_t *mage = spawn(MT_MAGE, (fvec2_t){4, 8}, 0);
    mobj_t *orc = spawn(MT_GRUNT, (fvec2_t){6, 8}, 1);
    W2_ApplyUpgrade(0, W2_UPGRADE_SLOW); mage->w2.mana = 255;
    RTS_RUN(cast(mage, W2_SPELL_SLOW, orc));
    CHECK(orc->w2.buffs[W2_BUFF_SLOW] > 980 && orc->speed == orc->info->speed * 0.5f);
    CHECK(!W2_CastOrder(mage, W2_SPELL_SLOW, orc, orc->core.position));
    W2_ApplyUpgrade(0, W2_UPGRADE_POLYMORPH); mage->w2.mana = 255;
    RTS_RUN(cast(mage, W2_SPELL_POLYMORPH, orc));
    CHECK(orc->type_id == MT_CRITTER && orc->owner == 15);
    mobj_t *ogre = spawn(MT_OGRE_MAGE, (fvec2_t){4.5f, 12.5f}, 0);
    friend->core.position = fixed3_from_fvec2((fvec2_t){6.5f, 12.5f}, 0);
    W2_ApplyUpgrade(0, W2_UPGRADE_BLOODLUST); ogre->w2.mana = 255;
    RTS_RUN(cast(ogre, W2_SPELL_BLOODLUST, friend));
    CHECK(friend->w2.buffs[W2_BUFF_BLOODLUST] > 980);
    mobj_t *enemy = spawn(MT_GRUNT, (fvec2_t){6.5f, 13.5f}, 1);
    CHECK(W2_AttackDamage(friend, enemy, 0) == 16);
    W2_ApplyUpgrade(0, W2_UPGRADE_RUNES); ogre->w2.mana = 255;
    mobj_t *point = w2_spawn_effect(ogre, W2_FX_SPELL, fixed3_from_fvec2((fvec2_t){6, 13}, 0));
    CHECK(point && W2_Distance(point, enemy) == 0);
    CHECK(P_MobjCells(point).x == 6 && P_MobjCells(point).y == 13);
    P_RemoveMobj(point);
    RTS_RUN(cast(ogre, W2_SPELL_RUNES, enemy));
    CHECK(enemy->hp <= 10);
    mobj_t *dk = spawn(MT_DEATH_KNIGHT, (fvec2_t){4, 18}, 0);
    W2_ApplyUpgrade(0, W2_UPGRADE_UNHOLY_ARMOR); dk->w2.mana = 255;
    friend->core.position = fixed3_from_fvec2((fvec2_t){6, 18}, 0);
    friend->hp = friend->max_hp;
    RTS_RUN(cast(dk, W2_SPELL_UNHOLY_ARMOR, friend));
    CHECK(friend->hp == 30 && friend->w2.buffs[W2_BUFF_ARMOR] > 480);
    P_DamageMobj(friend, dk, 100); CHECK(friend->hp == 30);
    tick(501); P_DamageMobj(friend, dk, 1); CHECK(friend->hp == 29);
    return 0;
}

static int native_effects(void) {
    fixture();
    w2_archive_t arc; w2_blob_t blob = {0}; uint32_t palette[256];
    CHECK(w2_archive_open(&arc, "data/WAR2/DATA/MAINDAT.WAR"));
    CHECK(w2_archive_extract(&arc, w2_era_palette(0), &blob) && w2_decode_palette(&blob, palette));
    w2_blob_free(&blob);
    for (int i = 0; i < W2_FX_COUNT; ++i) {
        spritesheet_t sprite = {0};
        CHECK(w2_archive_extract(&arc, 324 + i, &blob));
        CHECK(w2_decode_grp(&blob, palette, &sprite, w2_effects[i].directional, NULL));
        CHECK(sprite.spritedef.numframes == w2_effects[i].frames);
        R_FreeSprite(&sprite); w2_blob_free(&blob);
    }
    w2_archive_close(&arc);
    return 0;
}

static int naval(void) {
    fixture();
    level.speeds->terrain[2][0] = 0;
    level.speeds->terrain[2][1] = 100;
    level.speeds->terrain[4][1] = 100;
    for (int y = 0; y < 32; ++y) for (int x = 10; x < 32; ++x) {
        level.cell_terrain[L_Index(&level, x, y)] = 1;
        level.blocked[L_Index(&level, x, y)] = 1;
    }
    mobj_t *ship = spawn(MT_HUMAN_TRANSPORT, (fvec2_t){11, 6}, 0);
    mobj_t *passengers[7];
    for (int i = 0; i < 7; ++i) {
        passengers[i] = spawn(MT_FOOTMAN, (fvec2_t){9.5f, 5.5f}, 0);
        CHECK(W2_BoardOrder(passengers[i], ship) == (i < 6));
        tick(1);
    }
    CHECK(passengers[0]->w2.boarded && (passengers[0]->traits & MF_NOBLOCKMAP));
    CHECK(!W2_BoardOrder(ship, ship));
    P_RemoveMobj(passengers[6]);
    CHECK(W2_UnloadOrder(ship, fixed3_xy_to_fvec2(ship->core.position)));
    tick(2);
    int unloaded = 0;
    for (int i = 0; i < 6; ++i) unloaded += !passengers[i]->w2.boarded;
    CHECK(unloaded > 0);
    mobj_t *inside = NULL;
    for (int i = 0; i < 6; ++i) {
        if (!passengers[i]->w2.boarded) P_RemoveMobj(passengers[i]);
        else inside = passengers[i];
    }
    if (!inside) {
        inside = spawn(MT_FOOTMAN, (fvec2_t){9.5f, 5.5f}, 0);
        CHECK(W2_BoardOrder(inside, ship)); tick(1);
    }
    uint32_t inside_id = inside->id;
    P_DamageMobj(ship, NULL, ship->hp); tick(1);
    CHECK(!P_MobjById(inside_id));

    fixture();
    memset(level.cell_terrain, 1, 1024); memset(level.blocked, 1, 1024);
    level.speeds->terrain[2][1] = 100;
    mobj_t *tanker = spawn(MT_HUMAN_OIL_TANKER, (fvec2_t){10, 6}, 0);
    mobj_t *patch = spawn(MT_OIL_PATCH, (fvec2_t){14.5f, 5.5f}, 15);
    patch->allegiance = ALLEGIANCE_NEUTRAL;
    level.resource_vents = calloc(1, sizeof(*level.resource_vents));
    level.resource_vent_count = 1;
    level.resource_vents[0] = (resourcevent_t){.cell = {13, 4}, .footprint = {3, 3},
        .attachment = {14.5f, 5.5f}, .source_id = patch->id, .resource_type = 2, .amount = 250, .active = true};
    w2_mark_footprint(13, 4, (isize2_t){3, 3});
    CHECK(!W2_HarvestOrder(tanker, (fvec2_t){14, 5}));
    CHECK(W2_ConstructOrder(tanker, MT_HUMAN_OIL_PLATFORM, (ivec2_t){13, 4}));
    tick(6500);
    mobj_t *platform = P_MobjById(level.resource_vents[0].source_id);
    CHECK(platform && platform->type_id == MT_HUMAN_OIL_PLATFORM && !W2_UnderConstruction(platform));
    mobj_t *yard = spawn(MT_HUMAN_SHIPYARD, (fvec2_t){6.5f, 12.5f}, 0);
    w2_mark_footprint(5, 11, mobjinfo[yard->type_id].w2.footprint);
    level.player_resources[0][2] = 0;
    CHECK(W2_HarvestOrder(tanker, (fvec2_t){14, 5}));
    for (int i = 0; i < 3000 && level.player_resources[0][2] == 0; ++i) tick(1);
    CHECK(level.player_resources[0][2] == 100 && level.resource_vents[0].amount == 150);
    P_DamageMobj(platform, NULL, platform->hp);
    patch = P_MobjById(level.resource_vents[0].source_id);
    CHECK(patch && patch->type_id == MT_OIL_PATCH && level.resource_vents[0].amount == 150);
    CHECK(W2_ConstructOrder(tanker, MT_HUMAN_OIL_PLATFORM, (ivec2_t){13, 4}));
    for (int i = 0; i < 1000 && !tanker->w2.site; ++i) tick(1);
    platform = P_MobjById(tanker->w2.site);
    CHECK(platform && W2_CancelConstruction(platform));
    CHECK(P_MobjById(level.resource_vents[0].source_id)->type_id == MT_OIL_PATCH);
    CHECK(level.resource_vents[0].amount == 150 && (tanker->traits & MF_SELECTABLE));
    return 0;
}

static int spell_effects(void) {
    fixture();
    for (int id = W2_UPGRADE_RANGER; id < W2_UPGRADE_COUNT; ++id) W2_ApplyUpgrade(0, id);
    mobj_t *mage = spawn(MT_MAGE, (fvec2_t){5.5f, 5.5f}, 0);
    mobj_t *dk = spawn(MT_DEATH_KNIGHT, (fvec2_t){5.5f, 12.5f}, 0);
    mobj_t *paladin = spawn(MT_PALADIN, (fvec2_t){3.5f, 5.5f}, 0);
    mobj_t *ogre = spawn(MT_OGRE_MAGE, (fvec2_t){3.5f, 12.5f}, 0);
    mobj_t *friend = spawn(MT_FOOTMAN, (fvec2_t){7.5f, 5.5f}, 0);
    mage->w2.mana = 255;
    RTS_RUN(cast(mage, W2_SPELL_INVISIBILITY, friend));
    CHECK(friend->w2.buffs[W2_BUFF_INVISIBLE] && W2_VisibleTo(friend, 0) && !W2_VisibleTo(friend, 1));
    mage->w2.mana = 255;
    RTS_RUN(cast(mage, W2_SPELL_FLAME_SHIELD, friend));
    CHECK(effect(W2_FX_FLAME_SHIELD));
    mobj_t *enemy = spawn(MT_GRUNT, (fvec2_t){8.5f, 5.5f}, 1);
    int hp = enemy->hp;
    tick(8); CHECK(enemy->hp < hp);
    dk->w2.mana = 255;
    friend->core.position = fixed3_from_fvec2((fvec2_t){7.5f, 12.5f}, 0);
    friend->w2.buffs[W2_BUFF_SLOW] = 100;
    RTS_RUN(cast(dk, W2_SPELL_HASTE, friend));
    CHECK(!friend->w2.buffs[W2_BUFF_SLOW] && friend->speed == friend->info->speed * 2);
    paladin->w2.mana = 255;
    RTS_RUN(cast(paladin, W2_SPELL_VISION, NULL));
    bool vision = false;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (th->function == P_MobjThinker && !u->remove && u->type_id == MT_W2_EFFECT &&
            u->w2.cast.spell == W2_SPELL_VISION) vision |= W2_SightRange(u) == 12;
    }
    CHECK(vision);
    ogre->w2.mana = 255;
    RTS_RUN(cast(ogre, W2_SPELL_EYE, NULL));
    mobj_t *eye = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (th->function == P_MobjThinker && u->type_id == MT_EYE_OF_KILROGG && !u->remove) eye = u;
    }
    CHECK(eye && eye->owner == 0 && eye->w2.ttl > 700);
    eye->w2.ttl = 1; eye->w2.buffs[W2_BUFF_ARMOR] = 100;
    uint32_t eye_id = eye->id; tick(1); CHECK(!P_MobjById(eye_id));

    fixture();
    dk = spawn(MT_DEATH_KNIGHT, (fvec2_t){5.5f, 5.5f}, 0);
    dk->hp = 10; dk->w2.mana = 255;
    mobj_t *first = spawn(MT_GRUNT, (fvec2_t){7.5f, 5.5f}, 1); first->hp = 10;
    mobj_t *second = spawn(MT_GRUNT, (fvec2_t){8.5f, 5.5f}, 1);
    CHECK(W2_CastOrder(dk, W2_SPELL_DEATH_COIL, NULL, first->core.position)); A_W2_Cast(dk);
    CHECK(first->hp == 10 && second->hp == 60 && dk->w2.mana == 155);
    tick(12); CHECK(first->hp == 0 && second->hp == 20 && dk->hp == 60);
    W2_ApplyUpgrade(0, W2_UPGRADE_RAISE_DEAD); dk->w2.mana = 50;
    CHECK(W2_CastOrder(dk, W2_SPELL_RAISE_DEAD, NULL, first->core.position)); A_W2_Cast(dk);
    CHECK(first->remove && dk->w2.mana == 0);
    mobj_t *skeleton = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (th->function == P_MobjThinker && u->type_id == MT_SKELETON && !u->remove) skeleton = u;
    }
    CHECK(skeleton && skeleton->w2.ttl == 3600);
    paladin = spawn(MT_PALADIN, (fvec2_t){7.5f, 7.5f}, 0);
    W2_ApplyUpgrade(0, W2_UPGRADE_EXORCISM); paladin->w2.mana = 255;
    CHECK(W2_CastOrder(paladin, W2_SPELL_EXORCISM, skeleton, skeleton->core.position)); A_W2_Cast(paladin);
    CHECK(skeleton->hp == 0 && effect(W2_FX_EXORCISM));

    fixture();
    mage = spawn(MT_MAGE, (fvec2_t){3.5f, 5.5f}, 0); mage->w2.mana = 255;
    enemy = spawn(MT_GRUNT, (fvec2_t){6.5f, 5.5f}, 1);
    CHECK(W2_CastOrder(mage, W2_SPELL_FIREBALL, enemy, enemy->core.position)); A_W2_Cast(mage);
    CHECK(effect(W2_FX_FIREBALL) && enemy->hp == enemy->max_hp);
    tick(7); CHECK(enemy->hp < enemy->max_hp);
    W2_ApplyUpgrade(0, W2_UPGRADE_BLIZZARD); mage->w2.mana = 25;
    CHECK(W2_CastOrder(mage, W2_SPELL_BLIZZARD, NULL, (fixed3_t){16 * FIXED_ONE, 16 * FIXED_ONE, 0}));
    mage->w2.cast.position = fixed3_from_fvec2((fvec2_t){10.5f, 10.5f}, 0); A_W2_Cast(mage);
    CHECK(effect(W2_FX_BLIZZARD) && mage->w2.mana == 0 && !mage->w2.cast.spell);
    dk = spawn(MT_DEATH_KNIGHT, (fvec2_t){5.5f, 12.5f}, 0); dk->w2.mana = 255;
    W2_ApplyUpgrade(0, W2_UPGRADE_DEATH_AND_DECAY); W2_ApplyUpgrade(0, W2_UPGRADE_WHIRLWIND);
    CHECK(W2_CastOrder(dk, W2_SPELL_DECAY, NULL, mage->core.position)); A_W2_Cast(dk);
    CHECK(effect(W2_FX_DECAY) && dk->w2.cast.spell == W2_SPELL_DECAY);
    CHECK(W2_CastOrder(dk, W2_SPELL_WHIRLWIND, NULL, mage->core.position)); A_W2_Cast(dk);
    CHECK(effect(W2_FX_WHIRLWIND) && effect(W2_FX_WHIRLWIND)->w2.ttl == 800);
    mobj_t *sapper = spawn(MT_DEMOLITION_SQUAD, (fvec2_t){24.5f, 24.5f}, 0);
    mobj_t *farm = spawn(MT_FARM, (fvec2_t){26, 24}, 1);
    CHECK(W2_CastOrder(sapper, W2_SPELL_DEMOLISH, NULL, sapper->core.position)); A_W2_Cast(sapper);
    CHECK(sapper->hp == 0 && farm->hp == 0);
    return 0;
}

int main(void) {
    RTS_RUN(repair_and_fire()); RTS_RUN(projectiles()); RTS_RUN(upgrades()); RTS_RUN(spells()); RTS_RUN(native_effects());
    RTS_RUN(naval());
    RTS_RUN(assist_construction());
    RTS_RUN(spell_effects());
    P_FreeLevel(&level);
    puts("PASS: repair, fire, missiles, research, 19 spells, transports, oil and 28 native effect GRPs");
    return 0;
}
