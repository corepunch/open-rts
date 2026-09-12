#define _POSIX_C_SOURCE 200112L
#include "d_net.h"
#include <arpa/inet.h>
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
    const char *hosts[MAXPLAYERS - 1];
    int hostcount = 0, port = 5029, output = 1;
    for (int i = 1; i < *argc; ++i) {
        const char *arg = argv[i];
        if (arg[0] == '-' && arg[1] == '-') ++arg;
        if (!strcmp(arg, "-net")) {
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
            if (++i == *argc || !number(argv[i], 1, isport ? 65535 : 9,
                                       isport ? &port : &doomcom->ticdup)) goto usage;
        } else if (!strcmp(arg, "-extratic")) {
            doomcom->extratics = 1;
        } else {
            argv[output++] = argv[i];
        }
    }
    *argc = output; argv[output] = NULL;
    if (!netgame) return true;
    if (!hostcount || doomcom->consoleplayer > hostcount) goto usage;
    doomcom->numplayers = doomcom->numnodes = hostcount + 1;
    for (int i = 0; i < hostcount; ++i) {
        char host[256], service[16];
        if (strlen(hosts[i]) >= sizeof(host)) goto usage;
        strcpy(host, hosts[i][0] == '.' ? hosts[i] + 1 : hosts[i]);
        char *colon = strrchr(host, ':');
        int remoteport = port;
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
    struct sockaddr_in local = { .sin_family = AF_INET, .sin_port = htons(port),
                                .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (insocket < 0 || bind(insocket, (struct sockaddr *)&local, sizeof(local)) < 0 ||
        fcntl(insocket, F_SETFL, O_NONBLOCK) < 0) {
        snprintf(neterror, sizeof(neterror), "Network socket: %s", strerror(errno));
        I_ShutdownNetwork(); return false;
    }
    return true;
usage:
    snprintf(neterror, sizeof(neterror), "Usage: --port 5029 --dup 1 --extratic --net <1..4> <peer[:port]> ... (put --software before map paths)");
    return false;
}

void I_NetCmd(void) {
    uint8_t wire[WIREMAX + 1];
    if (doomcom->command == CMD_SEND) {
        size_t size = encode(wire);
        const struct sockaddr_in *to = &sendaddress[doomcom->remotenode];
        if (sendto(insocket, wire, size, 0, (const struct sockaddr *)to, sizeof(*to)) < 0 &&
            errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            snprintf(neterror, sizeof(neterror), "Network send: %s", strerror(errno));
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
