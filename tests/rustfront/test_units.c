#include "t_local.h"
#include "engine.h"
#include "../../games/rustfront/info.h"
#include "../../games/rustfront/rustfront.h"

#include <stdlib.h>
#include <string.h>

#define CHECK(c) RTS_CHECK(c, "rustfront units", #c)

/* Each faction's special units and powers (g_units.c), on maps/skirmish.txt: Blue (owner 0, the
 * local player) top left, Red (owner 1) bottom right, the computer kept still. */

static RtsGameModel *load(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = g_game_default_root};
    if (!rts_game_model_load(model, &config)) return NULL;
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    return model;
}

static void run(RtsGameModel *model, int tics) {
    for (int t = 0; t < tics; ++t) rts_game_model_tick(model, RTS_TICK_MS);
}

static bool live(const mobj_t *u) { return u && !u->remove && u->hp > 0; }

static int count_owned(int owner, uint16_t type) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        n += th->function == P_MobjThinker && u->owner == owner && u->type_id == type && live(u);
    }
    return n;
}

static mobj_t *find(int owner, uint16_t type) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (th->function == P_MobjThinker && u->owner == owner && u->type_id == type && live(u)) return u;
    }
    return NULL;
}

/* A w x h patch of open ground with nobody on it, searched outward from (x, y). */
static ivec2_t open_area(int x0, int y0, int w, int h) {
    for (int r = 0; r < 20; ++r)
        for (int y = y0 - r; y <= y0 + r; ++y)
            for (int x = x0 - r; x <= x0 + r; ++x) {
                bool ok = true;
                for (int dy = -1; dy <= h && ok; ++dy)
                    for (int dx = -1; dx <= w && ok; ++dx) {
                        int cx = x + dx, cy = y + dy;
                        ok = L_Contains(&level, cx, cy) && !level.blocked[L_Index(&level, cx, cy)] &&
                             !RF_TerrainHoldsOre(level.cell_terrain[L_Index(&level, cx, cy)]);
                    }
                for (thinker_t *th = thinkercap.next; ok && th != &thinkercap; th = th->next) {
                    const mobj_t *u = (const mobj_t *)th;
                    if (th->function != P_MobjThinker || !live(u)) continue;
                    int ux = u->core.position.x >> FIXED_FRAC_BITS, uy = u->core.position.y >> FIXED_FRAC_BITS;
                    ok = !(ux >= x - 1 && ux <= x + w && uy >= y - 1 && uy <= y + h);
                }
                if (ok) return (ivec2_t){x, y};
            }
    return (ivec2_t){-1, -1};
}

static mobj_t *spawn(uint16_t type, int owner, ivec2_t cell) {
    return RF_SpawnThing(type, owner, 0, cell);
}

static void order(int player, ticorder_t what, mobj_t *const *units, int count, const mobj_t *target, fixed2_t at) {
    ticcmd_t cmd = {.order = what, .count = (unsigned)count, .position = fixed3_from_fixed2(at, 0),
                    .target = target ? target->id : 0};
    for (int i = 0; i < count; ++i) cmd.units[i] = units[i]->id;
    G_RunTiccmd(player, &cmd);
}

static void place(mobj_t *u, ivec2_t cell) {
    u->core.position = fixed3_from_fixed2(fixed2_cell_center(cell), 0);
}

/* The APC: three riflemen board it, ride it, are out of reach inside, step out on unload, and
 * bail out hurt when it is destroyed. */
