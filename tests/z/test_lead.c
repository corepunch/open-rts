#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

#include <stdio.h>
#include <stdlib.h>

/* Moving-target lead (ZED.EXE 0x640ff and 0x763ed..0x764e3). The expected
 * numbers below are worked by hand from the traced formula:
 *   index  = (low signed byte of dx >> 4)^2 + (same for dy), at most 0x3ff
 *   steps  = (roots[index] * 16 / shot_speed) & 0xff
 *   amount = (steps * (target speed byte >> 2)) >> 3, along the facing. */

static int fail(const char *msg) { return rts_fail("z lead", msg); }

static RtsGameModel *load(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/Z" };
    if (model && rts_game_model_load(model, &config)) return model;
    rts_game_model_destroy(model);
    return NULL;
}

static mobj_t *spawn(int type, int team, fixed2_t at) {
    mobj_t *mo = P_SpawnMobj(fixed3_from_fixed2(at, 0), (uint16_t)type);
    if (!mo) return NULL;
    Z_SetTeam(mo, team);
    mo->hp = mo->max_hp = 500;
    return mo;
}

static fixed2_t pixel_at(fixed2_t at, int x, int y) {
    return fixed2_add(at, (fixed2_t){x * (FIXED_ONE / Z_TILE), y * (FIXED_ONE / Z_TILE)});
}

static mobj_t *bullet(void) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->type_id == MT_BULLET && !mo->remove) return mo;
    }
    return NULL;
}

/* The aim point a launched bullet will reach, from its Bresenham record. */
static ivec2_t endpoint(const mobj_t *b) {
    int major = b->z.water.major, minor = b->z.water.minor;
    ivec2_t d = b->z.water.vertical ? (ivec2_t){minor, major} : (ivec2_t){major, minor};
    return ivec2_add(b->z.water.start, (ivec2_t){d.x * b->z.water.direction.x, d.y * b->z.water.direction.y});
}

static void clear_ground(void) {
    zmap_t *map = level.native_data;
    for (int y = 36; y <= 46; ++y) for (int x = 36; x <= 54; ++x) map->ground_attr[y * Z_MAP_STRIDE + x] = 0;
    Z_RebuildCliffs(&level);
}

static int test_gate_and_formula(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load lead formula");
    const zmap_t *map = level.native_data;
    const uint8_t *roots = map->square_roots.bytes;
    /* 0x64110: threshold = accuracy + accuracy / 4 as bytes; draw <= it passes. */
    if (!Z_LeadGate(0, 0) || Z_LeadGate(0, 1) || !Z_LeadGate(60, 75) || Z_LeadGate(60, 76) ||
        !Z_LeadGate(80, 100) || Z_LeadGate(80, 101) || !Z_LeadGate(102, 0x7f) || !Z_LeadGate(127, 0x7f))
        return fail("lead gate threshold boundary");
    if (!Z_LeadGate(60, 75 + 128) || Z_LeadGate(60, 76 + 128)) return fail("lead gate masks the draw to seven bits");
    if (Z_LeadGate(250, 0x7f) != (((250 + 62) & 0xff) >= 0x7f)) return fail("lead gate adds as bytes");
    if (roots[100] != 10 || roots[1023] != 32) return fail("square root table fixture");
    /* Distance 160 gives index 100, roots 10, 160 / 15 = 10 steps. */
    static const int bytes[] = {20, 30, 40, 50, 60}, amounts[] = {6, 8, 12, 15, 18};
    for (int i = 0; i < 5; ++i) {
        ivec2_t out = Z_LeadAim(roots, (ivec2_t){0, 0}, (ivec2_t){160, 0}, bytes[i] >> 2, 2, 15);
        if (!ivec2_equal(out, (ivec2_t){160 + amounts[i], 0})) return fail("lead of a straight mover by speed");
    }
    /* All eight facings, the diagonals adding the whole step to both axes. */
    static const ivec2_t unit[8] = {{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}};
    for (int facing = 0; facing < 8; ++facing) {
        ivec2_t out = Z_LeadAim(roots, (ivec2_t){0, 0}, (ivec2_t){160, 0}, 10, facing, 15);
        if (!ivec2_equal(out, (ivec2_t){160 + unit[facing].x * 12, unit[facing].y * 12})) return fail("lead follows the target facing");
    }
    /* Shot speed zero divides by eight (0x7647f): 160 / 8 = 20 steps. */
    if (!ivec2_equal(Z_LeadAim(roots, (ivec2_t){0, 0}, (ivec2_t){160, 0}, 10, 2, 0), (ivec2_t){160 + 25, 0}))
        return fail("zero shot speed defaults to eight");
    /* A far target clamps the index at 0x3ff: 32 * 16 / 15 = 34 steps. */
    if (!ivec2_equal(Z_LeadAim(roots, (ivec2_t){0, 0}, (ivec2_t){-1000, 0}, 8, 2, 15), (ivec2_t){-1000 + 34, 0}))
        return fail("far lead clamps the table index");
    /* `imul al` keeps only the low signed byte of (delta >> 4): 2048 wraps to -128. */
    if (!ivec2_equal(Z_LeadAim(roots, (ivec2_t){2048, 0}, (ivec2_t){0, 0}, 8, 2, 15), (ivec2_t){34, 0}))
        return fail("difference wraps as a signed byte");
    /* Standing (speed zero) leaves the aim alone; a missing table too. */
    if (!ivec2_equal(Z_LeadAim(roots, (ivec2_t){0, 0}, (ivec2_t){160, 0}, 0, 2, 15), (ivec2_t){160, 0}) ||
        !ivec2_equal(Z_LeadAim(NULL, (ivec2_t){0, 0}, (ivec2_t){160, 0}, 10, 2, 15), (ivec2_t){160, 0}))
        return fail("no speed or table gives no lead");
    rts_game_model_destroy(model);
    return 0;
}

