#include "t_local.h"
#include "warcraft-2.h"
#include "info.h"
#include "w2_local.h"

#define CHECK(c) RTS_CHECK(c, "Warcraft II attack ground", #c)

/* An open 24x20 field of plain land, as the combat tests use. */
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

static fixed3_t cell_point(int x, int y) {
    return fixed3_from_fixed2(fixed2_cell_center((ivec2_t){x, y}), 0);
}

/* Shots that `source` loosed at the ground: its own projectile kind with no
 * unit subject. Impact puffs also carry no subject, so the kind decides. */
static int ground_shots(const mobj_t *source) {
    int count = 0, kind = mobjinfo[source->type_id].w2.projectile;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *shot = (const mobj_t *)th;
        if (th->function == P_MobjThinker && shot->type_id == MT_W2_EFFECT && !shot->remove &&
            shot->target == source && shot->w2.fx.kind == kind && shot->w2.fx.subject == 0) ++count;
    }
    return count;
}

static void ground_order(const mobj_t *unit, fixed3_t point) {
    ticcmd_t cmd = {.order = TC_ATTACK_GROUND, .count = 1, .units = {unit->id}, .position = point};
    G_RunTiccmd(unit->owner, &cmd);
}

/* Attack Ground is for siege, ships and towers with a splash shot. */
static int test_capability(void) {
    fixture();
    mobj_t *ballista = spawn(MT_BALLISTA, 3, 3, 0);
    mobj_t *catapult = spawn(MT_CATAPULT, 3, 6, 0);
    mobj_t *footman = spawn(MT_FOOTMAN, 3, 9, 0);
    mobj_t *archer = spawn(MT_ARCHER, 3, 12, 0);
    mobj_t *farm = spawn(MT_FARM, 10, 10, 0);
    CHECK(W2_CanAttackGround(ballista));
    CHECK(W2_CanAttackGround(catapult));
    CHECK(!W2_CanAttackGround(footman));
    CHECK(!W2_CanAttackGround(archer));
    CHECK(!W2_CanAttackGround(farm));
    CHECK(!W2_CanAttackGround(NULL));
    /* Attack Ground is an attack for the wargus buttons: a footman's order is refused. */
    ground_order(footman, cell_point(14, 9));
    CHECK(!footman->w2.ground.active);
    ground_order(ballista, cell_point(-2, 4));
    CHECK(!ballista->w2.ground.active);
    return 0;
}

/* The shot lands on the point, not on a unit ordered by name. The ballista
 * walks into reach, fires once, and then idles with no target. */
static int test_fires_at_point_then_idles(void) {
    fixture();
    mobj_t *ballista = spawn(MT_BALLISTA, 2, 4, 0);
    mobj_t *victim = spawn(MT_FOOTMAN, 12, 4, 1);
    fixed3_t point = cell_point(12, 4);
    ground_order(ballista, point);
    CHECK(ballista->w2.ground.active);
    CHECK(ballista->w2.ground.position.x == point.x && ballista->w2.ground.position.y == point.y);
    int hp = victim->hp;
    for (int t = 0; t < 2000 && ballista->w2.ground.active; ++t) tick(1);
    CHECK(!ballista->w2.ground.active);
    /* The shot is in the air the tic the order is spent, and the ballista
     * stands still: it never walks onto the point. */
    CHECK(ground_shots(ballista) >= 1);
    CHECK(!P_HasMoveOrder(ballista));
    tick(400);
    CHECK(victim->hp < hp);
    return 0;
}

/* Until it fires, the ground order is the ballista's only goal: an enemy
 * in reach that the unit would otherwise acquire is left alone. */
static int test_no_unit_target_while_pending(void) {
    fixture();
    mobj_t *ballista = spawn(MT_BALLISTA, 2, 4, 0);
    mobj_t *farm = spawn(MT_FARM, 4, 4, 1);
    int farm_hp = farm->hp;
    ground_order(ballista, cell_point(16, 14));
    CHECK(ballista->w2.ground.active);
    /* While pending, the farm in reach is neither a target nor a victim. Once
     * the shot is away the idle ballista may acquire it, as any unit does. */
    for (int t = 0; t < 3000 && ballista->w2.ground.active; ++t) {
        tick(1);
        CHECK(!ballista->attack.target);
        CHECK(farm->hp == farm_hp);
    }
    CHECK(!ballista->w2.ground.active);
    return 0;
}

/* A stop order abandons the ground order before the shot. */
static int test_stop_cancels(void) {
    fixture();
    mobj_t *ballista = spawn(MT_BALLISTA, 2, 4, 0);
    spawn(MT_FOOTMAN, 9, 4, 1);
    ground_order(ballista, cell_point(9, 4));
    CHECK(ballista->w2.ground.active);
    ticcmd_t stop = {.order = TC_STOP, .count = 1, .units = {ballista->id}};
    G_RunTiccmd(0, &stop);
    CHECK(!ballista->w2.ground.active);
    /* Idle, the ballista may still auto-acquire the victim; a ground shot must not happen. */
    for (int t = 0; t < 600; ++t) {
        tick(1);
        CHECK(ground_shots(ballista) == 0);
    }
    return 0;
}

int main(void) {
    RTS_RUN(test_capability());
    RTS_RUN(test_fires_at_point_then_idles());
    RTS_RUN(test_no_unit_target_while_pending());
    RTS_RUN(test_stop_cancels());
    P_FreeLevel(&level);
    puts("PASS: attack ground capability, point fire with splash, idle after the shot, no auto-target while pending, stop cancels");
    return 0;
}