static int test_transport(void) {
    RtsGameModel *model = load();
    CHECK(model);
    ivec2_t at = open_area(18, 12, 6, 4);
    CHECK(at.x >= 0);
    mobj_t *apc = spawn(MT_APC, 0, at);
    mobj_t *men[3];
    for (int i = 0; i < 3; ++i) men[i] = spawn(MT_RIFLEMAN, 0, (ivec2_t){at.x + 3 + i, at.y + 2});
    mobj_t *tank = spawn(MT_TANK, 1, (ivec2_t){at.x + 3, at.y + 8});
    CHECK(apc && men[0] && men[2] && tank);
    tank->traits &= ~MF_ATTACK; /* a witness, not a combatant */
    CHECK(RF_IsTransport(apc));
    order(0, TC_BOARD, men, 3, apc, fixed3_xy(apc->core.position));
    for (int t = 0; t < 20 * RTS_TICRATE && RF_Passengers(apc, NULL, 0) < 3; ++t) run(model, 1);
    CHECK(RF_Passengers(apc, NULL, 0) == 3);
    for (int i = 0; i < 3; ++i) {
        CHECK(men[i]->rf_boarded && !G_UnitCommandable(men[i]) && !(men[i]->traits & MF_SELECTABLE));
        CHECK(!RF_CanTargetHook(tank, men[i]));
    }
    /* They ride along. */
    ivec2_t to = {at.x + 4, at.y + 1};
    order(0, TC_MOVE, &apc, 1, NULL, fixed2_cell_center(to));
    run(model, 6 * RTS_TICRATE);
    CHECK(fixed2_distance_squared64(fixed3_xy(men[0]->core.position), fixed3_xy(apc->core.position)) == 0);
    /* A sixth would not fit: two more board, then the seats are full. */
    mobj_t *more[3];
    for (int i = 0; i < 3; ++i) more[i] = spawn(MT_RIFLEMAN, 0, (ivec2_t){to.x - 2 + i, to.y + 2});
    order(0, TC_BOARD, more, 3, apc, fixed3_xy(apc->core.position));
    run(model, 10 * RTS_TICRATE);
    CHECK(RF_Passengers(apc, NULL, 0) == RF_APC_SEATS);
    CHECK(!more[2]->rf_boarded && more[2]->rf_carrier == 0);
    /* Unloaded, they stand round it, visible and in command again. */
    order(0, TC_UNLOAD, &apc, 1, NULL, (fixed2_t){0});
    CHECK(RF_Passengers(apc, NULL, 0) == 0);
    for (int i = 0; i < 3; ++i) {
        CHECK(!men[i]->rf_boarded && (men[i]->traits & MF_SELECTABLE) && !P_MobjIsHidden(men[i]));
        CHECK(fixed2_distance_squared64(fixed3_xy(men[i]->core.position), fixed3_xy(apc->core.position)) <=
              (int64_t)FIXED_LIT(3.5) * FIXED_LIT(3.5));
    }
    /* Board again and blow the APC up: they bail out at half strength. */
    order(0, TC_BOARD, men, 3, apc, fixed3_xy(apc->core.position));
    for (int t = 0; t < 15 * RTS_TICRATE && RF_Passengers(apc, NULL, 0) < 3; ++t) run(model, 1);
    CHECK(RF_Passengers(apc, NULL, 0) == 3);
    int hp = men[0]->hp;
    P_DamageMobj(apc, tank, apc->hp);
    run(model, 2);
    for (int i = 0; i < 3; ++i) CHECK(live(men[i]) && !men[i]->rf_boarded);
    CHECK(men[0]->hp == hp / 2);
    rts_game_model_destroy(model);
    puts("PASS: the APC carries five soldiers out of reach, unloads them, and they bail out of its wreck");
    return 0;
}

/* Deploying: a Sentry Truck unfolds into a Sentry Post on its cell and back; a Drill Rig sets a
 * Derrick down that opens a well of ore for its side; neither deploys on ore or blocked ground. */
