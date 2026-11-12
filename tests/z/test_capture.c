#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

#include <stdio.h>

static int fail(const char *msg) { return rts_fail("z capture", msg); }

static RtsGameModel *load(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/Z" };
    if (model && rts_game_model_load(model, &config)) {
        P_AiSetFeatures(rts_game_model_ai(model), 0);
        return model;
    }
    rts_game_model_destroy(model);
    return NULL;
}

static bool alive(const mobj_t *target) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if ((const mobj_t *)th == target && th->function == P_MobjThinker) return true;
    return false;
}

/* The `n`th live thing of `type` (any team when team < 0). */
static mobj_t *nth(int type, int team, int n) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->type_id == type && mo->hp > 0 &&
            (team < 0 || mo->team == team) && n-- == 0) return mo;
    }
    return NULL;
}

static mobj_t *flag_of(int territory) {
    for (int i = 0; ; ++i) {
        mobj_t *flag = nth(MT_FLAG, -1, i);
        if (!flag || flag->z.territory == territory + 1) return flag;
    }
}

/* Nobody fights, so the tests watch only what they set up. */
static void disarm(void) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        ((mobj_t *)th)->traits &= ~MF_ATTACK;
}

static void place(mobj_t *mo, ivec2_t cell) {
    mo->core.position = fixed3_from_fixed2(fixed2_cell_center(cell), mo->core.position.z);
    P_ClearMove(mo);
    mo->movement.goal = fixed3_xy(mo->core.position);
    mo->movement.order_arrived = true;
}

static bool tick(RtsGameModel *model, int tics) {
    RtsRenderSnapshot snap;
    for (int t = 0; t < tics; ++t)
        if (!rts_tick(model, &snap)) return false;
    return true;
}

/* Level 1 plants eight flags (CPUPLR01.DAT's first territory has none). A
 * Grunt on a neutral flag takes it and the building standing in its
 * territory, and cheers; the computer cannot take it back at once. */
static int test_flags(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load level 1");
    const zmap_t *map = level.native_data;
    int flags = 0;
    while (nth(MT_FLAG, -1, flags)) flags++;
    if (flags != 8) return fail("eight flags");
    int total;
    if (Z_TerritoriesHeld(0, &total) != 1 || total != 9) return fail("production counts all nine territories");
    if (flag_of(0) || !flag_of(1) || flag_of(1)->team != 1 || flag_of(7)->team != 0 || flag_of(3)->team != Z_NEUTRAL)
        return fail("flags carry their territories' teams");
    disarm();
    const zterritory_t *west = &map->territories[3];
    mobj_t *building = NULL;
    for (int i = 0; !building && nth(MT_ROBOT_FACTORY, -1, i); ++i) {
        mobj_t *mo = nth(MT_ROBOT_FACTORY, -1, i);
        ivec2_t cell = fixed2_cell(fixed3_xy(mo->core.position));
        if (cell.x >= west->min.x && cell.x < west->max.x && cell.y >= west->min.y && cell.y < west->max.y)
            building = mo;
    }
    if (!building || building->team != Z_NEUTRAL) return fail("a neutral factory in the west territory");
    mobj_t *grunt = nth(MT_UNIT_GRUNT, 0, 0), *theirs = nth(MT_UNIT_GRUNT, 1, 0);
    place(grunt, west->flag);
    if (!tick(model, 2)) return fail("tick");
    mobj_t *flag = flag_of(3);
    if (flag->team != 0 || flag->owner != 0) return fail("Grunt takes the flag");
    if (Z_TerritoriesHeld(0, &total) != 2 || total != 9) return fail("capture immediately changes production's territory share");
    if (building->team != 0 || building->owner != 0) return fail("the factory changes sides");
    if (grunt->core.state_id != S_GRUNT_CHEER) return fail("Grunt cheers");
    place(grunt, (ivec2_t){west->flag.x + 3, west->flag.y});
    place(theirs, west->flag);
    if (!tick(model, 5) || flag->team != 0) return fail("a fresh flag holds a moment");
    if (!tick(model, 20) || flag->team != 1 || building->team != 1) return fail("the computer takes it back");
    if (Z_TerritoriesHeld(0, &total) != 1 || total != 9) return fail("recapture restores production's territory share");
    puts("PASS: z flags change hands with their buildings");
    rts_game_model_destroy(model);
    return 0;
}

