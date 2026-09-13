#define _POSIX_C_SOURCE 200112L
#include "d_net.h"
#include <arpa/inet.h>
#include <SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int insocket = -1;
static struct sockaddr_in sendaddress[MAXNETNODES];
static doomcom_t communication;
doomcom_t *doomcom = &communication;

/* Explicit encoding: no host padding, pointers, enums or floats on the wire. */
#define WIRECMD (28 + 4 * MAXCOMMANDUNITS)
#define WIREMAX (8 + BACKUPTICS * WIRECMD)
static void put32(uint8_t *p, uint32_t v) {
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}
static uint32_t checksum(const uint8_t *p, size_t size) {
    uint32_t sum = 0x1234567;
    for (size_t i = 4; i < size; ++i) sum += p[i] * (uint32_t)(i - 3);
    return sum & NCMD_CHECKSUM;
}

static size_t encode(uint8_t *wire) {
    doomdata_t *packet = &doomcom->data;
    wire[4] = packet->retransmitfrom; wire[5] = packet->starttic;
    wire[6] = packet->player; wire[7] = packet->numtics;
    size_t size = 8;
    for (int i = 0; i < packet->numtics; ++i) {
        const ticcmd_t *c = &packet->cmds[i];
        uint8_t *p = wire + size;
        put32(p, c->consistancy); put32(p + 4, c->order);
        put32(p + 8, (uint32_t)c->position.x); put32(p + 12, (uint32_t)c->position.y);
        put32(p + 16, c->target); put32(p + 20, (uint32_t)c->product);
        put32(p + 24, c->count);
        for (unsigned j = 0; j < c->count; ++j) put32(p + 28 + 4 * j, c->units[j]);
        size += 28 + 4 * c->count;
    }
    put32(wire, checksum(wire, size) | (packet->checksum & ~NCMD_CHECKSUM));
    return size;
}

static bool decode(const uint8_t *wire, size_t size) {
    if (size < 8 || wire[7] > BACKUPTICS ||
        (get32(wire) & NCMD_CHECKSUM) != checksum(wire, size)) return false;
    doomdata_t packet = { .checksum = get32(wire), .retransmitfrom = wire[4],
        .starttic = wire[5], .player = wire[6], .numtics = wire[7] };
    size_t offset = 8;
    for (int i = 0; i < packet.numtics; ++i) {
        if (size - offset < 28) return false;
        const uint8_t *p = wire + offset;
        ticcmd_t *c = &packet.cmds[i];
        if (get32(p + 4) > TC_BUILD) return false;
        c->consistancy = get32(p); c->order = get32(p + 4);
        c->position = (fixed3_t){ (int32_t)get32(p + 8), (int32_t)get32(p + 12), 0 };
        c->target = get32(p + 16); c->product = (int32_t)get32(p + 20);
        c->count = get32(p + 24);
        if (c->order > TC_BUILD || c->count > MAXCOMMANDUNITS ||
            c->count > (size - offset - 28) / 4) return false;
        for (unsigned j = 0; j < c->count; ++j) c->units[j] = get32(p + 28 + 4 * j);
        offset += 28 + 4 * c->count;
    }
    if (offset != size) return false;
    doomcom->data = packet;
    return true;
}


/* Session discovery is separate from Doom's tic protocol. The host relays
 * addressed tic packets so joiners only need one reachable UDP endpoint. */
enum { SESSION_MAGIC = 0x4f525453, SESSION_VERSION = 1,
       JOIN = 1, WELCOME, REJECT, DATA, GAME_LENGTH = 32, MAP_LENGTH = 512,
       WELCOME_SIZE = 10 + GAME_LENGTH + MAP_LENGTH };
static bool hosting, joining, session_received;
static int joined;
static char session_game[GAME_LENGTH], session_map[MAP_LENGTH];

bool I_NetJoining(void) { return joining; }