static int test_deploy(void) {
    RtsGameModel *model = load();
    CHECK(model);
    ivec2_t at = open_area(18, 12, 6, 4);
    CHECK(at.x >= 0);
    mobj_t *truck = spawn(MT_SENTRY_TRUCK, 0, at);
    CHECK(truck && RF_CanDeploy(truck));
    uint32_t id = truck->id;
    order(0, TC_DEPLOY, &truck, 1, NULL, (fixed2_t){0});
    CHECK(truck->type_id == MT_SENTRY && truck->id == id && !(truck->traits & MF_MOBILE));
    CHECK((truck->traits & MF_DETECTOR) && truck->max_hp == 380 && truck->hp == truck->max_hp);
    run(model, 1);
    CHECK(level.blocked[L_Index(&level, at.x, at.y)]);
    order(0, TC_DEPLOY, &truck, 1, NULL, (fixed2_t){0});
    CHECK(truck->type_id == MT_SENTRY_TRUCK && (truck->traits & MF_MOBILE));
    run(model, RTS_TICRATE);
    CHECK(!level.blocked[L_Index(&level, at.x, at.y)]);

    mobj_t *rig = spawn(MT_CL_DRILL, 0, (ivec2_t){at.x + 3, at.y + 1});
    CHECK(rig);
    CHECK(RF_Deploy(rig) && rig->type_id == MT_CL_DERRICK && (rig->traits & MF_RESOURCE_SOURCE));
    run(model, 2);
    bool well = false;
    for (int v = 0; v < level.resource_vent_count; ++v)
        well |= level.resource_vents[v].source_id == rig->id && level.resource_vents[v].amount == RF_DERRICK_ORE &&
                level.resource_vents[v].active;
    CHECK(well);
    /* Not on ore: find an ore cell and try there. */
    mobj_t *rig2 = spawn(MT_CL_DRILL, 0, at);
    bool on_ore = false;
    for (int i = 0; i < level.width * level.height && !on_ore; ++i)
        if (RF_TerrainHoldsOre(level.cell_terrain[i])) {
            place(rig2, (ivec2_t){i % level.width, i / level.width});
            on_ore = true;
        }
    CHECK(on_ore && !RF_Deploy(rig2) && rig2->type_id == MT_CL_DRILL);
    /* Not on another unit: next to the derrick's own cells. */
    place(rig2, (ivec2_t){(rig->core.position.x >> FIXED_FRAC_BITS), (rig->core.position.y >> FIXED_FRAC_BITS)});
    CHECK(!RF_Deploy(rig2));
    rts_game_model_destroy(model);
    puts("PASS: sentry trucks deploy and pack up; a drill rig's derrick opens its own well of ore");
    return 0;
}

/* The Phantom: the first hit phases it for three seconds (untouchable, untargetable, silent);
 * the next hit within twenty seconds lands. */
static int test_phase(void) {
    RtsGameModel *model = load();
    CHECK(model);
    ivec2_t at = open_area(18, 12, 4, 4);
    mobj_t *phantom = spawn(MT_PHANTOM, 0, at), *tank = spawn(MT_TANK, 1, (ivec2_t){at.x + 2, at.y + 2});
    CHECK(phantom && tank);
    tank->traits &= ~MF_ATTACK;
    phantom->traits &= ~MF_ATTACK;
    CHECK(RF_CanTargetHook(tank, phantom));
    P_DamageMobj(phantom, tank, 50);
    CHECK(RF_Phased(phantom) && phantom->hp == phantom->max_hp - 50);
    CHECK(!RF_CanTargetHook(tank, phantom) && RF_AttackBlockedHook(phantom, tank));
    CHECK(RF_HitDamage(tank, &tank->info->attack, phantom, 60, 1) == 0);
    CHECK(RF_ShieldDamage(phantom, 60) == 0);
    run(model, 3 * RTS_TICRATE + 1);
    CHECK(!RF_Phased(phantom) && RF_CanTargetHook(tank, phantom));
    int hp = phantom->hp;
    P_DamageMobj(phantom, tank, 50);
    CHECK(!RF_Phased(phantom) && phantom->hp == hp - 50);
    rts_game_model_destroy(model);
    puts("PASS: a Phantom phases out of reach for three seconds when first hit, then recharges");
    return 0;
}

