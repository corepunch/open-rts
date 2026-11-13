#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Retail unit health: a bullet or explosion that leaves exactly zero keeps
 * the unit alive (ZED.EXE 0x5af58 and 0x85ea4 test only the sign of 0x86ca4's
 * result); the next hit kills. */

static int fail(const char *msg) { return rts_fail("z damage", msg); }

static RtsGameModel *load_map(const char *path) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/Z", .map_path = path };
    if (model && rts_game_model_load(model, &config)) return model;
    rts_game_model_destroy(model);
    return NULL;
}

static bool tick(RtsGameModel *model, int tics) {
    RtsRenderSnapshot snap;
    for (int t = 0; t < tics; ++t)
        if (!rts_tick(model, &snap)) return false;
    return true;
}

static mobj_t *spawn(int type, int team, ivec2_t cell, int hp) {
    mobj_t *mo = P_SpawnMobj(fixed3_from_fixed2(fixed2_cell_center(cell), 0), (uint16_t)type);
    if (!mo) return NULL;
    Z_SetTeam(mo, team);
    mo->hp = mo->max_hp = hp;
    return mo;
}

static bool dead(const mobj_t *mo) { return mo->remove || mo->hp <= 0; }

static bool at_zero(const mobj_t *mo) {
    return !dead(mo) && mo->z.zero_hp && mo->hp == 1 && Z_NativeHp(mo) == 0;
}

static mobj_t *nth(int type, int team, int n) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->type_id == type && !mo->remove && mo->hp > 0 &&
            (team < 0 || mo->team == team) && n-- == 0) return mo;
    }
    return NULL;
}

static bool reload(RtsGameModel *model, const char *name) {
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t bytes = 0;
    bool ok = G_SaveGameBlob(&save, &bytes, name, &app, rts_game_model_ai(model), &hud) &&
              G_LoadGameBlob(save, bytes, &app, rts_game_model_ai(model), &hud);
    free(save);
    return ok;
}

static int test_boundary(void) {
    RtsGameModel *model = load_map(NULL);
    if (!model) return fail("load boundary fixture");
    static const int types[] = {MT_UNIT_GRUNT, MT_UNIT_MED_TANK, MT_UNIT_LIGHT_GUN};
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); ++i) {
        mobj_t *exact = spawn(types[i], 1, (ivec2_t){40, 40 + (int)i * 3}, 30);
        mobj_t *over = spawn(types[i], 1, (ivec2_t){44, 40 + (int)i * 3}, 30);
        mobj_t *under = spawn(types[i], 1, (ivec2_t){48, 40 + (int)i * 3}, 30);
        mobj_t *shared = spawn(types[i], 1, (ivec2_t){52, 40 + (int)i * 3}, 30);
        if (!exact || !over || !under || !shared) return fail("spawn boundary units");
        Z_NativeDamage(exact, NULL, 30);
        if (!at_zero(exact)) return fail("exactly zero health survives a bullet or explosion");
        Z_NativeDamage(over, NULL, 31);
        if (!dead(over)) return fail("a negative result kills");
        Z_NativeDamage(under, NULL, 29);
        if (dead(under) || under->z.zero_hp || under->hp != 1 || Z_NativeHp(under) != 1)
            return fail("one health survives without the zero state");
        P_DamageMobj(shared, NULL, 30);
        if (!dead(shared)) return fail("other damage keeps the engine's zero rule");
        Z_NativeDamage(exact, NULL, 1);
        if (!dead(exact)) return fail("any hit after zero kills");
    }
    mobj_t *plain = spawn(MT_UNIT_GRUNT, 1, (ivec2_t){40, 60}, 30);
    if (!plain) return fail("spawn follow-up fixture");
    Z_NativeDamage(plain, NULL, 30);
    P_DamageMobj(plain, NULL, 1);
    if (!dead(plain)) return fail("non-bullet damage kills a zero-health unit");
    mobj_t *building = nth(MT_RADAR, -1, 0);
    if (building) {
        int hp = building->hp;
        Z_NativeDamage(building, NULL, hp);
        if (building->z.zero_hp || building->hp > 0) return fail("buildings keep the engine's zero rule");
    }
    rts_game_model_destroy(model);
    puts("PASS: z exact-zero health survives bullets and explosions only");
    return 0;
}