static bool same_address(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static void send_wire(const uint8_t *wire, size_t size, const struct sockaddr_in *to) {
    if (sendto(insocket, wire, size, 0, (const struct sockaddr *)to, sizeof(*to)) < 0 &&
        errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        snprintf(neterror, sizeof(neterror), "Network send: %s", strerror(errno));
}

static void session_header(uint8_t *wire, int kind, int player, int players) {
    put32(wire, SESSION_MAGIC);
    wire[4] = SESSION_VERSION; wire[5] = (uint8_t)kind;
    wire[6] = (uint8_t)player; wire[7] = (uint8_t)players;
}

static void reject_join(const struct sockaddr_in *to, const char *reason) {
    uint8_t wire[264] = {0};
    session_header(wire, REJECT, 0, 0);
    snprintf((char *)wire + 8, sizeof(wire) - 8, "%s", reason);
    send_wire(wire, sizeof(wire), to);
}

/* Returns a logical Doom node for DATA, or -1 for handled session traffic. */
static int session_packet(uint8_t *wire, size_t size, const struct sockaddr_in *from) {
    if (size < 8 || get32(wire) != SESSION_MAGIC) return -1;
    int node = 1;
    if (hosting) {
        while (node <= joined && !same_address(from, &sendaddress[node])) ++node;
        if (wire[5] == JOIN) {
            if (size != 8 + GAME_LENGTH || wire[4] != SESSION_VERSION ||
                !memchr(wire + 8, 0, GAME_LENGTH) ||
                strcmp((char *)wire + 8, session_game)) {
                reject_join(from, "Network game or session version mismatch"); return -1;
            }
            if (node > joined) {
                if (joined == doomcom->numplayers - 1) {
                    reject_join(from, "Server is full; late joining is not supported"); return -1;
                }
                sendaddress[node] = *from;
                ++joined;
                printf("Player %d joined (%d/%d).\n", node + 1, joined + 1, doomcom->numplayers);
                fflush(stdout);
            }
            /* Nobody loads until the roster is full. Earlier joiners retry
             * and receive the same assignment even after the host has loaded. */
            if (joined != doomcom->numplayers - 1) return -1;
            uint8_t reply[WELCOME_SIZE] = {0};
            session_header(reply, WELCOME, node, doomcom->numplayers);
            reply[8] = (uint8_t)doomcom->ticdup; reply[9] = (uint8_t)doomcom->extratics;
            memcpy(reply + 10, session_game, GAME_LENGTH);
            memcpy(reply + 10 + GAME_LENGTH, session_map, MAP_LENGTH);
            send_wire(reply, sizeof(reply), from);
            return -1;
        }
        if (node > joined) return -1;
    } else if (!same_address(from, &sendaddress[1])) return -1;
    if (wire[4] != SESSION_VERSION) return -1;
    if (joining && !session_received && wire[5] == REJECT && size > 8 &&
        memchr(wire + 8, 0, size - 8)) {
        snprintf(neterror, sizeof(neterror), "%s", (char *)wire + 8);
        return -1;
    }
    if (joining && !session_received && wire[5] == WELCOME) {
        if (size != WELCOME_SIZE || wire[7] < 2 || wire[7] > MAXPLAYERS ||
            wire[6] < 1 || wire[6] >= wire[7] || wire[8] < 1 || wire[8] > 9 || wire[9] > 1 ||
            !memchr(wire + 10, 0, GAME_LENGTH) || strcmp((char *)wire + 10, session_game) ||
            !memchr(wire + 10 + GAME_LENGTH, 0, MAP_LENGTH)) return -1;
        memcpy(session_map, wire + 10 + GAME_LENGTH, MAP_LENGTH);
        doomcom->consoleplayer = wire[6];
        doomcom->numplayers = doomcom->numnodes = wire[7];
        doomcom->ticdup = wire[8]; doomcom->extratics = wire[9];
        session_received = true;
        return -1;
    }
    if (wire[5] != DATA || size < 16 || !decode(wire + 8, size - 8)) return -1;
    int player = doomcom->data.player, destination = wire[6];
    if (player >= doomcom->numplayers || destination >= doomcom->numplayers ||
        player == destination) return -1;
    if (hosting) {
        if (player != node) return -1;
        if (destination) {
            if (destination <= joined) send_wire(wire, size, &sendaddress[destination]);
            return -1;
        }
        return node;
    }
    if (!session_received || destination != doomcom->consoleplayer) return -1;
    return player < doomcom->consoleplayer ? player + 1 : player;
}

bool I_StartNetGame(const char *game, char *map, size_t capacity) {
    if (!hosting && !joining) return true;
    if (strlen(game) >= GAME_LENGTH || strlen(map) >= MAP_LENGTH ||
        (hosting && !map[0])) {
        snprintf(neterror, sizeof(neterror), "Network game/map name is missing or too long");
        return false;
    }
    if (hosting && (map[0] == '/' || strstr(map, "..") || strchr(map, '\\'))) {
        snprintf(neterror, sizeof(neterror), "Network maps must be relative to the data root, without '..'");
        return false;
    }
    strcpy(session_game, game);
    strcpy(session_map, map);
    if (hosting)
        printf("Hosting %s: %s; waiting for %d players on UDP.\n", game, map, doomcom->numplayers);
    else printf("Joining %s; waiting for the host's map.\n", game);
    fflush(stdout);
    uint64_t started = SDL_GetTicks64(), retry = 0;
    while (!neterror[0]) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                snprintf(neterror, sizeof(neterror), "Network startup cancelled"); return false;
            }
        }
        uint64_t now = SDL_GetTicks64();
        if (joining && now >= retry) {
            uint8_t wire[8 + GAME_LENGTH] = {0};
            session_header(wire, JOIN, 0, 0);
            memcpy(wire + 8, session_game, GAME_LENGTH);
            send_wire(wire, sizeof(wire), &sendaddress[1]);
            retry = now + 250;
        }
        doomcom->command = CMD_GET;
        I_NetCmd();
        if ((hosting && joined == doomcom->numplayers - 1) || (joining && session_received)) {
            if (!session_map[0] || session_map[0] == '/' || strstr(session_map, "..") ||
                strchr(session_map, '\\') || strlen(session_map) >= capacity) {
                snprintf(neterror, sizeof(neterror), "Network maps must be relative to the data root, without '..'");
                return false;
            }
            strcpy(map, session_map);
            printf("Session ready: player %d/%d, map %s.\n",
                   doomcom->consoleplayer + 1, doomcom->numplayers, map);
            fflush(stdout);
            return true;
        }
        if (now - started >= 60000) {
            snprintf(neterror, sizeof(neterror), "Network startup timed out waiting for %s", hosting ? "players" : "host");
            break;
        }
        SDL_Delay(10);
    }
    return false;
}

