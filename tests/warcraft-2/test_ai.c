#include "t_local.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#include <stdlib.h>

#define CHECK(c) RTS_CHECK(c, "Warcraft II computer player", #c)

/* The computer player's Warcraft II side: towers armed in place, spells in
 * battle, the research ladder, fleets and ferries, and passive slots.
 * AI_TRACE=1 prints what each played map's computer owns. */

static int count_type(int owner, int type) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *unit = (const mobj_t *)th;
        if (th->function == P_MobjThinker && !unit->remove && unit->hp > 0 && unit->owner == owner &&
            unit->type_id == type && !W2_UnderConstruction(unit)) ++n;
    }
    return n;
}

static void census(int owner, const AiStats *stats) {
    if (!getenv("AI_TRACE")) return;
    printf("  owner %d:", owner);
    for (int type = 1; type <= W2_TYPE_COUNT; ++type)
        if (count_type(owner, type)) printf(" %s=%d", mobjinfo[type].name, count_type(owner, type));
    printf("\n  research:");
    for (int id = 1; id < W2_UPGRADE_COUNT; ++id)
        if (W2_HasResearch(owner, id)) printf(" %s;", W2_Upgrade(id)->label);
    printf("\n  buys %d, waves %d (%d units), upgrades %d\n", stats->purchases, stats->waves,
           stats->wave_units, stats->upgrades);
}

/* ── fixtures ─────────────────────────────────────────────────────────── */

/* An open field of plain land, as the gameplay tests use. */
static void field(int size) {
    P_FreeLevel(&level); P_InitThinkers(); G_InitGame();
    consoleplayer = 0; leveltime = 1;
    level.width = level.height = size;
    int cells = size * size;
    level.tile_ids = calloc((size_t)cells, sizeof(*level.tile_ids));
    level.blocked = calloc((size_t)cells, 1); level.cell_solid = calloc((size_t)cells, 1);
    level.cell_terrain = calloc((size_t)cells, 1);
    level.resource_vents = calloc(8, sizeof(*level.resource_vents));
    level.speeds = calloc(1, sizeof(*level.speeds));
    level.speeds->class_count = 5;
    level.speeds->terrain[1][0] = level.speeds->terrain[2][0] = 100;
    for (int owner = 0; owner < 8; ++owner)
        for (int r = 0; r < 3; ++r) level.player_resources[owner][r] = 50000;
}

static mobj_t *spawn(int type, int x, int y, int owner) {
    isize2_t foot = mobjinfo[type].w2.footprint;
    bool structure = (mobjinfo[type].w2.flags & W2_STRUCTURE) != 0;
    fvec2_t at = structure ? (fvec2_t){x + foot.w * 0.5f, y + foot.h * 0.5f} : (fvec2_t){x + 0.5f, y + 0.5f};
    mobj_t *unit = P_SpawnMobj(fixed3_from_fvec2(at, 0), (uint16_t)type);
    assert(unit);
    unit->owner = unit->team = (uint8_t)owner;
    unit->allegiance = owner == 0 ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY;
    if (structure) w2_mark_footprint(x, y, foot);
    return unit;
}

static void tick(int count) { while (count-- > 0) P_Ticker(); }

/* One think of the computer's battle orders for `owner`. */
static void tactics(int owner) {
    mobjlist_t list = P_ListMobjs();
    G_AiInterface()->tactics(&level, owner, list.items, list.count);
    P_FreeMobjList(&list);
}

/* ── static defenses ──────────────────────────────────────────────────── */

/* A Watch Tower has no weapon, so the doctrine raises one as a site and
 * arms it in place as a Guard Tower. */
