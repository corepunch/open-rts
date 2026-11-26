/* Lockstep twins: two processes play the same Rustfront map from the same commands and
 * send the world's consistency vector (g_cmd.c, the one d_net.c compares between peers)
 * after every tic; the parent compares them tic by tic. Every peer of a network game is such
 * a twin, so whatever makes the twins differ would desync a real game.
 *
 * The second twin is made different in every way a peer may be: another local player
 * (consoleplayer, which decides only the view), units selected, a heap laid out differently
 * (anything that orders by pointer shows), and a different map played and freed first in the
 * same process (anything left over from the last game shows). The world must not care. */
#ifndef RF_DESYNC_TWINS_H
#define RF_DESYNC_TWINS_H

#include "t_local.h"
#include "../../games/rustfront/info.h"
#include "../../games/rustfront/rustfront.h"

#include <signal.h>
#include <sys/wait.h>

#define CHECK(c) RTS_CHECK(c, "rustfront desync", #c)

typedef struct {
    uint32_t parts[CONSISTENCY_COUNT];
} twin_tic_t;

/* What the first twin saw happen, so a passing comparison is known to have covered it. */
typedef struct {
    int tics, structures_built, units_trained, kills, mined_out, ore_taken; /* units_trained: things made */
    int on_ramp, low_level, high_level; /* seconds with a unit on a ramp, in a canyon, up a mesa */
    int owner0_refinery, owner0_barracks, owner0_trained; /* owner 0 ever had them; its soldiers at the end */
} twin_report_t;

typedef struct twin_s twin_t;
/* A scripted human's orders for tic `tic`, sent as the network would deliver them. */
typedef void (*twin_script_t)(int tic);

typedef struct {
    const char *map;          /* relative to the game root */
    int tics;
    int humans;               /* owners 0..humans-1 are people (scripted); the rest the computer */
    rf_startplan_t plan;
    twin_script_t script;
    int corrupt_tic;          /* > 0: the second twin breaks its world at this tic (a canary) */
    void (*corrupt)(void);
} twin_game_t;

static RtsGameModel *twin_model;

/* ── orders, as G_RunTiccmd receives them from the network ───────────────── */

static mobj_t *twin_find(int owner, uint16_t type) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (th->function == P_MobjThinker && u->owner == owner && u->type_id == type && !u->remove && u->hp > 0)
            return u;
    }
    return NULL;
}

static void twin_construct(int owner, uint16_t type, ivec2_t cell) {
    mobj_t *hq = twin_find(owner, RF_Role(RF_OwnerFaction(owner), RF_ROLE_HQ));
    if (!hq) return;
    ticcmd_t cmd = {.order = TC_CONSTRUCT, .product = type, .count = 1, .units = {hq->id},
                    .position = fixed3_from_fixed2(fixed2_cell_center(cell), 0)};
    G_RunTiccmd(owner, &cmd);
}

static void twin_train(int owner, uint16_t maker, uint16_t type, int count) {
    mobj_t *m = twin_find(owner, maker);
    if (!m) return;
    for (int i = 0; i < count; ++i) {
        ticcmd_t cmd = {.order = TC_BUILD, .product = type, .count = 1, .units = {m->id}};
        G_RunTiccmd(owner, &cmd);
    }
}

/* Every mobile unit of `owner`, its harvesters or the rest, ordered to `cell`. */
static void twin_order(int owner, int order, ivec2_t cell, bool harvesters) {
    ticcmd_t cmd = {.order = (uint8_t)order, .position = fixed3_from_fixed2(fixed2_cell_center(cell), 0)};
    for (thinker_t *th = thinkercap.next; th != &thinkercap && cmd.count < MAXCOMMANDUNITS; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (th->function != P_MobjThinker || u->owner != owner || u->remove || u->hp <= 0 || !(u->traits & MF_MOBILE))
            continue;
        if (((u->traits & MF_HARVESTER) != 0) != harvesters) continue;
        cmd.units[cmd.count++] = u->id;
    }
    if (cmd.count) G_RunTiccmd(owner, &cmd);
}

