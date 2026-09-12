#define _POSIX_C_SOURCE 200809L
#include "d_net.h"
#include "mobj_test.h"
#include "p_local.h"
#include "info.h"
#include "p_ai.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

enum { TESTTICS = 550 };
typedef struct { int tics, failed; uint32_t hashes[TESTTICS]; } result_t;
typedef enum { DIRECT, LOSSY, DUPLICATED, MISMATCH, DESYNC, QUIT, MODEL } testmode_t;

static mobj_t *actors[MAXPLAYERS];

static void init_world(void) {
    G_InitGame();
    level = (level_t){ .width = 32, .height = 32 };
    P_InitThinkers();
    gameinfo = NULL;
    for (int i = 0; i < MAXPLAYERS; ++i) {
        actors[i] = spawn_mobj_fixture((mobj_t){ .hp = 100, .max_hp = 100,
            .owner = (uint8_t)i, .team = (uint8_t)i, .speed = 4, .radius = 0.25f,
            .traits = MF_MOBILE | MF_SELECTABLE,
            .core.position = fixed3_from_fvec2((fvec2_t){4.5f + i * 6, 4.5f}, 0),
            .harvest.target = -1 });
    }
}

static void command_tests(void) {
    init_world();
    int argc = 1; char *argv[] = {"test", NULL};
    assert(I_InitNetwork(&argc, argv));
    D_CheckNetGame(G_Consistency());
    mobj_t *unit = actors[0];
    P_MobjSetSelected(unit, true);
    assert(G_SelectedTiccmd(TC_MOVE, actors, MAXPLAYERS, (fvec2_t){12.5f, 12.5f}, 0));
    assert(!unit->movement.order_id); /* Input never changes simulation ahead of its tic. */
    P_MobjSetSelected(unit, false);
    P_MobjSetSelected(actors[1], true);
    ticcmd_t cmd;
    G_BuildTiccmd(&cmd);
    G_RunTiccmd(1, &cmd);
    assert(!unit->movement.order_id); /* Can't command another owner's unit. */
    G_RunTiccmd(0, &cmd);
    assert(unit->movement.order_id && !actors[1]->movement.order_id);
    assert(fvec2_near(unit->movement.goal, (fvec2_t){12.5f, 12.5f}, 0.001f));
    uint32_t hash = G_Consistency();
    P_MobjSetSelected(unit, true);
    assert(hash == G_Consistency());
    cmd.order = TC_STOP;
    G_RunTiccmd(0, &cmd);
    assert(!unit->movement.order_id && unit->movement.order_arrived);

    unit->traits |= MF_ATTACK | MF_HARVESTER;
    cmd.order = TC_ATTACK; cmd.target = actors[1]->id;
    G_RunTiccmd(0, &cmd);
    assert(unit->attack.target == actors[1]);
    level.resource_vents = calloc(1, sizeof(*level.resource_vents));
    assert(level.resource_vents);
    level.resource_vent_count = 1;
    level.resource_vents[0] = (resourcevent_t){ .cell = {12,12}, .attachment = {12.5f,12.5f},
                                             .active = true, .rate = 1, .amount = 100 };
    cmd.order = TC_HARVEST;
    G_RunTiccmd(0, &cmd);
    assert(unit->harvest.target == 0 && unit->harvest.phase && !unit->attack.target);
    cmd.order = TC_STOP;
    G_RunTiccmd(0, &cmd);
    assert(unit->harvest.target == -1 && unit->harvest.phase == 0);

    /* Both owners are human; AI must not issue orders for either. */
    netgame = true; doomcom->numplayers = 2;
    unit->traits |= MF_RESOURCE_BASE;
    actors[1]->traits |= MF_RESOURCE_BASE | MF_ATTACK;
    actors[1]->movement.order_arrived = true;
    actors[2]->allegiance = ALLEGIANCE_ENEMY;
    AiContext ai;
    P_AiInit(&ai);
    uint32_t before_ai = G_Consistency();
    P_AiTick(&ai, &level, actors, MAXPLAYERS, NULL, 1000);
    assert(G_Consistency() == before_ai);
    netgame = false; doomcom->numplayers = 1;

    assert(G_QueueTiccmd(&cmd));
    P_RemoveMobj(unit);
    P_RunThinkers();
    actors[0] = NULL;
    mobj_t *replacement = spawn_mobj_fixture((mobj_t){.hp = 100, .traits = MF_MOBILE});
    replacement->movement.order_id = 123;
    G_BuildTiccmd(&cmd);
    G_RunTiccmd(0, &cmd);
    assert(replacement->movement.order_id == 123); /* A stale ID never names a new unit. */
    maketic = 255; assert(ExpandTics(0) == 256);
    maketic = 256; assert(ExpandTics(255) == 255);
    D_QuitNetGame();
    P_FreeLevel(&level);

    /* Native DC player production is charged at command execution, once. */
    G_InitGame(); P_InitThinkers();
    level.width = level.height = 32;
    mobj_t *producer = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){16,16}, 0), MT_EXCOPOD);
    assert(producer);
    producer->owner = producer->team = 1;
    level.player_resources[1][0] = 10000;
    const StaticProductDefinition *product = G_ModelProductByUIId(NULL, 81);
    assert(product);
    cmd = (ticcmd_t){ .order = TC_BUILD, .product = 81, .count = 1, .units = {producer->id} };
    G_RunTiccmd(0, &cmd);
    assert(level.player_resources[1][0] == 10000);
    G_RunTiccmd(1, &cmd);
    assert(level.player_resources[1][0] == 10000 - product->cost);
    assert(((mobj_t *)thinkercap.prev)->owner == 1);
    P_FreeLevel(&level);
    puts("PASS: deferred orders, selection independence, owner checks, stable IDs and production");
}