typedef struct { ivec2_t aim, start; uint16_t seed; bool fired; } shot_t;

/* One Gatling shot (accuracy 127: always passes the gate, scatter radius
 * collapses to nothing) at a Grunt 80 pixels east that is, or is not, walking
 * toward `facing`. Returns the launch record. */
static shot_t shoot(int facing, bool moving, int accuracy, int target_type) {
    shot_t shot = {0};
    clear_ground();
    zmap_t *map = level.native_data;
    fixed2_t at = fixed2_cell_center((ivec2_t){40, 40});
    mobj_t *gun = spawn(MT_UNIT_GATLING, 0, at);
    mobj_t *target = spawn(target_type, 1, pixel_at(at, 80, 0));
    if (!gun || !target) return shot;
    gun->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    gun->z.body_tracked = true; gun->z.body_accuracy = (uint8_t)accuracy;
    gun->core.angle = direction_to_angle(2, 8, ANG90, true);
    target->core.angle = direction_to_angle(facing, 8, ANG90, true);
    if (moving) {
        P_MoveUnitTo(&level, target, pixel_at(at, 400, 0));
        if (!P_HasMoveOrder(target)) return shot;
    }
    map->combat_seed = 0x1234;
    shot.fired = Z_Attack(gun, &gun->info->attack, target);
    shot.seed = map->combat_seed;
    mobj_t *b = bullet();
    if (!b) { shot.fired = false; return shot; }
    shot.aim = endpoint(b);
    shot.start = b->z.water.start;
    return shot;
}