static int test_shot(void) {
    for (int blast = 0; blast < 2; ++blast) {
        RtsGameModel *model = load_map(NULL);
        if (!model) return fail("load shot fixture");
        zmap_t *map = level.native_data;
        for (int y = 39; y <= 41; ++y) for (int x = 39; x <= 48; ++x)
            map->ground_attr[y * Z_MAP_STRIDE + x] = map->terrain[y * Z_MAP_STRIDE + x] = 0;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
            ((mobj_t *)th)->traits &= ~MF_ATTACK;
        mobj_t *shooter = spawn(blast ? MT_UNIT_LIGHT_TANK : MT_UNIT_GRUNT, 0, (ivec2_t){40, 40}, 500);
        mobj_t *target = spawn(MT_UNIT_GRUNT, 1, (ivec2_t){47, 40}, 500);
        if (!shooter || !target) return fail("spawn shot fixture");
        shooter->traits |= shooter->info->traits & MF_ATTACK;
        shooter->z.body_tracked = true;
        shooter->z.body_accuracy = 127;
        shooter->attack.target = target;
        P_UpdateSight();
        if (!P_Attack(shooter)) return fail("shooter fires");
        mobj_t *shot = nth(blast ? MT_SHELL : MT_BULLET, 0, 0);
        if (!shot) return fail("shot is in flight");
        target->hp = shot->missile.damage;
        shooter->traits &= ~MF_ATTACK;
        for (int n = 0; n < 4 * RTS_TICRATE && target->hp == shot->missile.damage; ++n)
            if (!tick(model, 1)) return fail("tick shot");
        if (!at_zero(target)) return fail("a real shot equal to health leaves zero health");
        if (!reload(model, "Zero health")) return fail("save and load a zero-health unit");
        rts_game_model_destroy(model);
    }
    puts("PASS: z bullet and shell hits at exactly zero health");
    return 0;
}

static int test_state(void) {
    RtsGameModel *model = load_map(NULL);
    if (!model) return fail("load state fixture");
    mobj_t *unit = spawn(MT_UNIT_GRUNT, 1, (ivec2_t){40, 40}, 30);
    mobj_t *other = spawn(MT_UNIT_GRUNT, 1, (ivec2_t){44, 40}, 30);
    if (!unit || !other) return fail("spawn state units");
    uint32_t id = unit->id, other_id = other->id;
    uint32_t plain = G_Consistency();
    Z_NativeDamage(unit, NULL, 30);
    uint32_t zero = G_Consistency();
    if (!at_zero(unit) || zero == plain) return fail("zero health is hashed");
    unit->z.zero_hp = false;
    if (G_Consistency() == zero) return fail("the zero marker alone changes the hash");
    unit->z.zero_hp = true;

    /* Selection and commands. */
    P_MobjSetSelected(unit, true);
    if (!P_MobjIsSelected(unit) || !(unit->traits & MF_SELECTABLE)) return fail("zero-health unit stays selectable");
    ticcmd_t move = {.order = TC_MOVE, .count = 1, .units = {unit->id},
                     .position = fixed3_from_fixed2(fixed2_cell_center((ivec2_t){36, 40}), 0)};
    G_RunTiccmd(unit->owner, &move);
    if (!P_HasMoveOrder(unit) || !at_zero(unit)) return fail("zero-health unit takes a move order");

    /* Save and load. */
    zero = G_Consistency();
    if (!reload(model, "Zero state")) return fail("reload zero state");
    unit = P_MobjById(id);
    other = P_MobjById(other_id);
    if (!unit || !at_zero(unit) || G_Consistency() != zero) return fail("zero state and hash survive reload");
    Z_NativeDamage(unit, NULL, 1);
    if (!dead(unit)) return fail("a reloaded zero-health unit dies on the next hit");

    if (unit->z.zero_hp || !reload(model, "After kill")) return fail("a killed zero-health unit saves and loads plainly");
    /* Malformed zero markers are rejected. */
    for (int bad = 0; bad < 4; ++bad) {
        other = P_MobjById(other_id);
        other->hp = 30;
        other->z.zero_hp = bad == 0 || bad == 1;
        if (bad == 1) other->hp = 1;
        if (bad == 2) memset(&other->z.zero_hp, 2, 1);
        if (bad == 3) { Z_NativeDamage(other, NULL, 30); other->type_id = MT_RADAR; }
        app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t bytes = 0;
        if (!G_SaveGameBlob(&save, &bytes, "Malformed", &app, rts_game_model_ai(model), &hud))
            return fail("save malformed zero marker");
        bool loaded = G_LoadGameBlob(save, bytes, &app, rts_game_model_ai(model), &hud);
        free(save);
        if (bad == 1 ? !loaded : loaded) return fail(bad == 1 ? "a well-formed zero unit loads" :
                                                    "a malformed zero marker is rejected");
        other = P_MobjById(other_id);
        if (other) { other->z.zero_hp = false; if (bad == 3) other->type_id = MT_UNIT_GRUNT; }
    }
    rts_game_model_destroy(model);
    puts("PASS: z zero-health selection, command, save and hash");
    return 0;
}

