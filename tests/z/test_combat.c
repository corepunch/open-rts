#include "t_local.h"
#include "engine.h"
#include "../../games/z/z_local.h"
#include "../../games/z/info.h"

#include <stdio.h>
#include <stdlib.h>

static int fail(const char *msg) { return rts_fail("z combat", msg); }

static RtsGameModel *load(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/Z" };
    if (model && rts_game_model_load(model, &config)) return model;
    rts_game_model_destroy(model);
    return NULL;
}

static bool alive(const mobj_t *target) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if ((const mobj_t *)th == target && th->function == P_MobjThinker) return true;
    return false;
}

static mobj_t *nth(int type, int team, int n) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->type_id == type && mo->hp > 0 &&
            (team < 0 || mo->team == team) && n-- == 0) return mo;
    }
    return NULL;
}

/* The `n`th live member of `squad`. */
static mobj_t *member(int squad, int n) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->hp > 0 && mo->z.squad == squad && n-- == 0) return mo;
    }
    return NULL;
}

/* A computer Grunt of another squad than `squad`. */
static mobj_t *other_squad(int squad) {
    for (int i = 0; nth(MT_UNIT_GRUNT, 1, i); ++i)
        if (nth(MT_UNIT_GRUNT, 1, i)->z.squad != squad) return nth(MT_UNIT_GRUNT, 1, i);
    return NULL;
}

static bool tick(RtsGameModel *model, int tics) {
    RtsRenderSnapshot snap;
    for (int t = 0; t < tics; ++t)
        if (!rts_tick(model, &snap)) return false;
    return true;
}

static void place(mobj_t *mo, ivec2_t cell) {
    mo->core.position = fixed3_from_fixed2(fixed2_cell_center(cell), mo->core.position.z);
    P_ClearMove(mo);
    mo->movement.goal = fixed3_xy(mo->core.position);
    mo->movement.order_arrived = true;
}

static void disarm_except(int squad, const mobj_t *keep);

static mobj_t *blast_unit(int type, int team, fixed2_t at, int hp) {
    mobj_t *mo = P_SpawnMobj(fixed3_from_fixed2(at, 0), (uint16_t)type);
    if (!mo) return NULL;
    Z_SetTeam(mo, team);
    mo->hp = mo->max_hp = hp;
    return mo;
}

static fixed2_t pixel_at(fixed2_t at, int x, int y) {
    return fixed2_add(at, (fixed2_t){x * (FIXED_ONE / Z_TILE), y * (FIXED_ONE / Z_TILE)});
}

static int test_shell_blast(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for shell blast");
    fixed2_t at = fixed2_cell_center((ivec2_t){5, 5});
    mobj_t *gun = blast_unit(MT_UNIT_LIGHT_TANK, 0, pixel_at(at, -80, 0), 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, at, 500);
    mobj_t *left = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, -18, -14), 500);
    mobj_t *right = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 26, 18), 500);
    mobj_t *outx = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 27, 0), 500);
    mobj_t *outy = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(at, 0, -15), 500);
    mobj_t *ally = blast_unit(MT_UNIT_GRUNT, 0, at, 500);
    mobj_t *killed = blast_unit(MT_UNIT_GRUNT, 1, at, 1);
    mobj_t *after = blast_unit(MT_UNIT_GRUNT, 1, at, 500);
    mobj_t *vehicle = blast_unit(MT_UNIT_MED_TANK, 1, at, 500);
    if (!gun || !target || !left || !right || !outx || !outy || !ally || !killed || !after || !vehicle)
        return fail("spawn shell targets");
    isize2_t robot_size;
    if (!Z_SpriteSize(level.native_data, 66, &robot_size)) return fail("robot spawn sprite size");
    ivec2_t pixel = fixed2_cell(fixed2_scale(at, FIXED_FROM_INT(Z_TILE)));
    ivec2_t expected_robot = ivec2_add(pixel, (ivec2_t){8 - robot_size.w / 2, 8 - robot_size.h / 2});
    ivec2_t expected_vehicle = ivec2_add(pixel, (ivec2_t){8, 8});
    ivec2_t robot_anchor = Z_BlastAnchor(target), vehicle_anchor = Z_BlastAnchor(vehicle);
    if (robot_anchor.x != expected_robot.x || robot_anchor.y != expected_robot.y ||
        vehicle_anchor.x != expected_vehicle.x || vehicle_anchor.y != expected_vehicle.y)
        return fail("robot and composed vehicle use their native selection anchors");
    ivec2_t shift = ivec2_sub(expected_vehicle, expected_robot);
    vehicle->core.position = fixed3_from_fixed2(pixel_at(at, 26 - shift.x, 18 - shift.y), 0);
    const weapondef_t *weapon = &gun->info->attack;
    if (!Z_Attack(gun, weapon, target)) return fail("shell uses area attack");
    mobj_t *shell = nth(MT_SHELL, 0, 0);
    if (!shell || target->hp != 500) return fail("shell remains in flight before impact");
    P_ExplodeMissile(shell);
    if (target->hp != 450 || left->hp != 450 || right->hp != 450 || after->hp != 450 ||
        vehicle->hp != 450 || killed->hp > 0)
        return fail("shell hits primary and boundary units once, including after a kill");
    if (outx->hp != 500 || outy->hp != 500 || ally->hp != 500 || gun->hp != 500)
        return fail("shell excludes outside and friendly units");
    if (Z_Attack(ally, &ally->info->attack, target)) return fail("bullets do not use shell area damage");
    puts("PASS: z shell blast hits enemies inside native bounds once");
    rts_game_model_destroy(model);
    return 0;
}