static int defenses(void) {
    field(32);
    spawn(MT_TOWN_HALL, 4, 4, 1);
    spawn(MT_ELVEN_LUMBER_MILL, 4, 12, 1);
    spawn(MT_HUMAN_BLACKSMITH, 12, 4, 1);
    for (int i = 0; i < 6; ++i) spawn(MT_FARM, 20 + (i % 3) * 3, 20 + (i / 3) * 3, 1);
    for (int i = 0; i < 3; ++i) spawn(MT_PEASANT, 10 + i, 12, 1);
    AiContext ai;
    P_AiInit(&ai);
    P_AiAttachGame(&ai, G_AiInterface());
    P_AiSetFeatures(&ai, AI_FEATURE_PRODUCTION);
    AiUnitInfo watch, guard, cannon;
    P_AiUnitInfo(&ai, MT_HUMAN_WATCH_TOWER, &watch);
    P_AiUnitInfo(&ai, MT_HUMAN_GUARD_TOWER, &guard);
    P_AiUnitInfo(&ai, MT_HUMAN_CANNON_TOWER, &cannon);
    CHECK((watch.roles & AI_ROLE_DEFENSE) && !(watch.roles & (AI_ROLE_HITS_GROUND | AI_ROLE_HITS_AIR)));
    CHECK((guard.roles & AI_ROLE_DEFENSE) && (guard.roles & AI_ROLE_HITS_AIR) && (cannon.roles & AI_ROLE_HITS_GROUND));
    bool site = false;
    for (int t = 0; t < RTS_TICRATE * 60 * 4 && !count_type(1, MT_HUMAN_GUARD_TOWER); ++t) {
        tick(1);
        G_ProductionTicker(RTS_FIXED_DT);
        mobjlist_t list = P_ListMobjs();
        AiTeamState *team = &ai.teams[1];
        if (team->plan_loaded) team->plan.goal_count = 0; /* The doctrine alone. */
        P_AiTick(&ai, &level, list.items, list.count, gameinfo, 1000 / RTS_TICRATE);
        P_FreeMobjList(&list);
        site |= count_type(1, MT_HUMAN_WATCH_TOWER) > 0;
    }
    CHECK(ai.teams[1].plan.doctrine.defenses > 0);
    CHECK(site && count_type(1, MT_HUMAN_GUARD_TOWER) == 1 && !count_type(1, MT_HUMAN_WATCH_TOWER));
    return 0;
}

/* ── spells ───────────────────────────────────────────────────────────── */

static mobj_t *effect_of(int spell) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *fx = (mobj_t *)th;
        if (th->function == P_MobjThinker && !fx->remove && fx->type_id == MT_W2_EFFECT && fx->w2.cast.spell == spell)
            return fx;
    }
    return NULL;
}

/* Wargus spells.lua ai-cast: heal wounded organic allies, exorcise undead. */
static int paladin(void) {
    field(32);
    mobj_t *caster = spawn(MT_PALADIN, 4, 4, 1);
    mobj_t *hurt = spawn(MT_FOOTMAN, 6, 4, 1);
    caster->w2.mana = 255;
    hurt->hp = hurt->max_hp / 2;
    tactics(1);
    CHECK(!caster->w2.cast.spell); /* Not researched. */
    W2_ApplyUpgrade(1, W2_UPGRADE_HEALING);
    W2_ApplyUpgrade(1, W2_UPGRADE_EXORCISM);
    tactics(1);
    CHECK(caster->w2.cast.spell == W2_SPELL_HEAL && caster->w2.cast.target == hurt->id);
    tick(20);
    CHECK(hurt->hp == hurt->max_hp && caster->w2.mana < 255);
    mobj_t *skeleton = spawn(MT_SKELETON, 8, 6, 0);
    caster->w2.mana = 255;
    tactics(1);
    CHECK(caster->w2.cast.spell == W2_SPELL_EXORCISM && caster->w2.cast.target == skeleton->id);
    tick(20);
    CHECK(skeleton->hp <= 0 || skeleton->remove);
    return 0;
}

/* Bloodlust on a fighter in combat, never on a unit standing idle. */
static int ogre_mage(void) {
    field(32);
    mobj_t *caster = spawn(MT_OGRE_MAGE, 4, 4, 1);
    mobj_t *grunt = spawn(MT_GRUNT, 6, 4, 1);
    mobj_t *enemy = spawn(MT_FOOTMAN, 8, 4, 0);
    W2_ApplyUpgrade(1, W2_UPGRADE_BLOODLUST);
    caster->w2.mana = 255;
    tactics(1);
    CHECK(!caster->w2.cast.spell);
    grunt->attack.target = enemy;
    tactics(1);
    CHECK(caster->w2.cast.spell == W2_SPELL_BLOODLUST);
    tick(20);
    CHECK(grunt->w2.buffs[W2_BUFF_BLOODLUST] > 900 || caster->w2.buffs[W2_BUFF_BLOODLUST] > 900);
    return 0;
}

/* Blizzard on a clump of enemies, but not where its own side stands;
 * polymorph the strongest enemy, slow the fastest, shield the melee. */