/* Aircraft: only anti-air weapons reach them, and a gunship shoots the ground. */
static int test_aircraft(void) {
    RtsGameModel *model = load();
    CHECK(model);
    ivec2_t at = open_area(18, 12, 5, 4);
    mobj_t *kestrel = spawn(MT_KESTREL, 1, at), *tank = spawn(MT_TANK, 0, (ivec2_t){at.x + 3, at.y + 1});
    mobj_t *rifleman = spawn(MT_RIFLEMAN, 0, (ivec2_t){at.x + 3, at.y + 3});
    CHECK(kestrel && tank && rifleman && (kestrel->traits & MF_FLY));
    CHECK(!RF_CanTargetHook(tank, kestrel) && RF_CanTargetHook(rifleman, kestrel));
    CHECK(RF_AntiAir(MT_LC_SERAPH) && !RF_AntiAir(MT_KESTREL) && !RF_AntiAir(MT_CL_BUZZARD) &&
          RF_AntiAir(MT_CL_TOWER) && RF_AntiAir(MT_LC_TOWER) && !RF_AntiAir(MT_GUN_TURRET));
    /* The gunship rockets the tank; the tank cannot answer. */
    rifleman->hp = 0;
    P_RemoveMobj(rifleman);
    int hp = tank->hp, khp = kestrel->hp;
    run(model, 8 * RTS_TICRATE);
    CHECK(tank->hp < hp && kestrel->hp == khp);
    rts_game_model_destroy(model);
    puts("PASS: aircraft are reached only by anti-air weapons; the Kestrel rockets armour");
    return 0;
}

/* Support: the Field Crane welds a damaged tank and a structure back, the Cantor mends soldiers. */
static int test_support(void) {
    RtsGameModel *model = load();
    CHECK(model);
    ivec2_t at = open_area(18, 12, 6, 4);
    mobj_t *crane = spawn(MT_CRANE, 0, at), *tank = spawn(MT_TANK, 0, (ivec2_t){at.x + 1, at.y});
    CHECK(crane && tank);
    tank->traits &= ~MF_ATTACK;
    tank->hp = 200;
    run(model, 3 * RTS_TICRATE);
    CHECK(tank->hp >= 260 && tank->hp <= 300);
    CHECK(crane->core.state_id == S_CRANE_WORK || crane->core.state_id == S_CRANE_WORK2);
    mobj_t *cantor = spawn(MT_LC_CANTOR, 0, (ivec2_t){at.x + 4, at.y + 2});
    mobj_t *man = spawn(MT_RIFLEMAN, 0, (ivec2_t){at.x + 5, at.y + 2});
    CHECK(cantor && man);
    man->traits &= ~MF_ATTACK;
    man->hp = 10;
    run(model, 3 * RTS_TICRATE);
    CHECK(man->hp >= 30 && man->hp <= 40);
    rts_game_model_destroy(model);
    puts("PASS: the Field Crane mends vehicles, the Cantor mends soldiers");
    return 0;
}

/* KKnD's ground war: the Crusher rolls over soldiers; a Sapper holding ground lays a mine that
 * the enemy cannot see without a detector and that blows up the first enemy to drive over it. */
static int test_crusher_and_mines(void) {
    RtsGameModel *model = load();
    CHECK(model);
    ivec2_t at = open_area(18, 12, 8, 3);
    mobj_t *crusher = spawn(MT_CL_CRUSHER, 0, at);
    mobj_t *men[2] = {spawn(MT_RIFLEMAN, 1, (ivec2_t){at.x + 3, at.y}), spawn(MT_RIFLEMAN, 1, (ivec2_t){at.x + 5, at.y})};
    CHECK(crusher && men[0] && men[1]);
    for (int i = 0; i < 2; ++i) men[i]->traits &= ~(MF_ATTACK | MF_MOBILE);
    crusher->traits &= ~MF_ATTACK;
    order(0, TC_MOVE, &crusher, 1, NULL, fixed2_cell_center((ivec2_t){at.x + 7, at.y}));
    run(model, 6 * RTS_TICRATE);
    CHECK(!live(men[0]) && !live(men[1]));

    /* Red's sapper mines the ground away from Blue's eyes. */
    ivec2_t far = open_area(30, 6, 4, 4);
    CHECK(far.x >= 0);
    mobj_t *sapper = spawn(MT_CL_SAPPER, 1, far);
    CHECK(sapper);
    sapper->traits &= ~MF_ATTACK;
    run(model, 7 * RTS_TICRATE);
    mobj_t *mine = find(1, MT_CL_MINE);
    CHECK(mine && RF_MineCount(1) == 1 && (mine->traits & MF_CLOAKED));
    sapper->hp = 0; /* one mine is enough */
    P_RemoveMobj(sapper);
    /* Blue looks with a scout: it sees the ground, not the mine; a Sentry Post detects it. */
    mobj_t *scout = spawn(MT_SCOUT, 0, (ivec2_t){far.x - 2, far.y});
    scout->traits &= ~MF_ATTACK;
    P_UpdateSight();
    CHECK(!P_VisibleToPlayer(mine));
    mobj_t *post = spawn(MT_SENTRY, 0, (ivec2_t){far.x - 2, far.y + 2});
    post->traits &= ~MF_ATTACK;
    P_UpdateSight();
    CHECK(P_VisibleToPlayer(mine));
    /* The scout drives over it and goes up. */
    int hp = scout->hp;
    order(0, TC_MOVE, &scout, 1, NULL, fixed2_cell_center((ivec2_t){far.x + 2, far.y}));
    run(model, 5 * RTS_TICRATE);
    CHECK(!live(mine));
    CHECK(!live(scout) || scout->hp <= hp - 180);
    rts_game_model_destroy(model);
    puts("PASS: the Crusher rolls over soldiers; sappers lay hidden mines that detectors reveal");
    return 0;
}