static int test_shell_flight(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for shell flight");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    disarm_except(0, NULL);
    fixed2_t start = fixed2_cell_center((ivec2_t){5, 5});
    mobj_t *gun = blast_unit(MT_UNIT_M_MISSILE, 0, start, 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(start, 96, 0), 500);
    if (!gun || !target || !Z_Attack(gun, &gun->info->attack, target)) return fail("launch shell");
    mobj_t *shell = nth(MT_SHELL, 0, 0);
    if (!shell || shell->missile.duration <= 1 || target->hp != 500) return fail("shell takes time to travel");
    uint32_t shell_id = shell->id, target_id = target->id;
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    if (!tick(model, 1) || target->hp != 500 ||
        !G_SaveGameBlob(&save, &size, "Shell flight", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save shell in flight");
    free(save);
    shell = P_MobjById(shell_id); target = P_MobjById(target_id);
    if (!shell || !target || !shell->target || shell->attack.target != target) return fail("load shell references");
    P_RemoveMobj(shell->target);
    if (!tick(model, RTS_TICRATE) || target->hp != 380) return fail("shell completes after its shooter is removed");
    puts("PASS: z shell travels and survives save and shooter removal");
    rts_game_model_destroy(model);
    return 0;
}

static int test_shell_aim(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for shell aim");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    disarm_except(0, NULL);
    fixed2_t start = fixed2_cell_center((ivec2_t){5, 5});
    mobj_t *gun = blast_unit(MT_UNIT_M_MISSILE, 0, start, 500);
    mobj_t *target = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(start, 96, 0), 500);
    mobj_t *first = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(start, 96, 0), 500);
    mobj_t *second = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(start, 160, 0), 500);
    if (!gun || !target || !first || !second) return fail("spawn shell aim fixtures");
    gun->traits &= ~MF_ATTACK; target->traits &= ~MF_ATTACK;
    first->traits &= ~MF_ATTACK; second->traits &= ~MF_ATTACK;
    if (!Z_Attack(gun, &gun->info->attack, target)) return fail("launch aimed shell");
    target->core.position = fixed3_from_fixed2(pixel_at(start, 160, 0), 0);
    if (!tick(model, RTS_TICRATE) || first->hp != 380 || target->hp != 500 || second->hp != 500)
        return fail("moving target does not redirect shell");
    if (!Z_Attack(gun, &gun->info->attack, target)) return fail("launch at removable target");
    P_RemoveMobj(target);
    if (!tick(model, RTS_TICRATE) || second->hp != 380) return fail("shell survives removal of its target");
    puts("PASS: z shells retain their aim when targets move or are removed");
    rts_game_model_destroy(model);
    return 0;
}