static bool number(const char *s, int min, int max, int *out) {
    char *end;
    long value = strtol(s, &end, 10);
    if (!*s || *end || value < min || value > max) return false;
    *out = (int)value;
    return true;
}

bool I_InitNetwork(int *argc, char **argv) {
    I_ShutdownNetwork();
    *doomcom = (doomcom_t){ .id = DOOMCOM_ID, .numnodes = 1, .numplayers = 1, .ticdup = 1 };
    neterror[0] = '\0';
    netgame = false;
    hosting = joining = session_received = false;
    joined = 0;
    memset(sendaddress, 0, sizeof(sendaddress));
    memset(session_game, 0, sizeof(session_game));
    memset(session_map, 0, sizeof(session_map));
    const char *hosts[MAXPLAYERS - 1];
    int hostcount = 0, port = 5029, output = 1, players = 2;
    bool port_set = false, players_set = false;
    for (int i = 1; i < *argc; ++i) {
        const char *arg = argv[i];
        if (arg[0] == '-' && arg[1] == '-') ++arg;
        if (!strcmp(arg, "-host")) {
            if (netgame) goto usage;
            netgame = hosting = true;
        } else if (!strcmp(arg, "-join")) {
            if (netgame || ++i == *argc || !argv[i][0]) goto usage;
            netgame = joining = true;
            hosts[hostcount++] = argv[i];
        } else if (!strcmp(arg, "-players")) {
            if (players_set || ++i == *argc || !number(argv[i], 2, MAXPLAYERS, &players)) goto usage;
            players_set = true;
        } else if (!strcmp(arg, "-net")) {
            int player;
            if (netgame || ++i == *argc || !number(argv[i], 1, MAXPLAYERS, &player)) goto usage;
            netgame = true;
            doomcom->consoleplayer = player - 1;
            while (i + 1 < *argc && argv[i + 1][0] != '-') {
                if (hostcount == MAXPLAYERS - 1) goto usage;
                hosts[hostcount++] = argv[++i];
            }
        } else if (!strcmp(arg, "-port") || !strcmp(arg, "-dup")) {
            bool isport = !strcmp(arg, "-port");
            if (isport) port_set = true;
            if (++i == *argc || !number(argv[i], 1, isport ? 65535 : 9,
                                       isport ? &port : &doomcom->ticdup)) goto usage;
        } else if (!strcmp(arg, "-extratic")) {
            doomcom->extratics = 1;
        } else {
            argv[output++] = argv[i];
        }
    }
    *argc = output; argv[output] = NULL;
    if (players_set && !hosting) goto usage;
    if (!netgame) return true;
    if (!hosting && (!hostcount || doomcom->consoleplayer > hostcount)) goto usage;
    doomcom->numplayers = doomcom->numnodes = hosting ? players : hostcount + 1;
    for (int i = 0; i < hostcount; ++i) {
        char host[256], service[16];
        if (strlen(hosts[i]) >= sizeof(host)) goto usage;
        strcpy(host, hosts[i][0] == '.' ? hosts[i] + 1 : hosts[i]);
        char *colon = strrchr(host, ':');
        int remoteport = joining ? 5029 : port;
        if (colon) { *colon++ = '\0'; if (!number(colon, 1, 65535, &remoteport)) goto usage; }
        snprintf(service, sizeof(service), "%d", remoteport);
        struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM }, *result;
        int error = getaddrinfo(host, service, &hints, &result);
        if (error) { snprintf(neterror, sizeof(neterror), "Resolve %.120s: %s", host, gai_strerror(error)); return false; }
        sendaddress[i + 1] = *(struct sockaddr_in *)result->ai_addr;
        freeaddrinfo(result);
        for (int j = 1; j <= i; ++j)
            if (sendaddress[j].sin_addr.s_addr == sendaddress[i + 1].sin_addr.s_addr &&
                sendaddress[j].sin_port == sendaddress[i + 1].sin_port) goto usage;
    }
    insocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (joining && !port_set) port = 0; /* OS-assigned client port permits same-machine play. */
    struct sockaddr_in local = { .sin_family = AF_INET, .sin_port = htons(port),
                                .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (insocket < 0 || bind(insocket, (struct sockaddr *)&local, sizeof(local)) < 0 ||
        fcntl(insocket, F_SETFL, O_NONBLOCK) < 0) {
        snprintf(neterror, sizeof(neterror), "Network socket: %s", strerror(errno));
        I_ShutdownNetwork(); return false;
    }
    return true;
usage:
    snprintf(neterror, sizeof(neterror), "Usage: --host [--players 2..4] [--port 5029] | --join host[:port] | --net <1..4> <peer[:port]> ...; --dup 1..9 --extratic");
    return false;
}