static int test_crew(void) {
    RtsGameModel *model = load_map(NULL);
    if (!model) return fail("load crew fixture");
    mobj_t *driver = spawn(MT_UNIT_PSYCHO, 0, (ivec2_t){40, 40}, 50);
    mobj_t *empty = spawn(MT_UNIT_APC, Z_NEUTRAL, (ivec2_t){40, 40}, 500);
    if (!driver || !empty) return fail("spawn crew fixture");
    empty->traits &= ~(MF_MOBILE | MF_ATTACK);
    P_SetMobjState(empty, S_A_P_C);
    Z_NativeDamage(driver, NULL, 50);
    uint32_t driver_id = driver->id, apc_id = empty->id;
    if (!at_zero(driver) || !Z_DetourBoard(driver, empty)) return fail("zero-health driver boards");
    if (empty->z.crew_count != 1 || empty->z.crew[0].zero != 1 || empty->z.crew[0].hp != 1)
        return fail("boarding keeps the zero marker");
    if (!reload(model, "Zero crew")) return fail("reload zero crew");
    empty = P_MobjById(apc_id);
    if (!empty || empty->z.crew[0].zero != 1) return fail("crew zero marker survives reload");
    if (!Z_EjectCrew(empty)) return fail("zero-health crew leaves the vehicle");
    mobj_t *out = nth(MT_UNIT_PSYCHO, -1, 0);
    if (!out || !at_zero(out) || out->id == driver_id) return fail("ejected crew is at zero health");
    Z_NativeDamage(out, NULL, 1);
    if (!dead(out)) return fail("ejected zero-health crew dies on the next hit");
    /* A driver already aboard with a zero marker but a corrupt hp is refused. */
    mobj_t *again = spawn(MT_UNIT_PSYCHO, 0, (ivec2_t){60, 40}, 50);
    mobj_t *apc = spawn(MT_UNIT_APC, Z_NEUTRAL, (ivec2_t){60, 40}, 500);
    if (!again || !apc) return fail("spawn second crew fixture");
    apc->traits &= ~(MF_MOBILE | MF_ATTACK);
    P_SetMobjState(apc, S_A_P_C);
    if (!Z_DetourBoard(again, apc)) return fail("healthy driver boards");
    apc->z.crew[0].zero = 1;
    if (Z_EjectCrew(apc)) return fail("a zero marker on a healthy crew member is rejected");
    apc->z.crew[0].zero = 0;
    if (!Z_EjectCrew(apc)) return fail("the unmarked crew leaves");
    rts_game_model_destroy(model);
    puts("PASS: z zero-health crew transfer");
    return 0;
}

static int test_elimination(void) {
    RtsGameModel *model = load_map(NULL);
    if (!model) return fail("load elimination fixture");
    mobj_t *last = nth(MT_UNIT_GRUNT, 1, 0);
    if (!last) return fail("enemy unit for elimination");
    int owner = last->owner;
    mobj_t *survivors[64];
    int count = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->owner == owner && mo != last && count < 64) survivors[count++] = mo;
    }
    if (!tick(model, 1)) return fail("settle elimination fixture");
    G_CheckElimination(ELIMINATE_ALL);
    level.outcome = OUTCOME_NONE;
    for (int i = 0; i < count; ++i) P_RemoveMobj(survivors[i]);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->owner == owner && mo != last) P_RemoveMobj(mo);
    }
    last->hp = last->max_hp = 30;
    Z_NativeDamage(last, NULL, 30);
    if (!at_zero(last)) return fail("last enemy is at zero health");
    G_CheckElimination(ELIMINATE_ALL);
    if (level.outcome != OUTCOME_NONE) return fail("a zero-health survivor keeps its side alive");
    Z_NativeDamage(last, NULL, 1);
    G_CheckElimination(ELIMINATE_ALL);
    if (level.outcome != OUTCOME_VICTORY) return fail("killing the zero-health survivor eliminates the side");
    rts_game_model_destroy(model);
    puts("PASS: z team elimination ignores zero only while it survives");
    return 0;
}