/* The Strider's arc leaps from its target to two more. */
static int test_strider(void) {
    RtsGameModel *model = load();
    CHECK(model);
    ivec2_t at = open_area(18, 12, 6, 3);
    mobj_t *strider = spawn(MT_LC_STRIDER, 0, at);
    mobj_t *men[3];
    for (int i = 0; i < 3; ++i) {
        men[i] = spawn(MT_RIFLEMAN, 1, (ivec2_t){at.x + 3 + i, at.y + 1});
        men[i]->traits &= ~(MF_ATTACK | MF_MOBILE);
        men[i]->hp = men[i]->max_hp = 1000;
    }
    order(0, TC_ATTACK, &strider, 1, men[0], fixed3_xy(men[0]->core.position));
    run(model, 3 * RTS_TICRATE);
    int hurt = 0;
    for (int i = 0; i < 3; ++i) hurt += men[i]->hp < 1000;
    CHECK(hurt == 3);
    rts_game_model_destroy(model);
    puts("PASS: the Strider's arc chains to two more enemies");
    return 0;
}

/* 7th Legion's Concord: the Spire deals Rites into a hand of three, each plays as it says, a
 * played card leaves the hand, and a kill pays a tithe. */
static int test_rites(void) {
    RtsGameModel *model = load();
    CHECK(model);
    CHECK(RF_NextDrawMs(0) == -1); /* no Spire, no cards */
    ivec2_t at = open_area(18, 10, 3, 3);
    CHECK(at.x >= 0);
    mobj_t *spire = spawn(MT_LC_SPIRE, 0, at);
    CHECK(spire);
    run(model, 1);
    CHECK(RF_NextDrawMs(0) > 0 && RF_HandCard(0, 0) == RF_RITE_NONE);
    run(model, RF_RITE_DRAW_MS / RTS_TICK_MS);
    CHECK(RF_HandCard(0, 0) != RF_RITE_NONE);
    bool played[RF_RITE_COUNT] = {0};
    ivec2_t field = open_area(24, 18, 4, 4);
    for (int round = 0; round < 24 && !(played[1] && played[2] && played[3] && played[4]); ++round) {
        rf_rite_t rite = RF_HandCard(0, 0);
        if (!rite) { run(model, RF_RITE_DRAW_MS / RTS_TICK_MS); continue; }
        fixed2_t spot = fixed2_cell_center((ivec2_t){field.x + 1, field.y + 1});
        mobj_t *mine = spawn(MT_TANK, 0, (ivec2_t){field.x + 1, field.y + 1});
        mobj_t *theirs = spawn(MT_TANK, 1, (ivec2_t){field.x + 2, field.y + 1});
        mine->traits &= ~MF_ATTACK; theirs->traits &= ~MF_ATTACK;
        mine->hp = 100;
        int credits = level.player_resources[0][0];
        /* Played the way the sidebar plays it: a TC_SPELL with no units. */
        ticcmd_t cmd = {.order = TC_SPELL, .product = 0, .position = fixed3_from_fixed2(spot, 0)};
        G_RunTiccmd(0, &cmd);
        CHECK(RF_HandCard(0, 0) == RF_RITE_NONE);
        switch (rite) {
        case RF_RITE_WINDFALL: CHECK(level.player_resources[0][0] == credits + 750); break;
        case RF_RITE_MENDING: CHECK(mine->hp == 100 + mine->max_hp / 2 && theirs->hp == theirs->max_hp); break;
        case RF_RITE_WARD:
            CHECK(mine->rf_ward > 0 && theirs->rf_ward == 0);
            CHECK(RF_ShieldDamage(mine, 100) == 0);
            break;
        case RF_RITE_STRIKE: {
            int before = theirs->hp;
            run(model, RTS_TICRATE);
            CHECK(theirs->hp == before);                     /* not yet */
            run(model, RTS_TICRATE);
            CHECK(!live(theirs) || theirs->hp <= before - 350);
            CHECK(live(mine) && mine->hp == 100);            /* its own side is spared */
            break;
        }
        default: CHECK(false);
        }
        played[rite] = true;
        mine->hp = 0; P_RemoveMobj(mine);
        if (live(theirs)) { theirs->hp = 0; P_RemoveMobj(theirs); }
        run(model, 2);
    }
    CHECK(played[RF_RITE_STRIKE] && played[RF_RITE_MENDING] && played[RF_RITE_WINDFALL] && played[RF_RITE_WARD]);
    /* The hand and the next draw survive a save. */
    run(model, RF_RITE_DRAW_MS / RTS_TICK_MS);
    rf_rite_t card = RF_HandCard(0, 0);
    CHECK(card != RF_RITE_NONE);
    size_t size = G_SaveExtraSize();
    void *save = malloc(size);
    CHECK(save);
    G_SaveExtra(save);
    ticcmd_t play = {.order = TC_SPELL, .product = 0, .position = fixed3_from_fixed2(fixed2_cell_center(field), 0)};
    G_RunTiccmd(0, &play);
    CHECK(RF_HandCard(0, 0) == RF_RITE_NONE);
    CHECK(G_LoadExtra(save, size) && RF_HandCard(0, 0) == card);
    free(save);
    /* A tithe: a Concord Warden's kill of a rifleman pays a fifth of 100. */
    mobj_t *warden = spawn(MT_LC_WARDEN, 0, field), *victim = spawn(MT_RIFLEMAN, 1, (ivec2_t){field.x + 1, field.y});
    int credits = level.player_resources[0][0];
    P_DamageMobj(victim, warden, victim->hp);
    CHECK(level.player_resources[0][0] == credits + 20);
    rts_game_model_destroy(model);
    puts("PASS: the Spire deals the Rites; Strike, Mending, Windfall and Warding play; kills pay a tithe");
    return 0;
}

