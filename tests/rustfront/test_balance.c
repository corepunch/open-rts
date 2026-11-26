#include "t_local.h"
#include "engine.h"
#include "../../games/rustfront/info.h"
#include "../../games/rustfront/rustfront.h"

#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define CHECK(c) RTS_CHECK(c, "rustfront balance", #c)

/* The balance harness: the computer against the computer, every pairing of the three factions,
 * on the point-symmetric duel map (maps/duel.py), over many seeds. A seed puts the factions in
 * one corner or the other and shifts when each side thinks, so no two games run alike. A side
 * loses when its last structure falls; a game still open at the time limit goes to the side
 * whose army, base and credits are worth half as much again as the other's, else it is a draw.
 *
 *   build/bin/tests/rustfront/test_balance            every pairing, 12 seeds: asserts that no
 *                                                     faction wins more than 65 % against another
 *   build/bin/tests/rustfront/test_balance --quick    one short game per pairing (make test-rustfront)
 *   OPEN_RTS_BALANCE_SEEDS=n, OPEN_RTS_BALANCE_MINUTES=m  more seeds or longer games
 *
 * Games run in forked processes, several at once. */

static const char *const NAME[RF_FACTION_COUNT] = {"Directorate", "Clans", "Concord"};
enum { MAX_GAMES = 512 };

typedef struct {
    int faction[2];   /* by owner */
    int seed;
    int winner;       /* faction, or -1 for a draw */
    int minutes;      /* when it was decided */
    int worth[2];
    bool timed_out;
    bool ok;
} game_t;

static bool live(const mobj_t *u) {
    return u && !u->remove && u->hp > 0 && u->type_id < MT_FX_HIT;
}

static int price(uint16_t type) {
    const StaticProductDefinition *p = G_ModelProductByUIId(NULL, type);
    return p ? p->cost : 0;
}

/* The side's units, structures and credits, at their price. */
static int worth(int owner) {
    int total = level.player_resources[owner][0];
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        if (th->function == P_MobjThinker && u->owner == owner && live(u))
            total += (int)((int64_t)price(u->type_id) * u->hp / (u->max_hp > 0 ? u->max_hp : 1));
    }
    return total;
}

static int structures(int owner) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *u = (const mobj_t *)th;
        n += th->function == P_MobjThinker && u->owner == owner && live(u) && RF_IsStructure(u);
    }
    return n;
}

/* Gives a side of the duel map (a Directorate base) to `faction`: each structure and the
 * harvester become that faction's in the same role; the starting army goes. */
static void convert(int owner, int faction) {
    mobj_t *units[256];
    int count = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap && count < 256; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (th->function == P_MobjThinker && u->owner == owner && live(u)) units[count++] = u;
    }
    for (int i = 0; i < count; ++i) {
        mobj_t *u = units[i];
        int role = -1;
        for (int r = 0; r < RF_ROLE_COUNT; ++r)
            if (RF_IsRole(u->type_id, (rf_role_t)r)) role = r;
        if (role < 0) { P_RemoveMobj(u); continue; }
        uint16_t type = RF_Role(faction, (rf_role_t)role);
        if (type == u->type_id) continue;
        mobj_t *n = P_SpawnMobj(u->core.position, type);
        if (!n) continue;
        n->core.angle = u->core.angle;
        n->owner = u->owner;
        n->team = u->team;
        n->allegiance = u->allegiance;
        P_RemoveMobj(u);
    }
    RF_SyncBlocking(&level);
}

static game_t play(int fa, int fb, int seed, int max_minutes) {
    game_t g = {.seed = seed, .winner = -1};
    bool swap = seed & 1;
    g.faction[0] = swap ? fb : fa;
    g.faction[1] = swap ? fa : fb;
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = g_game_default_root, .map_path = "maps/duel.txt"};
    if (!rts_game_model_load(model, &config)) return g;
    consoleplayer = 7; /* an observer: both sides are the computer's */
    for (int o = 0; o < 2; ++o) {
        convert(o, g.faction[o]);
        level.player_resources[o][0] = 5000;
        RF_SetAIDifficulty(o, RF_AI_NORMAL);
    }
    AiContext *ai = rts_game_model_ai(model);
    ai->think_counter = seed * 5 + seed / 2;
    int limit = max_minutes * 60 * RTS_TICRATE;
    for (int t = 1; t <= limit; ++t) {
        rts_game_model_tick(model, RTS_TICK_MS);
        if (getenv("OPEN_RTS_BALANCE_TRACE") && t % (30 * RTS_TICRATE) == 0) {
            const AiStats *s0 = P_AiStats(ai, 0), *s1 = P_AiStats(ai, 1);
            printf("    %d:%02d  %s: %d structures, %d credits, worth %d, buys %d, waves %d | "
                   "%s: %d structures, %d credits, worth %d, buys %d, waves %d\n",
                   t / (60 * RTS_TICRATE), t / RTS_TICRATE % 60, NAME[g.faction[0]], structures(0),
                   level.player_resources[0][0], worth(0), s0->purchases, s0->waves, NAME[g.faction[1]],
                   structures(1), level.player_resources[1][0], worth(1), s1->purchases, s1->waves);
        }
        if (t % (5 * RTS_TICRATE)) continue;
        int left[2] = {structures(0), structures(1)};
        if (left[0] && left[1]) continue;
        g.winner = left[0] ? g.faction[0] : left[1] ? g.faction[1] : -1;
        g.minutes = t / (60 * RTS_TICRATE);
        break;
    }
    g.worth[0] = worth(0);
    g.worth[1] = worth(1);
    if (!g.minutes) {
        g.timed_out = true;
        g.minutes = max_minutes;
        if (g.worth[0] * 2 > g.worth[1] * 3) g.winner = g.faction[0];
        else if (g.worth[1] * 2 > g.worth[0] * 3) g.winner = g.faction[1];
    }
    g.ok = true;
    rts_game_model_destroy(model);
    return g;
}