static int test_repair(void) {
    RtsGameModel *model = load_map("LEVEL12/LEVEL12.MAP");
    if (!model) return fail("load repair fixture");
    zmap_t *map = level.native_data;
    map->computer.intelligence = 127;
    for (int t = 0; t < map->territory_count; ++t) map->territories[t].team = 1;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (Z_UnitType(mo) >= 0) P_RemoveMobj(mo);
        else if (mo->type_id == MT_FLAG) Z_SetTeam(mo, 1);
    }
    P_SyncBuildingBlocking();
    mobj_t *facility = nth(MT_REPAIR, -1, 0);
    if (!facility) return fail("repair facility");
    Z_SetTeam(facility, 1);
    mobj_t *tank = P_SpawnMobj(facility->core.position, MT_UNIT_MED_TANK);
    if (!tank) return fail("repair tank");
    Z_SetTeam(tank, 1);
    ivec2_t centre = fixed2_cell(fixed3_xy(facility->core.position));
    bool placed = false;
    for (int d = 1; d <= 6 && !placed; ++d)
        for (int y = -d; y <= d && !placed; ++y)
            for (int x = -d; x <= d && !placed; ++x) {
                fixed2_t cell = fixed2_cell_center(ivec2_add(centre, (ivec2_t){x, y}));
                if (!P_CheckPosition(&level, tank, cell)) continue;
                tank->core.position = fixed3_from_fixed2(cell, 0);
                placed = true;
            }
    if (!placed) return fail("place repair tank");
    Z_NativeDamage(tank, NULL, tank->hp);
    if (!at_zero(tank)) return fail("tank is at zero health");
    ticcmd_t repair = {.order = TC_REPAIR, .target = facility->id, .position = facility->core.position,
                       .count = 1, .units = {tank->id}};
    G_RunTiccmd(1, &repair);
    for (int i = 0; i < 8 * RTS_TICRATE && tank->z.service_work >= 0; ++i)
        if (!tick(model, 1)) return fail("tank approaches repair bay");
    if (tank->z.service_work >= 0) return fail("zero-health tank enters the bay");
    if (tank->max_hp % 20 || tank->z.service_work != -((1 + tank->max_hp / 20) * 880 / map->production_speed))
        return fail("repair stock counts the whole missing health of a zero-health unit");
    int last = Z_NativeHp(tank);
    bool stepped = false;
    for (int i = 0; i < 20 * RTS_TICRATE && tank->z.service; ++i) {
        if (!tick(model, 1)) return fail("repair simulation");
        if (!tank->z.zero_hp && Z_NativeHp(tank) != last) {
            stepped = true;
            if (tank->hp != Z_NativeHp(tank) || tank->hp < 1) return fail("repair from zero stores a plain health");
        }
        last = Z_NativeHp(tank);
    }
    if (!stepped || tank->z.zero_hp || tank->hp != tank->max_hp || Z_NativeHp(tank) != tank->max_hp)
        return fail("repair restores a zero-health tank");
    rts_game_model_destroy(model);
    puts("PASS: z repair from zero health");
    return 0;
}

static void place_top_left(mobj_t *unit, ivec2_t top_left) {
    ivec2_t delta = ivec2_sub(top_left, Z_AITopLeft(unit));
    fixed2_t position = fixed2_add(fixed3_xy(unit->core.position),
        fixed2_scale(fixed2_from_cell(delta), FIXED_ONE / Z_TILE));
    unit->core.position = fixed3_from_fixed2(position, unit->core.position.z);
}

static void burn(mobj_t *fire) {
    fire->z.fire.frame = 3;
    fire->z.fire.native_step = (uint32_t)((int64_t)leveltime * Z_NATIVE_HZ / RTS_TICRATE);
    A_ZGroundFire(fire);
}

/* Ground fire (the flame pass 0x86934 -> 0x86995, damage sites 0x86a3e for
 * class-1 vehicles and 0x86c71 for the rest) tests only the sign. */