/* ── one twin ──────────────────────────────────────────────────────────── */

static void twin_observe(twin_report_t *r) {
    r->kills = 0;
    for (int p = 0; p < 8; ++p) r->kills += level.side_stats[p].losses;
    r->mined_out = r->ore_taken = 0;
    for (int i = 0; i < level.resource_vent_count; ++i) {
        const resourcevent_t *v = &level.resource_vents[i];
        r->ore_taken += 1000 - v->amount;
        if (level.cell_terrain[L_Index(&level, v->cell.x, v->cell.y)] == RF_DefaultTerrain()) r->mined_out++;
    }
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        if (th->function != P_MobjThinker || u->remove || u->hp <= 0 || !(u->traits & MF_MOBILE) || u->owner >= 8)
            continue;
        ivec2_t c = fixed2_cell(fixed3_xy(u->core.position));
        if (!L_Contains(&level, c.x, c.y)) continue;
        if (RF_CellRelief(&level, c.x, c.y) == RF_RELIEF_RAMP) r->on_ramp++;
        int lv = RF_CellLevel(&level, c.x, c.y);
        r->low_level += lv == 0;
        r->high_level += lv >= 2;
    }
}

static int twin_count_structures(int *trained) {
    int n = 0;
    *trained = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        if (th->function != P_MobjThinker || u->remove || u->hp <= 0 || !u->info || u->owner >= 8) continue;
        if (u->type_id >= MT_FX_HIT) continue;
        if (u->info->footprint.w > 0 && !(u->traits & MF_MOBILE)) n++;
        else (*trained)++;
    }
    return n;
}

static bool twin_load(const char *map, const rf_startplan_t *plan) {
    RF_SetStartPlan(plan);
    twin_model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = g_game_default_root, .map_path = map};
    return twin_model && rts_game_model_load(twin_model, &config);
}

static void twin_run(const twin_game_t *game, int second, int out) {
    if (second) {
        /* A different heap: anything ordered by address would come out another way. */
        for (int i = 0; i < 97; ++i) (void)malloc((size_t)(i * 37 % 300 + 16));
        /* A game played and thrown away first: anything one game leaves to the next shows. */
        rf_startplan_t other = {.count = 2, .seat = {{1, RF_CONCORD, 0}, {0, RF_CLANS, 0}}};
        if (!twin_load("maps/ironbridge.txt", &other)) _exit(3);
        for (int t = 0; t < 200; ++t) rts_game_model_tick(twin_model, RTS_TICK_MS);
        rts_game_model_destroy(twin_model);
    }
    /* A network game of `humans` people: d_net.c's D_PlayerIsHuman counts the seats, and the
     * computer plays the rest. Each peer is another of the people. */
    netgame = true;
    doomcom->numplayers = game->humans;
    consoleplayer = second ? game->humans - 1 : 0;
    if (!twin_load(game->map, &game->plan)) _exit(4);
    if (second) {
        RtsGameCommand select = {.kind = RTS_GAME_COMMAND_SELECT_ALL_PLAYER_UNITS};
        rts_game_model_command(twin_model, &select);
    }
    twin_report_t report = {0};
    int units0, structures0 = twin_count_structures(&units0);
    uint32_t ids0 = level.next_mobj_id;
    for (int tic = 0; tic < game->tics; ++tic) {
        if (game->script) game->script(tic);
        if (!rts_game_model_tick(twin_model, RTS_TICK_MS)) _exit(5);
        if (second && tic + 1 == game->corrupt_tic && game->corrupt) game->corrupt();
        twin_tic_t sample;
        G_ConsistencyVector(sample.parts);
        if (write(out, &sample, sizeof(sample)) != (ssize_t)sizeof(sample)) _exit(6);
        if (tic % 30 == 0) { /* once a second */
            twin_report_t now = {0};
            twin_observe(&now);
            report.on_ramp += now.on_ramp > 0;
            report.low_level += now.low_level > 0;
            report.high_level += now.high_level > 0;
            report.kills = now.kills;
            report.mined_out = now.mined_out;
            report.ore_taken = now.ore_taken;
            /* Ever had: a raid may burn them down later. */
            report.owner0_refinery |= twin_find(0, RF_Role(RF_OwnerFaction(0), RF_ROLE_REFINERY)) != NULL;
            report.owner0_barracks |= twin_find(0, RF_Role(RF_OwnerFaction(0), RF_ROLE_BARRACKS)) != NULL;
        }
    }
    report.tics = game->tics;
    int units;
    report.structures_built = twin_count_structures(&units) - structures0;
    report.units_trained = (int)(level.next_mobj_id - ids0); /* every unit, shot and effect made */
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        report.owner0_trained += th->function == P_MobjThinker && u->owner == 0 && RF_IsInfantry(u->type_id) &&
                                 !u->remove && u->hp > 0;
    }
    if (write(out, &report, sizeof(report)) != (ssize_t)sizeof(report)) _exit(7);
    close(out);
    _exit(0);
}