static int test_gatling_lead(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load Gatling lead");
    /* The still target sets the unled aim, the same shot without any order. */
    shot_t still = shoot(2, false, 127, MT_UNIT_GRUNT);
    if (!still.fired) return fail("Gatling fires at a standing target");
    if (!still.aim.x) return fail("fixture");
    rts_game_model_destroy(model);

    /* A Grunt walks at speed byte 40 (level 4): 10 after the shift. */
    for (int facing = 0; facing < 8; ++facing) {
        model = load();
        if (!model) return fail("reload Gatling lead");
        shot_t unled = shoot(facing, false, 127, MT_UNIT_GRUNT);
        rts_game_model_destroy(model);
        model = load();
        if (!model) return fail("reload Gatling lead");
        shot_t led = shoot(facing, true, 127, MT_UNIT_GRUNT);
        if (!unled.fired || !led.fired || !ivec2_equal(unled.start, led.start)) return fail("lead fixture fires");
        /* Source to the unled aim in 16ths, hand-worked for this layout. */
        int dx = (int8_t)((int16_t)(led.start.x - unled.aim.x) >> 4), dy = (int8_t)((int16_t)(led.start.y - unled.aim.y) >> 4);
        int index = dx * dx + dy * dy;
        const zmap_t *map = level.native_data;
        int steps = (map->square_roots.bytes[index > 0x3ff ? 0x3ff : index] * 16 / 15) & 0xff;
        int amount = (steps * 10) >> 3;
        static const ivec2_t unit[8] = {{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}};
        ivec2_t want = ivec2_add(unled.aim, (ivec2_t){unit[facing].x * amount, unit[facing].y * amount});
        if (amount <= 0 || !ivec2_equal(led.aim, want)) return fail("Gatling aim leads a walking Grunt along its facing");
        /* The gate draw precedes the scatter's draws, so the seed moved on. */
        if (led.seed == unled.seed) return fail("lead consumes a random draw");
        rts_game_model_destroy(model);
    }

    /* The same shot twice is identical (determinism). */
    model = load();
    shot_t a = shoot(3, true, 127, MT_UNIT_GRUNT);
    rts_game_model_destroy(model);
    model = load();
    shot_t b = shoot(3, true, 127, MT_UNIT_GRUNT);
    if (!ivec2_equal(a.aim, b.aim) || a.seed != b.seed) return fail("lead is deterministic");
    rts_game_model_destroy(model);

    /* A low accuracy follows the gate: the draw comes from the saved seed. */
    model = load();
    uint16_t probe = 0x1234;
    bool pass = Z_LeadGate(30, Z_NextRandom(&probe));
    shot_t gated = shoot(2, true, 30, MT_UNIT_GRUNT);
    rts_game_model_destroy(model);
    model = load();
    shot_t plain = shoot(2, false, 30, MT_UNIT_GRUNT);
    rts_game_model_destroy(model);
    /* With the draw spent first, the scatter of both shots differs only when
     * a lead applies or the stream moved; compare the lead's effect on x. */
    if (pass && gated.aim.x == plain.aim.x && gated.seed == plain.seed) return fail("a passing gate leads");
    if (!pass && gated.aim.x != plain.aim.x && gated.seed == plain.seed) return fail("a failing gate does not lead");

    /* A moving vehicle leads by its body speed byte (Jeep: 60, tracked 20 -> 5). */
    model = load();
    shot_t jeep_unled = shoot(2, false, 127, MT_UNIT_JEEP);
    rts_game_model_destroy(model);
    model = load();
    shot_t jeep_led = shoot(2, true, 127, MT_UNIT_JEEP);
    if (jeep_led.aim.x <= jeep_unled.aim.x) return fail("a moving vehicle is led");
    rts_game_model_destroy(model);

    return 0;
}

static int test_lead_stops(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load lead stop");
    clear_ground();
    zmap_t *map = level.native_data;
    fixed2_t at = fixed2_cell_center((ivec2_t){40, 40});
    mobj_t *gun = spawn(MT_UNIT_GATLING, 0, at);
    mobj_t *target = spawn(MT_UNIT_GRUNT, 1, pixel_at(at, 80, 0));
    if (!gun || !target) return fail("spawn lead stop fixture");
    gun->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    gun->z.body_tracked = true; gun->z.body_accuracy = 127;
    gun->core.angle = direction_to_angle(2, 8, ANG90, true);
    target->core.angle = direction_to_angle(4, 8, ANG90, true);
    P_MoveUnitTo(&level, target, pixel_at(at, 80, 400));
    if (!P_HasMoveOrder(target)) return fail("target walks");
    map->combat_seed = 0x1234;
    if (!Z_Attack(gun, &gun->info->attack, target)) return fail("first shot");
    mobj_t *first = bullet();
    ivec2_t led = endpoint(first);
    uint32_t id = first->id;
    /* The target stops while the shot is in flight: the shot keeps its aim,
     * and the next one has no lead and draws nothing for it. */
    P_ClearMove(target);
    if (P_HasMoveOrder(target)) return fail("target stops");
    if (!ivec2_equal(endpoint(P_MobjById(id)), led)) return fail("a shot in flight keeps its led aim");
    uint16_t seed = map->combat_seed;
    P_RemoveMobj(first);
    if (!Z_Attack(gun, &gun->info->attack, target)) return fail("second shot");
    mobj_t *second = bullet();
    if (!second || endpoint(second).y >= led.y) return fail("a stopped target is not led");
    uint16_t probe = seed;
    Z_NextRandom(&probe); Z_NextRandom(&probe);
    if (map->combat_seed != probe) return fail("a stopped target spends only the scatter draws");
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    if (test_gate_and_formula() || test_gatling_lead() || test_lead_stops()) return 1;
    return 0;
}