static int mage(void) {
    field(32);
    mobj_t *caster = spawn(MT_MAGE, 4, 10, 1);
    for (int i = 0; i < 4; ++i) spawn(MT_FOOTMAN, 12 + i % 2, 9 + i / 2, 0);
    W2_ApplyUpgrade(1, W2_UPGRADE_BLIZZARD);
    caster->w2.mana = 255;
    mobj_t *friend = spawn(MT_FOOTMAN, 13, 10, 1);
    tactics(1);
    CHECK(caster->w2.cast.spell != W2_SPELL_BLIZZARD); /* A friend in the clump. */
    P_RemoveMobj(friend);
    tick(1);
    tactics(1);
    CHECK(caster->w2.cast.spell == W2_SPELL_BLIZZARD);
    int mana = caster->w2.mana;
    tick(30);
    CHECK(caster->w2.mana < mana && effect_of(W2_SPELL_BLIZZARD));

    field(32);
    caster = spawn(MT_MAGE, 4, 10, 1);
    mobj_t *knight = spawn(MT_KNIGHT, 10, 10, 0);
    mobj_t *footman = spawn(MT_FOOTMAN, 10, 12, 0);
    W2_ApplyUpgrade(1, W2_UPGRADE_POLYMORPH);
    W2_ApplyUpgrade(1, W2_UPGRADE_SLOW);
    caster->w2.mana = 255;
    tactics(1);
    CHECK(caster->w2.cast.spell == W2_SPELL_POLYMORPH && caster->w2.cast.target == knight->id);
    tick(20);
    CHECK(knight->type_id == MT_CRITTER);
    caster->w2.mana = 100;
    tactics(1);
    CHECK(caster->w2.cast.spell == W2_SPELL_SLOW && caster->w2.cast.target == footman->id);
    tick(20);
    CHECK(footman->w2.buffs[W2_BUFF_SLOW] > 900);
    W2_ApplyUpgrade(1, W2_UPGRADE_FLAME_SHIELD);
    mobj_t *guard = spawn(MT_FOOTMAN, 8, 12, 1);
    guard->attack.target = footman;
    caster->w2.mana = 100;
    tactics(1);
    CHECK(caster->w2.cast.spell == W2_SPELL_FLAME_SHIELD && caster->w2.cast.target == guard->id);
    tick(20);
    CHECK(effect_of(W2_SPELL_FLAME_SHIELD));
    caster->w2.mana = 100;
    tactics(1);
    CHECK(caster->w2.cast.spell != W2_SPELL_FLAME_SHIELD); /* Shielded already. */
    return 0;
}

/* Death and decay on a clump, haste on an attacking fighter, whirlwind
 * on a building. */
static int death_knight(void) {
    field(32);
    mobj_t *caster = spawn(MT_DEATH_KNIGHT, 4, 10, 1);
    for (int i = 0; i < 3; ++i) spawn(MT_FOOTMAN, 12, 9 + i, 0);
    W2_ApplyUpgrade(1, W2_UPGRADE_DEATH_AND_DECAY);
    W2_ApplyUpgrade(1, W2_UPGRADE_HASTE);
    W2_ApplyUpgrade(1, W2_UPGRADE_WHIRLWIND);
    caster->w2.mana = 255;
    tactics(1);
    CHECK(caster->w2.cast.spell == W2_SPELL_DECAY);
    tick(30);
    CHECK(effect_of(W2_SPELL_DECAY));

    field(32);
    caster = spawn(MT_DEATH_KNIGHT, 4, 10, 1);
    mobj_t *grunt = spawn(MT_GRUNT, 6, 10, 1);
    mobj_t *enemy = spawn(MT_FOOTMAN, 9, 10, 0);
    grunt->attack.target = enemy;
    W2_ApplyUpgrade(1, W2_UPGRADE_HASTE);
    W2_ApplyUpgrade(1, W2_UPGRADE_WHIRLWIND);
    caster->w2.mana = 255;
    tactics(1);
    CHECK(caster->w2.cast.spell == W2_SPELL_HASTE);
    tick(20);
    CHECK(grunt->w2.buffs[W2_BUFF_HASTE] > 900 || caster->w2.buffs[W2_BUFF_HASTE] > 900);
    P_RemoveMobj(enemy);
    spawn(MT_FARM, 12, 9, 0);
    spawn(MT_FOOTMAN, 9, 12, 0);
    tick(1);
    grunt->w2.buffs[W2_BUFF_HASTE] = caster->w2.buffs[W2_BUFF_HASTE] = 1;
    grunt->attack.target = NULL;
    caster->w2.mana = 255;
    tactics(1);
    CHECK(caster->w2.cast.spell == W2_SPELL_WHIRLWIND);
    tick(20);
    CHECK(effect_of(W2_SPELL_WHIRLWIND));
    return 0;
}