static int test_shell_speed(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for shell speed");
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    disarm_except(0, NULL);
    fixed2_t start = fixed2_cell_center((ivec2_t){5, 5});
    isize2_t robot_size;
    if (!Z_SpriteSize(level.native_data, 66, &robot_size)) return fail("shell timing sprite size");
    int x = 96 - 8 + robot_size.w / 2;
    int y = -8 + robot_size.h / 2;
    mobj_t *gun = blast_unit(MT_UNIT_M_MISSILE, 0, start, 500);
    mobj_t *cardinal = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(start, x, y), 500);
    mobj_t *diagonal = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(start, x, y + 96), 500);
    if (!gun || !cardinal || !diagonal) return fail("spawn shell speed fixtures");
    gun->traits &= ~MF_ATTACK; cardinal->traits &= ~MF_ATTACK; diagonal->traits &= ~MF_ATTACK;
    if (!Z_Attack(gun, &gun->info->attack, cardinal) || !Z_Attack(gun, &gun->info->attack, diagonal))
        return fail("launch cardinal and diagonal shells");
    mobj_t *a = nth(MT_SHELL, 0, 0), *b = nth(MT_SHELL, 0, 1);
    /* 96 major-axis pixels, 4 pixels per 70 Hz step, at 30 engine tics/s. */
    if (!a || !b || a->missile.duration != 11 || b->missile.duration != 11)
        return fail("cardinal and diagonal shots use the same major-axis rate");
    if (!tick(model, 10) || cardinal->hp != 500 || diagonal->hp != 500)
        return fail("shells remain in flight before their final tick");
    if (!tick(model, 1) || cardinal->hp != 380 || diagonal->hp != 380)
        return fail("shells arrive on their scheduled tick");
    mobj_t *far = blast_unit(MT_UNIT_GRUNT, 1, pixel_at(start, 1920, 0), 500);
    if (!far || !Z_Attack(gun, &gun->info->attack, far)) return fail("launch long-flight shell");
    mobj_t *long_shot = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->type_id == MT_SHELL && (mo->traits & MF_MISSILE)) long_shot = mo;
    }
    if (!long_shot || long_shot->missile.duration <= 100) return fail("long flight exceeds 100 tics");
    for (int i = 0; i < 100; ++i) P_MobjThinker(long_shot);
    if (long_shot->missile.age != 100 || !(long_shot->traits & MF_MISSILE))
        return fail("long flight takes exactly one step per engine tic");
    puts("PASS: z shell flight uses native major-axis speed");
    rts_game_model_destroy(model);
    return 0;
}