/* A Grunt ordered onto an empty Jeep walks over and drives it away. */
static int test_board(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for boarding");
    disarm();
    mobj_t *jeep = nth(MT_UNIT_JEEP, Z_NEUTRAL, 0), *grunt = nth(MT_UNIT_GRUNT, 0, 0);
    if (!jeep || !grunt || (jeep->traits & MF_MOBILE)) return fail("an empty Jeep and a Grunt");
    P_DamageMobj(jeep, NULL, jeep->hp / 4);
    if (!jeep->z.body_tracked) return fail("the empty Jeep has tracked body damage");
    ivec2_t at = fixed2_cell(fixed3_xy(jeep->core.position));
    place(grunt, (ivec2_t){at.x + 3, at.y});
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        P_MobjSetSelected((mobj_t *)th, (mobj_t *)th == grunt);
    RtsGameCommand order = { .kind = RTS_GAME_COMMAND_ATTACK_UNIT,
                             .data.attack_unit = { .target_id = jeep->id } };
    if (!rts_game_model_command(model, &order)) return fail("order the Grunt onto the Jeep");
    for (int t = 0; t < 10 * RTS_TICRATE && alive(grunt); ++t)
        if (!tick(model, 1)) return fail("tick");
    if (alive(grunt)) return fail("the Grunt gets in");
    if (jeep->team != 0 || jeep->owner != 0 || jeep->z.driver != MT_UNIT_GRUNT - MT_UNITS + 1)
        return fail("the Jeep is the player's, driven by the Grunt");
    if (jeep->z.body_accuracy != 60) return fail("boarding installs the new driver's accuracy");
    if ((jeep->traits & (MF_MOBILE | MF_SELECTABLE | MF_ATTACK)) != (MF_MOBILE | MF_SELECTABLE | MF_ATTACK) ||
        jeep->core.state_id != (int)mobjinfo[MT_UNIT_JEEP].spawnstate || !P_MobjIsSelected(jeep))
        return fail("the Jeep drives, fights and stays selected");
    if (jeep->core.angle != P_PointToAngle(-FIXED_ONE, 0) ||
        states[jeep->core.state_id].count < 2 || states[jeep->core.state_id].tics != 1)
        return fail("the Jeep faces west, as it stood empty, with its motor running");
    fvec2_t start = fixed3_xy_to_fvec2(jeep->core.position);
    RtsGameCommand move = { .kind = RTS_GAME_COMMAND_MOVE_SELECTED,
        .data.move_selected = { .target = fixed2_from_fvec2((fvec2_t){start.x, start.y + 4.0f}) } };
    if (!rts_game_model_command(model, &move) || !tick(model, 3 * RTS_TICRATE)) return fail("drive");
    if (fixed3_xy_to_fvec2(jeep->core.position).y < start.y + 1.0f) return fail("the Jeep moves");
    puts("PASS: z a Grunt gets into an empty Jeep and drives it");
    rts_game_model_destroy(model);
    return 0;
}

/* A Grunt walking over a crate of 20 shares them with its squad of three;
 * then it may attack a building, which it throws them at. */
static int test_grenades(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for grenades");
    disarm();
    mobj_t *crate = nth(MT_GRENADES_2, -1, 0), *grunt = nth(MT_UNIT_GRUNT, 0, 0);
    mobj_t *factory = nth(MT_ROBOT_FACTORY, -1, 0);
    if (!crate || !grunt || !factory || !grunt->z.squad) return fail("a crate, a squad and a factory");
    Z_SetTeam(factory, 1);
    grunt->traits |= MF_ATTACK;
    if (P_CanTarget(grunt, factory)) return fail("bullets cannot hurt a building");
    place(grunt, fixed2_cell(fixed3_xy(crate->core.position)));
    if (!tick(model, 2)) return fail("tick");
    if (alive(crate)) return fail("the crate is picked up");
    int held = 0, members = 0;
    for (int i = 0; nth(MT_UNIT_GRUNT, 0, i); ++i) {
        const mobj_t *mo = nth(MT_UNIT_GRUNT, 0, i);
        if (mo->z.squad != grunt->z.squad) continue;
        held += mo->z.grenades;
        members++;
    }
    if (members != 3 || held != 20 || grunt->z.grenades < 6) return fail("20 grenades shared by three");
    if (!P_CanTarget(grunt, factory)) return fail("grenades reach buildings");
    ivec2_t at = fixed2_cell(fixed3_xy(factory->core.position));
    place(grunt, (ivec2_t){at.x, at.y + 5});
    int before = grunt->z.grenades, hp = factory->hp;
    bool threw = false, flew = false;
    for (int t = 0; t < 10 * RTS_TICRATE && factory->hp == hp; ++t) {
        grunt->attack.target = factory;
        if (!tick(model, 1)) return fail("tick");
        threw |= grunt->core.state_id == S_GRUNT_THROW || grunt->core.state_id == S_GRUNT_TOSS;
        flew |= nth(MT_GRENADE, -1, 0) != NULL;
    }
    if (!threw || !flew) return fail("the Grunt throws a grenade");
    if (factory->hp >= hp || grunt->z.grenades >= before) return fail("the grenade hurts the factory");
    puts("PASS: z Grunts pick up grenades and throw them at buildings");
    rts_game_model_destroy(model);
    return 0;
}