static bool read_all(int fd, void *buffer, size_t size) {
    uint8_t *p = buffer;
    while (size) {
        ssize_t n = read(fd, p, size);
        if (n <= 0) return false;
        p += n;
        size -= (size_t)n;
    }
    return true;
}

/* Plays both twins; returns the first tic whose vectors differ (-1: none) and which
 * subsystems differ there, and the first twin's report. */
static int twin_compare(const twin_game_t *game, uint32_t *subsystems, twin_report_t *report) {
    int fds[2][2];
    pid_t pid[2];
    fflush(stdout);
    fflush(stderr);
    for (int i = 0; i < 2; ++i) {
        if (pipe(fds[i])) return -2;
        pid[i] = fork();
        if (pid[i] < 0) return -2;
        if (!pid[i]) {
            close(fds[i][0]);
            twin_run(game, i, fds[i][1]);
        }
        close(fds[i][1]);
    }
    int first = -1;
    *subsystems = 0;
    for (int tic = 0; tic < game->tics; ++tic) {
        twin_tic_t a, b;
        if (!read_all(fds[0][0], &a, sizeof(a)) || !read_all(fds[1][0], &b, sizeof(b))) { first = -3; break; }
        for (int s = 0; s < CONSISTENCY_COUNT; ++s)
            if (a.parts[s] != b.parts[s]) *subsystems |= 1u << s;
        if (*subsystems) { first = tic; break; }
    }
    twin_report_t other;
    bool reported = first == -1 && read_all(fds[0][0], report, sizeof(*report)) &&
                    read_all(fds[1][0], &other, sizeof(other));
    if (first != -1) for (int i = 0; i < 2; ++i) kill(pid[i], SIGKILL);
    for (int i = 0; i < 2; ++i) {
        int status;
        waitpid(pid[i], &status, 0);
        if (first == -1 && (!WIFEXITED(status) || WEXITSTATUS(status))) {
            fprintf(stderr, "twin %d exited with status %d\n", i + 1, status);
            first = -4;
        }
        close(fds[i][0]);
    }
    if (first == -1 && !reported) first = -5;
    return first;
}

static void twin_print(const char *what, const twin_report_t *r) {
    printf("%s: %d structures and %d things made, %d units lost, %d ore mined, %d cells mined out; "
           "seconds with units on a ramp %d, in a canyon %d, up a mesa %d; owner 0 refinery %d, barracks %d, "
           "%d soldiers\n",
           what, r->structures_built, r->units_trained, r->kills, r->ore_taken, r->mined_out, r->on_ramp,
           r->low_level, r->high_level, r->owner0_refinery, r->owner0_barracks, r->owner0_trained);
}

static void twin_explain(int tic, uint32_t subsystems) {
    if (tic < 0) return;
    char list[128] = "";
    for (int s = 0; s < CONSISTENCY_COUNT; ++s)
        if (subsystems & (1u << s))
            snprintf(list + strlen(list), sizeof(list) - strlen(list), " %s", g_consistency_names[s]);
    fprintf(stderr, "twins first differ at tic %d in:%s\n", tic, list);
}

#endif