/* Campaign heroes are not thrown into waves: they stay home. */
static int heroes(void) {
    field(32);
    mobj_t *hero = spawn(MT_LOTHAR, 4, 4, 1);
    mobj_t *footman = spawn(MT_FOOTMAN, 5, 4, 1);
    mobj_t *hall = spawn(MT_GREAT_HALL, 24, 24, 0);
    CHECK(mobjinfo[MT_LOTHAR].w2.attributes & W2_HERO);
    mobj_t *wave[] = {hero, footman};
    CHECK(G_AiInterface()->dispatch(&level, 1, wave, 2, hall));
    CHECK(P_HasMoveOrder(footman) && footman->attack.target == hall);
    CHECK(!P_HasMoveOrder(hero) && !hero->attack.target);
    return 0;
}

/* ── played maps ──────────────────────────────────────────────────────── */

typedef struct {
    int ships_out;   /* most warships seen near the enemy's start */
    int landed;      /* most soldiers seen on the enemy's island */
    int flyers_out;
} sortie_t;

/* Plays `minutes` of a PUD with its real computer players, the human
 * idle, and watches `owner` take the fight to `enemy`. */
static RtsGameModel *play(const char *map, int minutes, int stock, int owner, int enemy, sortie_t *out) {
    W2_SetStartResources(stock);
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = g_game_default_root, .map_path = map };
    if (!model || !rts_game_model_load(model, &config)) return NULL;
    ivec2_t away = {0}, spot;
    bool found = false;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *unit = (const mobj_t *)th;
        if (th->function != P_MobjThinker || unit->owner != enemy || !(unit->traits & MF_MOBILE)) continue;
        away = fvec2_cell(fixed3_xy_to_fvec2(unit->core.position));
        found = true;
    }
    memset(out, 0, sizeof(*out));
    for (int t = 0; t < RTS_TICRATE * 60 * minutes; ++t) {
        if (!rts_game_model_tick(model, RTS_FIXED_DT)) return NULL;
        if (!found || t % RTS_TICRATE) continue;
        int ships = 0, landed = 0, flyers = 0;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            const mobj_t *unit = (const mobj_t *)th;
            if (th->function != P_MobjThinker || unit->owner != owner || unit->hp <= 0 || unit->remove ||
                !(unit->traits & MF_ATTACK) || !(unit->traits & MF_MOBILE)) continue;
            ivec2_t at = fvec2_cell(fixed3_xy_to_fvec2(unit->core.position));
            int dx = at.x - away.x, dy = at.y - away.y;
            bool near = dx * dx + dy * dy < 24 * 24;
            if (unit->traits & MF_FLY) flyers += near;
            else if (mobjinfo[unit->type_id].w2.domain == W2_DOMAIN_SEA) ships += near;
            else landed += P_NavNearestReachable(&level, 1, at, away, 2, &spot);
        }
        if (ships > out->ships_out) out->ships_out = ships;
        if (landed > out->landed) out->landed = landed;
        if (flyers > out->flyers_out) out->flyers_out = flyers;
    }
    return model;
}

/* PUD AIPL: a passive computer never acts; the other scripts pick the
 * plan, and an island start plays the sea game. */