static int test_apc_crew(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for APC crew");
    disarm();
    mobj_t *first = nth(MT_UNIT_GRUNT, 0, 0), *second = nth(MT_UNIT_GRUNT, 0, 1);
    if (!first || !second) return fail("two Grunts");
    first->z.squad = second->z.squad = 250;
    first->hp = 21; second->hp = 13;
    mobj_t *apc = P_SpawnMobj(first->core.position, MT_UNIT_APC);
    if (!apc) return fail("spawn APC");
    Z_SetTeam(apc, Z_NEUTRAL);
    apc->traits &= ~(MF_MOBILE | MF_ATTACK);
    P_SetMobjState(apc, S_A_P_C);
    mobj_t *ordered[] = {first};
    if (!Z_BoardOrder(ordered, 1, apc) || !tick(model, 2)) return fail("board APC");
    if (apc->z.crew_count != 2 || alive(first) || alive(second)) return fail("APC takes its squad");
    if (Z_HitDamage(apc, NULL, apc, 5, 1) != 5 ||
        Z_HitDamage(apc, NULL, nth(MT_UNIT_GRUNT, 1, 0), 5, 1) != 10)
        return fail("APC crew fires its combined weapons");
    apc->core.position.z = FIXED_ONE;
    apc->type_id = MT_UNIT_GATLING;
    if (Z_EjectCrew(apc)) return fail("fort-wall crew cannot eject");
    apc->type_id = MT_UNIT_APC;
    apc->core.position.z = 0;
    apc->z.crew_hurt = apc->z.crew[0].hp;
    if (Z_EjectCrew(apc)) return fail("cannot eject a driver with no health");
    apc->z.crew_hurt = 0;
    ticcmd_t unload = {.order = TC_UNLOAD};
    mobj_t *carrier[] = {apc};
    if (!G_GameUnitCommand(0, &unload, carrier, 1, NULL) ||
        apc->team != Z_NEUTRAL || apc->z.driver || apc->z.crew_count)
        return fail("eject crew and leave empty APC");
    int found21 = 0, found13 = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function != P_MobjThinker || mo->remove || mo->type_id != MT_UNIT_GRUNT || mo->team != 0) continue;
        found21 += mo->hp == 21; found13 += mo->hp == 13;
    }
    if (!found21 || !found13) return fail("ejected crew retain individual health");
    if (Z_EjectCrew(apc)) return fail("empty APC cannot eject");
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        P_MobjSetSelected((mobj_t *)th, false);
    mobj_t *other = P_SpawnMobj(apc->core.position, MT_UNIT_APC);
    if (!other) return fail("spawn second player's APC");
    Z_SetTeam(other, 1);
    other->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    int previous_player = consoleplayer;
    consoleplayer = 1;
    bool ejected = Z_EjectCrew(other);
    consoleplayer = previous_player;
    bool selected_driver = false;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && !mo->remove && mo->team == 1 &&
            mo->type_id == MT_UNIT_GRUNT && P_MobjIsSelected(mo)) selected_driver = true;
    }
    if (!ejected || !selected_driver) return fail("second player selects their ejected driver");
    puts("PASS: z APC boards and ejects its squad");
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    if (test_flags() || test_board() || test_grenades() || test_apc_crew()) return 1;
    return 0;
}