/* The computer deploys its trucks and rigs, and plays its cards. */
static int test_ai(void) {
    RtsGameModel *model = load();
    CHECK(model);
    ivec2_t at = open_area(30, 20, 6, 4);
    mobj_t *truck = spawn(MT_SENTRY_TRUCK, 1, at), *rig = spawn(MT_CL_DRILL, 1, (ivec2_t){at.x + 3, at.y + 1});
    CHECK(truck && rig);
    mobjlist_t list = P_ListMobjs();
    RF_AiUnits(1, list.items, list.count);
    P_FreeMobjList(&list);
    CHECK(truck->type_id == MT_SENTRY && rig->type_id == MT_CL_DERRICK);
    CHECK(count_owned(1, MT_SENTRY) == 1);
    rts_game_model_destroy(model);
    puts("PASS: the computer deploys sentry trucks and drill rigs where they stop");
    return 0;
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    RTS_RUN(test_transport());
    RTS_RUN(test_deploy());
    RTS_RUN(test_phase());
    RTS_RUN(test_aircraft());
    RTS_RUN(test_support());
    RTS_RUN(test_crusher_and_mines());
    RTS_RUN(test_strider());
    RTS_RUN(test_rites());
    RTS_RUN(test_ai());
    SDL_Quit();
    return 0;
}