static int scripts(void) {
    sortie_t sortie;
    RtsGameModel *model = play("DEATH.PUD", 3, 0, 5, 0, &sortie);
    CHECK(model);
    const w2_pud_t *pud = level.native_data;
    CHECK(pud->owners[5] == 4 && pud->ai[5] == 1);
    CHECK(G_AiInterface()->player_level(&level, 5) == AI_LEVEL_NONE);
    const AiStats *stats = P_AiStats(rts_game_model_ai(model), 5);
    CHECK(stats->thinks == 0 && stats->waves == 0 && stats->purchases == 0);
    rts_game_model_destroy(model);

    W2_SetStartResources(0);
    model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = g_game_default_root, .map_path = "LAND_SEA.PUD" };
    CHECK(model && rts_game_model_load(model, &config));
    pud = level.native_data;
    CHECK(pud->ai[2] == 0 && pud->ai[6] == 25);
    AiPlan land = {0}, sea = {0};
    CHECK(G_AiInterface()->plan(&level, 2, AI_LEVEL_NORMAL, &land));
    CHECK(G_AiInterface()->plan(&level, 6, AI_LEVEL_NORMAL, &sea));
    bool land_fleet = false, sea_fleet = false;
    for (int i = 0; i < land.doctrine.roster_count; ++i)
        land_fleet |= land.doctrine.roster[i].product == 200 + MT_HUMAN_DESTROYER;
    for (int i = 0; i < sea.doctrine.roster_count; ++i)
        sea_fleet |= sea.doctrine.roster[i].product == 200 + MT_HUMAN_DESTROYER && sea.doctrine.roster[i].weight > 0;
    CHECK(!land_fleet && sea_fleet);
    CHECK(land.doctrine.defenses > 0 && land.doctrine.research > 0);
    rts_game_model_destroy(model);
    return 0;
}

/* A rich land game: the research ladder climbs past tier one, the hall
 * becomes a castle, towers are armed and the army waves. */
static int ladder(void) {
    sortie_t sortie;
    RtsGameModel *model = play("LAND_SEA.PUD", 40, 4, 2, 0, &sortie);
    CHECK(model);
    const AiStats *stats = P_AiStats(rts_game_model_ai(model), 2);
    census(2, stats);
    CHECK(count_type(2, MT_CASTLE) == 1);
    CHECK(W2_HasResearch(2, W2_UPGRADE_SWORD2) && W2_HasResearch(2, W2_UPGRADE_HUMAN_SHIELD2));
    CHECK(W2_HasResearch(2, W2_UPGRADE_RANGER));
    CHECK(stats->upgrades >= 5 && stats->waves >= 2);
    CHECK(count_type(2, MT_HUMAN_GUARD_TOWER) + count_type(2, MT_HUMAN_CANNON_TOWER) >= 1);
    rts_game_model_destroy(model);
    return 0;
}

/* The ISLANDS sea script: a shipyard and oil, then a fleet that sails to
 * the enemy's island and a transport that lands soldiers on it. */
static int navy(void) {
    sortie_t sortie;
    RtsGameModel *model = play("ISLANDS.PUD", 30, 4, 1, 0, &sortie);
    CHECK(model);
    const AiStats *stats = P_AiStats(rts_game_model_ai(model), 1);
    census(1, stats);
    if (getenv("AI_TRACE")) printf("  ships out %d, landed %d\n", sortie.ships_out, sortie.landed);
    CHECK(count_type(1, MT_HUMAN_SHIPYARD) && count_type(1, MT_HUMAN_OIL_PLATFORM) + count_type(1, MT_HUMAN_REFINERY));
    CHECK(count_type(1, MT_HUMAN_DESTROYER) + count_type(1, MT_BATTLESHIP) >= 2);
    CHECK(sortie.ships_out >= 2 && sortie.landed >= 1 && stats->waves >= 1);
    rts_game_model_destroy(model);
    return 0;
}

/* The DRAGON air script: dragons behind towers, sent over the water. */
static int air(void) {
    sortie_t sortie;
    RtsGameModel *model = play("DRAGON.PUD", 25, 0, 2, 5, &sortie);
    CHECK(model);
    const AiStats *stats = P_AiStats(rts_game_model_ai(model), 2);
    census(2, stats);
    if (getenv("AI_TRACE")) printf("  flyers out %d\n", sortie.flyers_out);
    CHECK(count_type(2, MT_DRAGON) >= 3 && sortie.flyers_out >= 3);
    /* The death knights' spells come with the ladder. */
    CHECK(W2_HasResearch(2, W2_UPGRADE_DEATH_AND_DECAY) || W2_HasResearch(2, W2_UPGRADE_HASTE));
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    RTS_RUN(defenses());
    RTS_RUN(paladin());
    RTS_RUN(ogre_mage());
    RTS_RUN(mage());
    RTS_RUN(death_knight());
    RTS_RUN(heroes());
    RTS_RUN(scripts());
    RTS_RUN(ladder());
    RTS_RUN(navy());
    RTS_RUN(air());
    P_FreeLevel(&level);
    puts("PASS: Warcraft II computer arms towers, casts in battle, climbs research, sails, lands, flies and keeps passive slots idle");
    return 0;
}
