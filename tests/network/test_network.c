#define _POSIX_C_SOURCE 200809L
#include "engine.h"
#include "t_local.h"
#include "info.h"
#include "dark-colony.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

enum { TESTTICS = 550 };
typedef struct { int tics, failed; uint32_t hashes[TESTTICS]; } result_t;
typedef enum { DIRECT, LOSSY, DUPLICATED, MISMATCH, DESYNC, QUIT, MODEL,
               HOSTED, HOSTED_MAP, HOSTED_LOSSY, HOSTED_MIXED, MENU_HOSTED,
               SPEED_MISMATCH } testmode_t;

static mobj_t *actors[MAXPLAYERS];

static void init_world(void) {
    G_InitGame();
    level = (level_t){ .width = 32, .height = 32 };
    P_InitThinkers();
    gameinfo = NULL;
    for (int i = 0; i < MAXPLAYERS; ++i) {
        actors[i] = spawn_mobj_fixture((mobj_t){ .hp = 100, .max_hp = 100,
            .owner = (uint8_t)i, .team = (uint8_t)i, .speed = 4, .radius = 0.25f,
            .allegiance = i ? ALLEGIANCE_ENEMY : ALLEGIANCE_PLAYER,
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
    cmd = (ticcmd_t){.order = TC_PURCHASE, .product = 81};
    G_RunTiccmd(1,&cmd);
    assert(level.player_resources[1][0] == 10000 - product->cost);
    assert(level.purchases[1][2].selected == 1 && !level.purchases[0][2].selected);
    uint32_t reserved_hash = G_Consistency();
    cmd.target = 1;
    G_RunTiccmd(0,&cmd);
    assert(G_Consistency() == reserved_hash);
    G_RunTiccmd(1,&cmd);
    assert(level.player_resources[1][0] == 10000 && !level.purchases[1][2].selected);
    cmd.target = 0;
    G_RunTiccmd(1,&cmd);
    G_RunTiccmd(1,&(ticcmd_t){.order = TC_SUBMIT});
    assert(level.player_resources[1][0] == 10000 - product->cost);
    assert(!level.purchases[1][2].selected);
    P_FreeLevel(&level);
    G_InitGame(); P_InitThinkers();
    level.width = level.height = 32;
    producer = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){16,16},0),MT_EXCOPOD);
    assert(producer);
    producer->owner = producer->team = 1;
    level.player_resources[1][0] = 10000;
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

static void session_tests(void) {
    struct sockaddr_in address;
    int socket = bound_socket(&address);
    char port[16], server[64];
    snprintf(port, sizeof(port), "%d", ntohs(address.sin_port));
    snprintf(server, sizeof(server), "127.0.0.1:%s", port);
    close(socket);
    fflush(NULL);
    pid_t host = fork();
    assert(host >= 0);
    if (!host) {
        char *args[] = {"test", "--host", "--port", port, "--dup", "3", "--extratic", NULL};
        int argc = 7;
        char map[512] = "SCENARIO/MPLAYER/J2PLAY01.MAP";
        assert(I_InitNetwork(&argc, args));
        assert(argc == 1 && !I_NetJoining());
        D_SetGameSpeed(175);
        assert(I_StartNetGame("dark-colony", map, sizeof(map)));
        I_ShutdownNetwork();
        _exit(0);
    }
    char *badargs[] = {"test", "--join", server, NULL};
    int argc = 3;
    char map[512] = "";
    assert(I_InitNetwork(&argc, badargs));
    assert(!I_StartNetGame("wrong-game", map, sizeof(map)));
    assert(strstr(neterror, "mismatch"));
    char *args[] = {"test", "--join", server, NULL};
    argc = 3;
    D_SetGameSpeed(35);
    assert(I_InitNetwork(&argc, args));
    assert(argc == 1 && I_NetJoining());
    assert(game_speed == 35);
    assert(I_StartNetGame("dark-colony", map, sizeof(map)));
    assert(!strcmp(map, "SCENARIO/MPLAYER/J2PLAY01.MAP"));
    assert(doomcom->consoleplayer == 1 && doomcom->numplayers == 2);
    assert(doomcom->ticdup == 3 && doomcom->extratics == 1);
    assert(game_speed == 175);
    D_QuitNetGame();
    D_SetGameSpeed(100);
    int status;
    assert(waitpid(host, &status, 0) == host && WIFEXITED(status) && !WEXITSTATUS(status));
    char *invalid[] = {"test", "--host", "--join", server, NULL};
    argc = 4;
    assert(!I_InitNetwork(&argc, invalid));
    D_QuitNetGame();
    puts("PASS: session rejects another game, assigns slots, inherits host map, speed and timing, validates switches");
}

static void lan_tests(void) {
    int control[2], ready[2];
    assert(pipe(control) == 0 && pipe(ready) == 0);
    fflush(NULL);
    pid_t host = fork();
    assert(host >= 0);
    if (!host) {
        close(control[1]); close(ready[0]);
        assert(fcntl(control[0], F_SETFL, O_NONBLOCK) == 0);
        bool hosted = I_HostNetGame("dark-colony", "LAN test", "SCENARIO/MPLAYER/D2PLAY01.MAP", 3);
        if (!hosted) fprintf(stderr, "LAN host: %s\n", neterror);
        assert(hosted);
        assert(write(ready[1], "R", 1) == 1);
        char map[512], command;
        uint64_t deadline = SDL_GetTicks64() + 15000;
        bool finished = false;
        while (SDL_GetTicks64() < deadline) {
            int status = I_PollNetGame(map, sizeof(map));
            assert(status >= 0);
            if (read(control[0], &command, 1) == 1) {
                if (command == 'C') {
                    assert(I_NetPlayerCount() == 2);
                    I_CancelNetGame();
                    assert(I_HostNetGame("dark-colony", "LAN test", "SCENARIO/MPLAYER/D2PLAY01.MAP", 2));
                    assert(write(ready[1], "R", 1) == 1);
                } else if (command == 'L') {
                    assert(I_NetPlayerCount() == 1); /* Cancelled join freed its slot. */
                    assert(write(ready[1], "R", 1) == 1);
                } else if (command == 'Q') { finished = true; break; }
            }
            SDL_Delay(1);
        }
        assert(finished);
        assert(!strcmp(map, "SCENARIO/MPLAYER/D2PLAY01.MAP") && doomcom->numplayers == 2);
        I_ShutdownNetwork();
        _exit(0);
    }
    close(control[0]); close(ready[1]);
    char byte;
    assert(read(ready[0], &byte, 1) == 1);
    assert(I_OpenNetBrowser("wrong-game"));
    I_QueryNetGames("127.0.0.1");
    uint64_t deadline = SDL_GetTicks64() + 100;
    int count = 0;
    while (SDL_GetTicks64() < deadline) { I_NetGames(&count); SDL_Delay(1); }
    assert(count == 0);
    assert(I_OpenNetBrowser("dark-colony"));
    I_QueryNetGames("127.0.0.1");
    deadline = SDL_GetTicks64() + 3000;
    const netgame_t *games;
    do { games = I_NetGames(&count); SDL_Delay(1); } while (!count && SDL_GetTicks64() < deadline);
    assert(count == 1 && !netgame);
    assert(!strcmp(games[0].name, "LAN test") && games[0].players == 1 && games[0].capacity == 3);
    assert(!strcmp(games[0].map, "SCENARIO/MPLAYER/D2PLAY01.MAP"));
    /* Repeated offers update a row rather than creating duplicates. */
    int before = count;
    for (int i = 0; i < 10; ++i) { I_QueryNetGames("127.0.0.1"); I_NetGames(&count); SDL_Delay(2); }
    assert(count == before);
    assert(I_JoinNetGame("dark-colony", "127.0.0.1"));
    char map[512] = "";
    for (int i = 0; i < 100; ++i) { assert(I_PollNetGame(map, sizeof(map)) == 0); SDL_Delay(1); }
    I_CancelNetGame();
    assert(!netgame);
    SDL_Delay(20);
    assert(write(control[1], "L", 1) == 1);
    assert(read(ready[0], &byte, 1) == 1);
    assert(I_JoinNetGame("dark-colony", "127.0.0.1"));
    for (int i = 0; i < 100; ++i) { assert(I_PollNetGame(map, sizeof(map)) == 0); SDL_Delay(1); }
    assert(write(control[1], "C", 1) == 1);
    assert(read(ready[0], &byte, 1) == 1);
    deadline = SDL_GetTicks64() + 3000;
    int status;
    do { status = I_PollNetGame(map, sizeof(map)); SDL_Delay(1); } while (!status && SDL_GetTicks64() < deadline);
    assert(status == -1 && strstr(neterror, "cancelled"));
    I_CancelNetGame();
    assert(I_JoinNetGame("dark-colony", "127.0.0.1"));
    deadline = SDL_GetTicks64() + 3000;
    do { status = I_PollNetGame(map, sizeof(map)); SDL_Delay(1); } while (!status && SDL_GetTicks64() < deadline);
    assert(status == 1 && doomcom->consoleplayer == 1 && doomcom->numplayers == 2);
    assert(!strcmp(map, "SCENARIO/MPLAYER/D2PLAY01.MAP"));
    assert(write(control[1], "Q", 1) == 1);
    int result;
    assert(waitpid(host, &result, 0) == host && WIFEXITED(result) && !WEXITSTATUS(result));
    close(control[1]); close(ready[0]);
    D_QuitNetGame();
    puts("PASS: LAN discovery, game filtering, offer deduplication, join/host cancellation, rehost, asynchronous map/slot agreement");
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
    bool native_map = mode == HOSTED_MAP || mode == HOSTED_MIXED || mode == MENU_HOSTED;
    const char *chosen_map = mode == HOSTED_MIXED || mode == MENU_HOSTED ? "SCENARIO/MPLAYER/D2PLAY01.MAP" :
                                                  "SCENARIO/MPLAYER/J4PLAY01.MAP";
    char map[512];
    snprintf(map, sizeof(map), "%s", chosen_map);
    bool hosted = mode == HOSTED || native_map || mode == HOSTED_LOSSY;
    D_SetGameSpeed(hosted ? (player ? 70 : 150) :
                   mode == SPEED_MISMATCH && player == 1 ? 150 : 100);
    if (hosted) {
        char server[64], playercount[8];
        snprintf(server, sizeof(server), "127.0.0.1:%d",
                 ntohs((mode == HOSTED_LOSSY ? proxies : addresses)[0].sin_port));
        snprintf(playercount, sizeof(playercount), "%d", players);
        char *hostargs[] = {"test", "--host", "--port", port, "--players", playercount, NULL};
        char *joinargs[] = {"test", "--join", server, "--port", port, NULL};
        int session_argc = player ? (mode == HOSTED_LOSSY ? 5 : 3) : 6;
        if (player) map[0] = '\0';
        if (mode == MENU_HOSTED) {
            assert(player ? I_JoinNetGame("dark-colony", "127.0.0.1") :
                I_HostNetGame("dark-colony", "Menu match", map, players));
            int status;
            uint64_t deadline = SDL_GetTicks64() + 3000;
            do { status = I_PollNetGame(map, sizeof(map)); SDL_Delay(1); }
            while (!status && SDL_GetTicks64() < deadline);
            assert(status == 1 && I_NetMenuSession());
        } else assert(I_InitNetwork(&session_argc, player ? joinargs : hostargs));
        assert(I_StartNetGame("dark-colony", map, sizeof(map)));
        assert(game_speed == 150);
        assert(!I_NetMenuSession());
        assert(!strcmp(map, chosen_map));
        assert(doomcom->numplayers == players);
        player = doomcom->consoleplayer;
    } else
    assert(I_InitNetwork(&argc, argv));
    RtsGameModel *model = NULL;
    if (mode == MODEL || native_map) {
        model = rts_game_model_create();
        RtsGameModelConfig config = {.data_root = "data/DCOLONY",
            .map_path = native_map ? map : "SCENARIO/HUMAN/HUMAN02.MAP"};
        assert(model && rts_game_model_load(model, &config));
        /* Explicit test units: campaign starting forces need not cover all peers. */
        for (int p = 0; mode == MODEL && p < players; ++p) {
            actors[p] = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){32.5f + p * 2, 32.5f}, 0), MT_TROOPER);
            assert(actors[p]);
            actors[p]->owner = actors[p]->team = (uint8_t)p;
        }
        if (native_map) {
            memset(actors, 0, sizeof(actors));
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
                mobj_t *u = (mobj_t *)th;
                if ((u->type_id == MT_EXCOPOD || u->type_id == MT_ALIEN_MINDHIVE) && u->team < players) {
                    assert(u->owner == u->team);
                    actors[u->owner] = u;
                }
            }
            for (int p = 0; p < players; ++p) {
                assert(actors[p] && level.player_resources[p][0] == 1500);
                for (int q = 0; q < players; ++q)
                    assert(P_IsAlly(actors[p], actors[q]) == (p == q));
            }
        }
        P_UpdateSight();
    } else init_world();
    D_CheckNetGame(G_Consistency() + (mode == MISMATCH && player == 1));
    uint32_t first_production_id = level.next_mobj_id;
    if (native_map) {
        /* A real native base for every slot: queue a Barracks purchase through
         * the same command API as the sidebar, without granting test money. */
        assert(G_BuildOrder(actors[player], actors[player]->type_id == MT_EXCOPOD ? 80 : 41));
    }
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
        waypoints_t path = {.points = {{4 + player*6,24},{4 + player*6,12}},
                            .count = 2,.mode = WP_BACKTRACK};
        assert(G_PathOrder(actors,MAXPLAYERS,&path));
    }
    result_t result = {0};
    int end = mode == MISMATCH || mode == SPEED_MISMATCH || mode == DESYNC || mode == MODEL || mode == HOSTED_MAP || mode == MENU_HOSTED ? 90 : TESTTICS;
    if (mode == QUIT && player == 1) end = 40;
    bool trained = false;
    uint64_t deadline = SDL_GetTicks64() + 45000;
    while (gametic < end && !neterror[0] && SDL_GetTicks64() < deadline) {
        if (model) {
            if (mode == HOSTED_MIXED && !trained) {
                int ui_id = player ? 48 : 89;
                const StaticProductDefinition *product = G_ModelProductByUIId(model, ui_id);
                if (G_ModelProductAvailable(model, player, product) && G_FindProducer(player, product)) {
                    RtsGameCommand train = {.kind = RTS_GAME_COMMAND_ACTIVATE_UI_BUTTON,
                        .data.activate_ui_button.ui_id = ui_id};
                    assert(rts_game_model_command(model, &train));
                    trained = true;
                }
            }
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
    if (mode != MISMATCH && mode != SPEED_MISMATCH && mode != DESYNC) assert(gametic == end && !result.failed);
    if (mode == SPEED_MISMATCH) {
        assert(result.failed && gametic == 0);
        assert(strstr(neterror, "speed mismatch") || strstr(neterror, "killed"));
    }
    if (native_map) {
        bool barracks[MAXPLAYERS] = {0};
        bool troops[MAXPLAYERS] = {0};
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *u = (mobj_t *)th;
            if ((u->type_id == MT_BRRKPOD || u->type_id == MT_ALIEN_WARHIVE) && u->owner < players)
                barracks[u->owner] = true;
            if (!u->remove && u->owner < players && u->id >= first_production_id &&
                u->type_id == (u->owner ? MT_GREY : MT_TROOPER)) troops[u->owner] = true;
        }
        for (int p = 0; p < players; ++p) {
            assert(barracks[p]);
            assert(level.player_resources[p][0] == (mode == HOSTED_MIXED ? 150 : 500));
            if (mode == HOSTED_MIXED) assert(trained && troops[p]);
        }
    }
    if (hosted && !player) {
        uint64_t until = SDL_GetTicks64() + 2000;
        bool waiting = true;
        while (waiting && SDL_GetTicks64() < until) {
            NetUpdate();
            waiting = false;
            for (int p = 1; p < players; ++p) waiting |= playeringame[p];
            SDL_Delay(1);
        }
    }
    D_QuitNetGame();
    assert(write(output, &result, sizeof(result)) == sizeof(result));
    close(output);
    if (model) rts_game_model_destroy(model);
    else P_FreeLevel(&level);
    /* SDL's inherited timer thread cannot be joined after fork on macOS.
     * The child owns no display; _exit releases its process resources. */
    _exit(0);
}