static int bound_socket(struct sockaddr_in *address) {
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    assert(fd >= 0);
    *address = (struct sockaddr_in){.sin_family = AF_INET, .sin_addr.s_addr = inet_addr("127.0.0.1")};
    assert(bind(fd, (struct sockaddr *)address, sizeof(*address)) == 0);
    socklen_t size = sizeof(*address);
    assert(getsockname(fd, (struct sockaddr *)address, &size) == 0);
    assert(fcntl(fd, F_SETFL, O_NONBLOCK) == 0);
    return fd;
}

static void peer(int player, int players, const struct sockaddr_in *addresses,
                 const struct sockaddr_in *proxies, testmode_t mode, int output) {
    assert(SDL_Init(SDL_INIT_TIMER) == 0);
    char port[16], playerarg[8], hosts[MAXPLAYERS][64];
    snprintf(port, sizeof(port), "%d", ntohs(addresses[player].sin_port));
    snprintf(playerarg, sizeof(playerarg), "%d", player + 1);
    char *argv[16] = {"test", "--port", port, "--dup", mode == DUPLICATED ? "3" : "1",
                      "--extratic", "--net", playerarg};
    int argc = 8;
    for (int p = 0; p < players; ++p) {
        if (p == player) continue;
        snprintf(hosts[p], sizeof(hosts[p]), "127.0.0.1:%d",
                 ntohs((mode == LOSSY ? proxies : addresses)[p].sin_port));
        argv[argc++] = hosts[p];
    }
    argv[argc] = NULL;
    assert(I_InitNetwork(&argc, argv));
    RtsGameModel *model = NULL;
    if (mode == MODEL) {
        model = rts_game_model_create();
        RtsGameModelConfig config = {.data_root = "data/DCOLONY", .map_path = "SCENARIO/HUMAN/HUMAN02.MAP"};
        assert(model && rts_game_model_load(model, &config));
        /* Explicit test units: campaign starting forces need not cover all peers. */
        for (int p = 0; p < players; ++p) {
            actors[p] = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){32.5f + p * 2, 32.5f}, 0), MT_TROOPER);
            assert(actors[p]);
            actors[p]->owner = actors[p]->team = (uint8_t)p;
        }
        P_UpdateSight();
    } else init_world();
    D_CheckNetGame(G_Consistency() + (mode == MISMATCH && player == 1));
    /* Different local selection and render cadence must not affect the world. */
    if (model) {
        RtsGameCommand select = {.kind = RTS_GAME_COMMAND_SELECT_ALL_PLAYER_UNITS};
        RtsGameCommand move = {.kind = RTS_GAME_COMMAND_MOVE_SELECTED,
                               .data.move_selected.target = {32.5f, 32.5f}};
        assert(rts_game_model_command(model, &select));
        assert(rts_game_model_command(model, &move));
    } else {
        P_MobjSetSelected(actors[player], true);
        assert(G_SelectedTiccmd(TC_MOVE, actors, MAXPLAYERS,
                               (fvec2_t){4.5f + player * 6, 24.5f}, 0));
    }
    result_t result = {0};
    int end = mode == MISMATCH || mode == DESYNC || mode == MODEL ? 90 : TESTTICS;
    if (mode == QUIT && player == 1) end = 40;
    uint64_t deadline = SDL_GetTicks64() + 45000;
    while (gametic < end && !neterror[0] && SDL_GetTicks64() < deadline) {
        if (model) {
            int before = gametic;
            if (!rts_game_model_tick(model, FIXED_DT)) break;
            if (gametic > before) result.hashes[before] = G_Consistency();
            SDL_Delay(player + 1);
            continue;
        }
        int count = TryRunTics();
        if (count > end - gametic) count = end - gametic;
        while (count-- > 0) {
            if (!D_RunTiccmds()) break;
            P_Ticker();
            if (mode == DESYNC && gametic == 25 && player == 1) --actors[0]->hp;
            result.hashes[gametic++] = G_Consistency();
            NetUpdate();
        }
        SDL_Delay(player + 1);
    }
    result.tics = gametic;
    result.failed = neterror[0] != '\0';
    if (neterror[0]) fprintf(stderr, "peer %d: %s\n", player + 1, neterror);
    if (mode != MISMATCH && mode != DESYNC) assert(gametic == end && !result.failed);
    D_QuitNetGame();
    assert(write(output, &result, sizeof(result)) == sizeof(result));
    close(output);
    if (model) rts_game_model_destroy(model);
    else P_FreeLevel(&level);
    SDL_Quit();
    _exit(0);
}