/* Every game in its own process, `workers` at a time. */
static void play_all(game_t *games, int count, int max_minutes, int workers) {
    pid_t pid[MAX_GAMES];
    int pipes[MAX_GAMES];
    int started = 0, finished = 0, running = 0;
    while (finished < count) {
        while (running < workers && started < count) {
            int fd[2];
            if (pipe(fd)) break;
            fflush(stdout);
            pid_t child = fork();
            if (child == 0) {
                close(fd[0]);
                SDL_Init(SDL_INIT_VIDEO);
                game_t g = play(games[started].faction[0], games[started].faction[1], games[started].seed,
                                max_minutes);
                ssize_t wrote = write(fd[1], &g, sizeof(g));
                fflush(stdout);
                _exit(wrote == (ssize_t)sizeof(g) ? 0 : 1);
            }
            close(fd[1]);
            pid[started] = child;
            pipes[started] = fd[0];
            started++;
            running++;
        }
        int status;
        pid_t done = wait(&status);
        if (done < 0) break;
        for (int i = 0; i < started; ++i) {
            if (pid[i] != done) continue;
            game_t g = {0};
            ssize_t got = read(pipes[i], &g, sizeof(g));
            close(pipes[i]);
            int fa = games[i].faction[0], fb = games[i].faction[1], seed = games[i].seed;
            games[i] = got == (ssize_t)sizeof(g) ? g : (game_t){.seed = seed, .winner = -1};
            if (!games[i].ok) { games[i].faction[0] = fa; games[i].faction[1] = fb; }
            finished++;
            running--;
            const game_t *r = &games[i];
            printf("  %-11s vs %-11s seed %2d: %-11s %s at %2d min (worth %6d / %6d)\n", NAME[r->faction[0]],
                   NAME[r->faction[1]], r->seed, r->winner < 0 ? "draw" : NAME[r->winner],
                   r->timed_out ? "on points" : "wins", r->minutes, r->worth[0], r->worth[1]);
        }
    }
}

static int env_int(const char *name, int fallback) {
    const char *v = getenv(name);
    return v && atoi(v) > 0 ? atoi(v) : fallback;
}

int main(int argc, char **argv) {
    bool quick = argc > 1 && !strcmp(argv[1], "--quick");
    int seeds = quick ? 1 : env_int("OPEN_RTS_BALANCE_SEEDS", 12);
    int minutes = quick ? 6 : env_int("OPEN_RTS_BALANCE_MINUTES", 35);
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    int workers = env_int("OPEN_RTS_BALANCE_JOBS", cpus > 1 ? (int)cpus : 1);
    static game_t games[MAX_GAMES];
    int count = 0;
    for (int a = 0; a < RF_FACTION_COUNT; ++a)
        for (int b = a + 1; b < RF_FACTION_COUNT; ++b)
            for (int s = 0; s < seeds && count < MAX_GAMES; ++s)
                games[count++] = (game_t){.faction = {a, b}, .seed = s, .winner = -1};
    printf("rustfront balance: %d games, %d seeds a pairing, up to %d minutes each, %d at once\n", count, seeds,
           minutes, workers);
    play_all(games, count, minutes, workers);
    for (int i = 0; i < count; ++i) CHECK(games[i].ok);
    if (quick) {
        puts("PASS: the computer plays every faction pairing on the duel map");
        return 0;
    }
    /* Per pairing: each faction's share of the games (a draw is half a win to each). */
    bool balanced = true;
    int wins_total[RF_FACTION_COUNT] = {0}, games_total[RF_FACTION_COUNT] = {0};
    for (int a = 0; a < RF_FACTION_COUNT; ++a)
        for (int b = a + 1; b < RF_FACTION_COUNT; ++b) {
            int played = 0, wins_a = 0, wins_b = 0, draws = 0, minutes_sum = 0;
            for (int i = 0; i < count; ++i) {
                const game_t *g = &games[i];
                bool pair = (g->faction[0] == a && g->faction[1] == b) || (g->faction[0] == b && g->faction[1] == a);
                if (!pair) continue;
                played++;
                minutes_sum += g->minutes;
                if (g->winner == a) wins_a++;
                else if (g->winner == b) wins_b++;
                else draws++;
            }
            if (!played) continue;
            int share_a = (200 * wins_a + 100 * draws) / (2 * played), share_b = 100 - share_a;
            printf("%-11s %2d  %-11s %2d  draws %2d  -> %3d %% / %3d %%, %d min a game\n", NAME[a], wins_a,
                   NAME[b], wins_b, draws, share_a, share_b, minutes_sum / played);
            balanced &= share_a <= 65 && share_b <= 65;
            wins_total[a] += 2 * wins_a + draws;
            wins_total[b] += 2 * wins_b + draws;
            games_total[a] += 2 * played;
            games_total[b] += 2 * played;
        }
    for (int f = 0; f < RF_FACTION_COUNT; ++f)
        if (games_total[f]) printf("%-11s overall %3d %%\n", NAME[f], 100 * wins_total[f] / games_total[f]);
    CHECK(balanced);
    puts("PASS: no faction wins more than 65 % against another");
    return 0;
}