void I_NetCmd(void) {
    uint8_t wire[WIREMAX + 9];
    if (doomcom->command == CMD_SEND) {
        int node = doomcom->remotenode;
        size_t size;
        if (hosting || joining) {
            int player = node <= doomcom->consoleplayer ? node - 1 : node;
            session_header(wire, DATA, player, doomcom->numplayers);
            size = 8 + encode(wire + 8);
        } else size = encode(wire);
        send_wire(wire, size, &sendaddress[joining ? 1 : node]);
        return;
    }
    doomcom->remotenode = -1;
    /* Bound work so unrelated traffic cannot starve input and rendering. */
    for (int attempt = 0; attempt < 64; ++attempt) {
        struct sockaddr_in from;
        socklen_t length = sizeof(from);
        ssize_t size = recvfrom(insocket, wire, sizeof(wire), 0, (struct sockaddr *)&from, &length);
        if (size < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                snprintf(neterror, sizeof(neterror), "Network receive: %s", strerror(errno));
            return;
        }
        if (hosting || joining) {
            int node = session_packet(wire, (size_t)size, &from);
            if (node < 0) continue;
            doomcom->remotenode = node; doomcom->datalength = (int)size - 8;
            return;
        }
        for (int node = 1; node < doomcom->numnodes; ++node) {
            if (from.sin_addr.s_addr != sendaddress[node].sin_addr.s_addr ||
                from.sin_port != sendaddress[node].sin_port) continue;
            if (!decode(wire, (size_t)size)) break;
            doomcom->remotenode = node; doomcom->datalength = (int)size;
            return;
        }
    }
}

void I_ShutdownNetwork(void) {
    if (insocket >= 0) close(insocket);
    insocket = -1;
}