static void network_test(testmode_t mode, int players) {
    struct sockaddr_in addresses[MAXPLAYERS], proxies[2];
    int sockets[MAXPLAYERS], proxy[2] = {-1,-1}, pipes[MAXPLAYERS][2];
    pid_t children[MAXPLAYERS];
    for (int i = 0; i < players; ++i) {
        sockets[i] = bound_socket(&addresses[i]);
        assert(pipe(pipes[i]) == 0);
    }
    if (mode == LOSSY)
        for (int i = 0; i < 2; ++i) proxy[i] = bound_socket(&proxies[i]);
    /* Release the reserved peer ports before starting the children. */
    for (int i = 0; i < players; ++i) close(sockets[i]);
    fflush(NULL);
    for (int i = 0; i < players; ++i) {
        children[i] = fork(); assert(children[i] >= 0);
        if (!children[i]) {
            for (int p = 0; p < players; ++p) {
                close(pipes[p][0]); if (p != i) close(pipes[p][1]);
            }
            if (mode == LOSSY) { close(proxy[0]); close(proxy[1]); }
            peer(i, players, addresses, proxies, mode, pipes[i][1]);
        }
        close(pipes[i][1]);
    }
    bool finished[MAXPLAYERS] = {0};
    int remaining = players, sequence[2] = {0}, dropped = 0, duplicated = 0, reordered = 0;
    uint8_t delayed[2][65536];
    ssize_t delayed_size[2] = {0};
    uint64_t deadline = SDL_GetTicks64() + 50000;
    while (remaining && SDL_GetTicks64() < deadline) {
        if (mode == LOSSY) {
            for (int to = 0; to < 2; ++to) {
                uint8_t wire[65536];
                ssize_t size = recv(proxy[to], wire, sizeof(wire), 0);
                if (size <= 0) continue;
                int from = 1 - to;
                int n = ++sequence[to];
                if (n % 7 == 0 || (n >= 100 && n < 106)) { ++dropped; continue; }
                if (n % 13 == 0 && !delayed_size[to]) {
                    memcpy(delayed[to], wire, (size_t)size); delayed_size[to] = size;
                    continue;
                }
                assert(sendto(proxy[from], wire, (size_t)size, 0,
                    (struct sockaddr *)&addresses[to], sizeof(addresses[to])) == size);
                if (n % 11 == 0) {
                    ++duplicated;
                    sendto(proxy[from], wire, (size_t)size, 0,
                           (struct sockaddr *)&addresses[to], sizeof(addresses[to]));
                }
                /* Corrupt/truncated packets from a known endpoint must be ignored. */
                if (n % 17 == 0) {
                    wire[0] ^= 1;
                    sendto(proxy[from], wire, (size_t)size - 1, 0,
                           (struct sockaddr *)&addresses[to], sizeof(addresses[to]));
                }
                if (delayed_size[to]) {
                    ++reordered;
                    sendto(proxy[from], delayed[to], (size_t)delayed_size[to], 0,
                           (struct sockaddr *)&addresses[to], sizeof(addresses[to]));
                    delayed_size[to] = 0;
                }
            }
        }
        for (int i = 0; i < players; ++i) {
            int status;
            if (!finished[i] && waitpid(children[i], &status, WNOHANG) == children[i]) {
                assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
                finished[i] = true; --remaining;
            }
        }
        SDL_Delay(1);
    }
    if (remaining) {
        for (int i = 0; i < players; ++i) if (!finished[i]) kill(children[i], SIGKILL);
        assert(!"network test timeout");
    }
    result_t results[MAXPLAYERS];
    for (int i = 0; i < players; ++i) {
        assert(read(pipes[i][0], &results[i], sizeof(results[i])) == sizeof(results[i]));
        close(pipes[i][0]);
    }
    if (mode == MISMATCH || mode == DESYNC) {
        assert(results[0].failed || results[1].failed);
        assert(results[0].tics < 90 && results[1].tics < 90);
    } else {
        for (int p = 1; p < players; ++p)
            for (int tic = 0; tic < results[p].tics; ++tic)
                assert(results[p].hashes[tic] == results[0].hashes[tic]);
    }
    if (mode == LOSSY) {
        assert(dropped && duplicated && reordered);
        close(proxy[0]); close(proxy[1]);
    }
    printf("PASS: network mode=%d players=%d tics=%d (dropped=%d duplicated=%d reordered=%d)\n",
           mode, players, results[0].tics, dropped, duplicated, reordered);
}

int main(int argc, char **argv) {
    assert(SDL_Init(SDL_INIT_TIMER) == 0);
    if (argc == 2 && !strcmp(argv[1], "--model")) {
        network_test(MODEL, 2);
        SDL_Quit();
        return 0;
    }
    command_tests();
    network_test(DIRECT, 4);
    network_test(LOSSY, 2);
    network_test(DUPLICATED, 2);
    network_test(MISMATCH, 2);
    network_test(DESYNC, 2);
    network_test(QUIT, 2);
    network_test(MODEL, 2);
    SDL_Quit();
    return 0;
}