static int test_body_slowdown(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for body damage");
    fixed2_t at = fixed2_cell_center((ivec2_t){5, 5});
    mobj_t *tank = blast_unit(MT_UNIT_MED_TANK, 0, at, 200);
    mobj_t *direct = blast_unit(MT_UNIT_MED_TANK, 0, at, 200);
    mobj_t *robot = blast_unit(MT_UNIT_GRUNT, 0, at, 200);
    mobj_t *jeep = blast_unit(MT_UNIT_JEEP, 0, at, 200);
    if (!tank || !direct || !robot || !jeep) return fail("spawn bodies for slowdown");
    P_DamageMobj(tank, NULL, 49);
    if (tank->z.body_speed != 40 || tank->speed != Z_LevelSpeed(4)) return fail("above three quarters keeps speed");
    P_DamageMobj(tank, NULL, 1);
    if (tank->z.body_speed != 36 || tank->z.body_fire != 63 || tank->z.body_accuracy != 72 || tank->speed != Z_LevelSpeed(3) ||
        G_WeaponCooldown(tank, &tank->info->attack, tank->info->attack.cooldown_ms) != 37 * 1000 / Z_NATIVE_HZ)
        return fail("three quarters scales speed and firing rate to 90 percent");
    P_DamageMobj(tank, NULL, 50);
    if (tank->z.body_speed != 30 || tank->z.body_fire != 53 || tank->z.body_accuracy != 61 || tank->speed != Z_LevelSpeed(3)) return fail("half compounds the 85 percent scale");
    P_DamageMobj(tank, NULL, 50);
    if (tank->z.body_speed != 24 || tank->z.body_fire != 42 || tank->z.body_accuracy != 48 || tank->speed != Z_LevelSpeed(2)) return fail("quarter compounds the 80 percent scale");
    P_DamageMobj(direct, NULL, 151);
    if (direct->z.body_speed != 32 || direct->speed != Z_LevelSpeed(3)) return fail("one hit crossing multiple thresholds applies only the lowest");
    P_DamageMobj(robot, NULL, 151);
    if (robot->z.body_tracked || robot->speed != robot->info->speed) return fail("robot health does not scale vehicle body speed");
    P_DamageMobj(jeep, NULL, 50);
    if (jeep->z.body_fire != 98 || G_WeaponCooldown(jeep, &jeep->info->attack, jeep->info->attack.cooldown_ms) != jeep->info->attack.cooldown_ms)
        return fail("Jeep retains its native firing rate");
    mobj_t *victim = blast_unit(MT_UNIT_JEEP, 1, at, 200);
    if (!victim) return fail("spawn driver accuracy victim");
    victim->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    jeep->z.body_accuracy = 0;
    for (int i = 0; i < 128; ++i, ++leveltime) Z_HitDamage(jeep, &jeep->info->attack, victim, 1, 1);
    if (victim->z.crew_hurt) return fail("zero accuracy cannot hit a driver");
    jeep->z.body_accuracy = 127;
    for (int i = 0; i < 128 && !victim->z.crew_hurt; ++i, ++leveltime)
        Z_HitDamage(jeep, &jeep->info->attack, victim, 1, 1);
    if (!victim->z.crew_hurt) return fail("degraded accuracy is used for driver hits");
    direct->hp = 160;
    P_DamageMobj(direct, NULL, 10);
    if (direct->z.body_speed != 28) return fail("damage after healing uses actual previous health");
    app_t app = {0}; hudtext_t hud = {0}; void *save = NULL; size_t size = 0;
    uint32_t id = tank->id;
    if (!G_SaveGameBlob(&save, &size, "Body damage", &app, rts_game_model_ai(model), &hud) ||
        !G_LoadGameBlob(save, size, &app, rts_game_model_ai(model), &hud)) return fail("save and load damaged body");
    free(save);
    tank = P_MobjById(id);
    if (!tank || tank->z.body_speed != 24 || tank->z.body_fire != 42 || tank->z.body_accuracy != 48 || tank->speed != Z_LevelSpeed(2))
        return fail("save retains mutable body speed, firing rate and accuracy");
    uint32_t hash = G_Consistency();
    tank->z.body_speed++;
    if (G_Consistency() == hash) return fail("body speed contributes to consistency");
    tank->z.body_speed--;
    tank->z.body_fire++;
    if (G_Consistency() == hash) return fail("body fire rate contributes to consistency");
    tank->z.body_fire--;
    tank->z.body_accuracy++;
    if (G_Consistency() == hash) return fail("body accuracy contributes to consistency");
    tank->z.body_accuracy--;
    tank->z.body_tracked = false;
    if (G_Consistency() == hash) return fail("body initialization contributes to consistency");
    puts("PASS: z vehicle damage scales its native speed at health thresholds");
    rts_game_model_destroy(model);
    return 0;
}

/* Only `squad`'s members and `keep` fight; everyone else stands down. */
static void disarm_except(int squad, const mobj_t *keep) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (mo != keep && (!squad || mo->z.squad != squad)) mo->traits &= ~MF_ATTACK;
        else if (mo->info) mo->traits |= mo->info->traits & MF_ATTACK;
    }
}

static int cells_apart(const mobj_t *a, const mobj_t *b) {
    ivec2_t d = ivec2_sub(fixed2_cell(fixed3_xy(a->core.position)), fixed2_cell(fixed3_xy(b->core.position)));
    return abs(d.x) > abs(d.y) ? abs(d.x) : abs(d.y);
}

/* A shot robot bursts into bits (290..295) and is gone within a second;
 * it no longer sits on the field for seconds. */
static int test_death(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for a death");
    mobj_t *grunt = nth(MT_UNIT_GRUNT, 1, 0);
    if (!grunt) return fail("a computer Grunt");
    const state_t *die = &states[mobjinfo[grunt->type_id].deathstate];
    if (die->frame != Z_ROBOT_DIE || P_StateFrames(die) != Z_ROBOT_DIE_FRAMES - 1)
        return fail("the death runs the burst pictures");
    P_DamageMobj(grunt, NULL, grunt->hp);
    if (grunt->core.state_id != S_GRUNT_DIE) return fail("the Grunt starts dying");
    int frames = 0, last = -1;
    for (int t = 0; t < RTS_TICRATE && alive(grunt); ++t) {
        if (grunt->core.frame != last) frames++;
        last = grunt->core.frame;
        if (!tick(model, 1)) return fail("tick");
    }
    if (alive(grunt)) return fail("the dead Grunt goes");
    if (frames < Z_ROBOT_DIE_FRAMES) return fail("it shows every burst picture");
    puts("PASS: z a robot bursts and is gone");
    rts_game_model_destroy(model);
    return 0;
}