static int test_ground_fire(void) {
    static const int types[] = {MT_UNIT_GRUNT, MT_UNIT_MED_TANK};
    RtsGameModel *model = load_map(NULL);
    if (!model) return fail("load ground fire fixture");
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); ++i) {
        ivec2_t origin = {400 + 100 * (int)i, 400};
        mobj_t *fire = P_SpawnMobj(fixed3_from_fixed2(
            fixed2_scale(fixed2_from_cell(origin), FIXED_ONE / Z_TILE), 0), MT_GROUND_FIRE);
        if (!fire) return fail("spawn ground fire");
        fire->team = 0;
        fire->z.fire.damage = 20;
        fire->z.fire.lifetime = 127;
        fire->z.fire.sprite = 71;
        fire->z.fire.frame_count = 4;
        mobj_t *exact = spawn(types[i], 1, (ivec2_t){40, 40}, 20);
        mobj_t *over = spawn(types[i], 1, (ivec2_t){44, 40}, 19);
        mobj_t *under = spawn(types[i], 1, (ivec2_t){48, 40}, 21);
        if (!exact || !over || !under) return fail("spawn ground fire victims");
        mobj_t *victims[] = {exact, over, under};
        for (int v = 0; v < 3; ++v) place_top_left(victims[v], ivec2_add(origin, (ivec2_t){8, 8}));
        burn(fire);
        if (!at_zero(exact)) return fail("ground fire leaving exactly zero health survives");
        if (!dead(over)) return fail("ground fire with a negative result kills");
        if (dead(under) || under->z.zero_hp || Z_NativeHp(under) != 1) return fail("ground fire leaving one health");
        burn(fire);
        if (!dead(exact)) return fail("the next ground fire pass kills a zero-health unit");
    }
    rts_game_model_destroy(model);
    puts("PASS: z ground fire leaves exactly zero health alive");
    return 0;
}

/* The grenade-box unit pass (the explosion pass 0x85ce4..0x85ea4, damage 127,
 * sites 0x85dfa for class-1 vehicles and 0x85e9f for the rest). */
static int test_box_blast(void) {
    static const int types[] = {MT_UNIT_GRUNT, MT_UNIT_MED_TANK};
    RtsGameModel *model = load_map(NULL);
    if (!model) return fail("load box blast fixture");
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); ++i) {
        fixed2_t at = fixed2_cell_center((ivec2_t){40 + 8 * (int)i, 60});
        mobj_t *exact = spawn(types[i], 1, (ivec2_t){40 + 8 * (int)i, 60}, 127);
        mobj_t *over = spawn(types[i], 1, (ivec2_t){40 + 8 * (int)i, 60}, 126);
        mobj_t *under = spawn(types[i], 1, (ivec2_t){40 + 8 * (int)i, 60}, 128);
        mobj_t *box = P_SpawnMobj(fixed3_from_fixed2(at, 0), MT_BOX_BLAST);
        if (!exact || !over || !under || !box) return fail("spawn box blast fixture");
        A_ZBoxBlast(box);
        if (!at_zero(exact)) return fail("a box blast leaving exactly zero health survives");
        if (!dead(over)) return fail("a box blast with a negative result kills");
        if (dead(under) || under->z.zero_hp || Z_NativeHp(under) != 1) return fail("a box blast leaving one health");
        A_ZBoxBlast(box);
        if (!dead(exact)) return fail("the next box blast kills a zero-health unit");
    }
    rts_game_model_destroy(model);
    puts("PASS: z box blast leaves exactly zero health alive");
    return 0;
}

/* 0x86d04 subtracts the damage byte from the byte at +0x3a and its sign flag
 * is the bit 7 of the result, so a wrap needs the damage to exceed the health
 * by 128 or more. Retail data never does: every weapon damage is at most 120,
 * the ground fire byte is 20, the box blast is 127, and every robot health
 * byte is at most 100. A vehicle whose body is spent reaches the byte path
 * with +0x3a cleared, so it needs a damage of 129. */
static int test_byte_wrap_unreachable(void) {
    int max_damage = 0, max_robot_hp = 0;
#define Z_UNIT(t, type, label, kind, mobile, hp, dmg, ...) \
    do { \
        if ((dmg) > max_damage) max_damage = (dmg); \
        if (!strcmp(#kind, "ROBOT") && (hp) > max_robot_hp) max_robot_hp = (hp); \
    } while (0);
#include "../../games/z/units.inc"
#undef Z_UNIT
    if (max_damage != 120 || max_robot_hp != 100) return fail("retail damage and robot health bounds");
    for (int hp = 0; hp <= max_robot_hp; ++hp)
        for (int damage = 1; damage <= 127; ++damage) {
            unsigned char byte = (unsigned char)(hp - damage);
            if ((byte < 128) != (hp - damage >= 0)) return fail("byte subtraction sign equals the plain sign");
        }
    for (int damage = 1; damage <= 127; ++damage)
        if ((unsigned char)(0 - damage) < 128) return fail("a spent body byte path cannot survive");
    puts("PASS: z byte health subtraction cannot wrap with retail data");
    return 0;
}

int main(void) {
    if (test_boundary() || test_shot() || test_state() || test_crew() || test_elimination() || test_repair() ||
        test_ground_fire() || test_box_blast() || test_byte_wrap_unreachable()) return 1;
    return 0;
}