static void network_test(testmode_t mode, int players) {
    bool lossy = mode == LOSSY || mode == HOSTED_LOSSY;
    struct sockaddr_in addresses[MAXPLAYERS], proxies[2];
    int sockets[MAXPLAYERS], proxy[2] = {-1,-1}, pipes[MAXPLAYERS][2];
    pid_t children[MAXPLAYERS];
    for (int i = 0; i < players; ++i) {
        sockets[i] = bound_socket(&addresses[i]);
        assert(pipe(pipes[i]) == 0);
    }
    if (lossy)
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
            if (lossy) { close(proxy[0]); close(proxy[1]); }
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
        if (lossy) {
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
    if (mode == MISMATCH || mode == SPEED_MISMATCH || mode == DESYNC) {
        assert(results[0].failed || results[1].failed);
        assert(results[0].tics < 90 && results[1].tics < 90);
    } else {
        for (int p = 1; p < players; ++p)
            for (int tic = 0; tic < results[p].tics; ++tic)
                assert(results[p].hashes[tic] == results[0].hashes[tic]);
    }
    if (lossy) {
        assert(dropped && duplicated && reordered);
        close(proxy[0]); close(proxy[1]);
    }
    printf("PASS: network mode=%d players=%d tics=%d (dropped=%d duplicated=%d reordered=%d)\n",
           mode, players, results[0].tics, dropped, duplicated, reordered);
}

int main(int argc, char **argv) {
    assert(SDL_Init(SDL_INIT_TIMER) == 0);
    if (argc == 2 && !strcmp(argv[1], "--lan")) {
        lan_tests();
        network_test(MENU_HOSTED, 2);
        SDL_Quit();
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--model")) {
        network_test(MODEL, 2);
        SDL_Quit();
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--hosted")) {
        session_tests();
        network_test(HOSTED, 4);
        network_test(HOSTED_MAP, 4);
        network_test(HOSTED_MIXED, 2);
        network_test(HOSTED_LOSSY, 2);
        SDL_Quit();
        return 0;
    }
    command_tests();
    lan_tests();
    session_tests();
    network_test(DIRECT, 4);
    network_test(LOSSY, 2);
    network_test(DUPLICATED, 2);
    network_test(MISMATCH, 2);
    network_test(SPEED_MISMATCH, 2);
    network_test(DESYNC, 2);
    network_test(QUIT, 2);
    network_test(MODEL, 2);
    network_test(HOSTED, 4);
    network_test(HOSTED_MAP, 4);
    network_test(HOSTED_MIXED, 2);
    network_test(MENU_HOSTED, 2);
    network_test(HOSTED_LOSSY, 2);
    SDL_Quit();
    return 0;
}