/* A Grunt ordered onto a robot beyond its reach keeps that target and
 * walks in, even with another enemy in reach on the way. */
static int test_ordered_target(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for an order");
    mobj_t *grunt = nth(MT_UNIT_GRUNT, 0, 0), *mark = nth(MT_UNIT_GRUNT, 1, 0);
    mobj_t *other = mark ? other_squad(mark->z.squad) : NULL;
    if (!grunt || !mark || !other) return fail("a Grunt and two computer squads");
    disarm_except(0, grunt);
    ivec2_t at = fixed2_cell(fixed3_xy(mark->core.position));
    place(grunt, (ivec2_t){at.x, at.y + 9});
    place(other, (ivec2_t){at.x + 3, at.y + 9});
    grunt->hp = grunt->max_hp = 10000;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        P_MobjSetSelected((mobj_t *)th, (mobj_t *)th == grunt);
    RtsGameCommand order = { .kind = RTS_GAME_COMMAND_ATTACK_UNIT, .data.attack_unit = { .target_id = mark->id } };
    if (!rts_game_model_command(model, &order)) return fail("order the attack");
    int start = cells_apart(grunt, mark);
    for (int t = 0; t < 2 * RTS_TICRATE; ++t) {
        if (!tick(model, 1)) return fail("tick");
        if (grunt->attack.target != mark) return fail("the ordered target sticks");
    }
    if (cells_apart(grunt, mark) >= start) return fail("the Grunt closes in");
    puts("PASS: z an ordered target is chased, not swapped for one in reach");
    rts_game_model_destroy(model);
    return 0;
}

/* A Grunt shot at from beyond its squad's reach turns on the shooter and
 * its squad mates go for it too; an idle squad notices an enemy that comes
 * within a few cells of its reach. */
static int test_alert(void) {
    RtsGameModel *model = load();
    if (!model) return fail("load for alerts");
    /* Idle squads, as p_alert.c sees them: no computer player sends them off. */
    P_AiSetFeatures(rts_game_model_ai(model), 0);
    mobj_t *mark = nth(MT_UNIT_GRUNT, 1, 0), *jeep = nth(MT_UNIT_JEEP, Z_NEUTRAL, 0);
    if (!mark || !jeep || !mark->z.squad) return fail("a squad and a Jeep");
    int squad = mark->z.squad;
    Z_SetTeam(jeep, 0);
    jeep->traits |= jeep->info->traits & (MF_SELECTABLE | MF_MOBILE);
    jeep->z.driver = MT_UNIT_GRUNT - MT_UNITS + 1;
    jeep->hp = jeep->max_hp = 10000;
    disarm_except(squad, NULL);
    ivec2_t at = fixed2_cell(fixed3_xy(mark->core.position));
    place(jeep, (ivec2_t){at.x + 12, at.y});
    if (!tick(model, RTS_TICRATE)) return fail("tick");
    for (int i = 0; member(squad, i); ++i)
        if (member(squad, i)->attack.target) return fail("the squad starts idle");
    P_DamageMobj(mark, jeep, 1);
    if (mark->attack.target != jeep) return fail("the shot Grunt turns on the Jeep");
    if (!tick(model, RTS_TICRATE)) return fail("tick");
    for (int i = 0; member(squad, i); ++i)
        if (member(squad, i)->attack.target != jeep) return fail("its squad joins in");

    mobj_t *guard = other_squad(squad);
    if (!guard) return fail("a second squad");
    disarm_except(guard->z.squad, NULL);
    at = fixed2_cell(fixed3_xy(guard->core.position));
    place(jeep, (ivec2_t){at.x, at.y + 12});
    if (!tick(model, RTS_TICRATE)) return fail("tick");
    if (guard->attack.target) return fail("a far Jeep goes unnoticed");
    place(jeep, (ivec2_t){at.x, at.y + 7});
    if (!tick(model, RTS_TICRATE)) return fail("tick");
    if (guard->attack.target != jeep) return fail("a near Jeep is noticed");
    puts("PASS: z squads answer for their members and notice enemies nearby");
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    if (test_death() || test_ordered_target() || test_alert() || test_shell_blast() || test_body_slowdown() ||
        test_shell_flight() || test_shell_aim() || test_shell_speed()) return 1;
    return 0;
}
