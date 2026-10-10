#include "engine.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

doomdata_t *netbuffer;
bool netgame, netready, netactive;
bool nodeingame[MAXNETNODES], playeringame[MAXPLAYERS];
int consoleplayer, gametic, maketic, ticdup = 1;
int game_speed = 100; /* Percent, as DC.EXE's Options dialog: 10..200. */
static struct { uint64_t time, scaled; } speedclock;

static uint64_t scaled_time(void) {
    uint64_t now = SDL_GetTicks64();
    if (now >= speedclock.time)
        speedclock.scaled += (now - speedclock.time) * (uint64_t)game_speed;
    speedclock.time = now;
    return speedclock.scaled;
}

void D_SetGameSpeed(int speed) {
    if (speed >= 10 && speed <= 200) {
        scaled_time();
        game_speed = speed;
    }
}
int nettics[MAXNETNODES];
ticcmd_t netcmds[MAXPLAYERS][BACKUPTICS];
char neterror[256];

static ticcmd_t localcmds[BACKUPTICS];
static int resendto[MAXNETNODES], resendcount[MAXNETNODES];
static bool remoteresend[MAXNETNODES], gotsetup[MAXNETNODES];
static bool gotcommands[MAXNETNODES];
static int nodeforplayer[MAXPLAYERS], playerfornode[MAXNETNODES];
static uint32_t consistancy[BACKUPTICS], startsignature;
static uint32_t consvec[BACKUPTICS][CONSISTENCY_COUNT];
static int consistency_floor; /* No hashes to compare before this tic (after a resync). */
struct netdesync_s netdesync;
static doomdata_t reboundstore;
static bool reboundpacket;
static uint64_t gametime, oldentertics;
static uint64_t lastreceived[MAXNETNODES];
static int skiptics, frameon, frameskip[4], oldnettics;

enum { RESENDCOUNT = 10, NETVERSION = 5 };

static uint64_t I_GetTime(void) {
    return scaled_time() * RTS_TICRATE / 100 / 1000 / (uint64_t)ticdup;
}

int ExpandTics(int low) {
    int delta = low - (maketic & 255);
    if (delta > 64) return (maketic & ~255) - 256 + low;
    if (delta < -64) return (maketic & ~255) + 256 + low;
    return (maketic & ~255) + low;
}

bool D_PlayerIsHuman(int owner) {
    return netgame ? owner >= 0 && owner < doomcom->numplayers : owner == consoleplayer;
}

static void HSendPacket(int node, uint32_t flags) {
    netbuffer->checksum = flags;
    if (!node) {
        reboundstore = *netbuffer;
        reboundpacket = true;
    } else {
        doomcom->command = CMD_SEND;
        doomcom->remotenode = node;
        I_NetCmd();
    }
}

static bool HGetPacket(void) {
    if (reboundpacket) {
        *netbuffer = reboundstore;
        doomcom->remotenode = 0;
        reboundpacket = false;
        return true;
    }
    if (!netgame) return false;
    doomcom->command = CMD_GET;
    I_NetCmd();
    return doomcom->remotenode >= 0;
}

static void SendSetup(int node) {
    *netbuffer = (doomdata_t){ .player = (uint8_t)consoleplayer, .numtics = 1 };
    netbuffer->cmds[0] = (ticcmd_t){ .consistancy = startsignature,
        .product = NETVERSION, .target = (uint32_t)doomcom->numplayers,
        .position = { ticdup, RTS_TICRATE, game_speed } };
    HSendPacket(node, NCMD_SETUP);
}


/* ---- Resync from save ----------------------------------------------------
 * A desync means this peer's game is no longer the others'. When handlers
 * are installed the lowest seated player (the authority) freezes at a tic
 * boundary, serializes its game, and sends it in chunks inside NCMD_RESYNC
 * packets (a TC_RESYNC ticcmd whose units[] carry the bytes). Every peer
 * reloads it, then all restart lockstep from that tic with a new epoch, so
 * tic packets of the old timeline fail their checksum. */
enum { RS_REQUEST = 1, RS_CHUNK, RS_ACK };
enum { RS_WORDS = 240, RS_WINDOW = 24, RS_MAX = 3, RS_TIMEOUT_MS = 20000, RS_MAX_BYTES = 256 << 20 };
typedef enum { RS_IDLE, RS_WAIT, RS_SEND, RS_RECV } rsstate_t;
static struct {
    const netresync_t *handlers;
    rsstate_t state;
    bool want_snapshot;
    uint32_t id, done_id; /* Session being repaired; last one this peer completed. */
    uint8_t *blob;        /* Authority: the save. Receiver: the save so far. */
    size_t size;
    int chunks, base, cursor, have_count;
    uint32_t crc;
    bool acked[MAXNETNODES];
    bool *have;
    uint64_t started, last_request;
} rs;
int netresyncs;

void D_SetNetResync(const netresync_t *handlers) { rs.handlers = handlers; }

static uint32_t blob_crc(const uint8_t *data, size_t size) {
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0; i < size; ++i) hash = (hash ^ data[i]) * UINT32_C(16777619);
    return hash;
}

static int authority_player(void) {
    for (int p = 0; p < doomcom->numplayers; ++p) if (playeringame[p]) return p;
    return 0;
}

static void resync_free(void) {
    free(rs.blob); free(rs.have);
    rs.blob = NULL; rs.have = NULL; rs.size = 0;
}

/* Restart lockstep from tic `base` with every ring empty. */
static void reset_to_tic(int base, uint32_t epoch) {
    net_epoch = epoch;
    gametic = base * ticdup;
    maketic = base;
    memset(localcmds, 0, sizeof(localcmds));
    memset(netcmds, 0, sizeof(netcmds));
    memset(consistancy, 0, sizeof(consistancy));
    memset(consvec, 0, sizeof(consvec));
    memset(resendcount, 0, sizeof(resendcount));
    memset(remoteresend, 0, sizeof(remoteresend));
    memset(frameskip, 0, sizeof(frameskip));
    skiptics = frameon = oldnettics = 0;
    uint64_t now = SDL_GetTicks64();
    for (int n = 0; n < MAXNETNODES; ++n) {
        nettics[n] = resendto[n] = base;
        lastreceived[n] = now;
    }
    consistency_floor = base + BACKUPTICS;
    gametime = oldentertics = I_GetTime();
}

static void send_resync(int node, int kind, uint32_t id, int index) {
    *netbuffer = (doomdata_t){ .player = (uint8_t)(netgame ? consoleplayer : 0), .numtics = 1 };
    ticcmd_t *c = &netbuffer->cmds[0];
    c->order = TC_RESYNC;
    c->position = (fixed3_t){ kind, (int32_t)id, index };
    if (kind == RS_CHUNK) {
        c->target = (uint32_t)rs.size;
        c->product = rs.chunks;
        c->consistancy = rs.crc;
        c->subsystems[0] = (uint32_t)rs.base;
        size_t from = (size_t)index * RS_WORDS * 4;
        unsigned words = 0;
        for (; words < RS_WORDS && from + 4 * words < rs.size; ++words) {
            uint32_t w = 0;
            for (int b = 0; b < 4; ++b) {
                size_t at = from + 4 * words + b;
                w = w << 8 | (at < rs.size ? rs.blob[at] : 0);
            }
            c->units[words] = w;
        }
        c->count = words;
    }
    HSendPacket(node, NCMD_RESYNC);
}

static void snapshot_failed(const char *why) {
    snprintf(neterror, sizeof(neterror), "Resync failed: %s", why);
    resync_free();
    rs.state = RS_IDLE;
}

/* A desync was found (or announced by the authority). */
static bool begin_resync(void) {
    if (rs.state != RS_IDLE) return true;
    if (!rs.handlers || netresyncs >= RS_MAX) return false;
    rs.state = RS_WAIT;
    rs.want_snapshot = authority_player() == consoleplayer;
    rs.started = SDL_GetTicks64();
    rs.last_request = 0;
    printf("Player %d: game state diverged, resynchronizing from player %d's save.\n",
           consoleplayer + 1, authority_player() + 1);
    return true;
}

static void take_snapshot(void) {
    rs.want_snapshot = false;
    resync_free();
    void *saved = NULL;
    if (!rs.handlers->save(&saved, &rs.size) || !saved || !rs.size ||
        rs.size > RS_MAX_BYTES) { free(saved); rs.size = 0; snapshot_failed("cannot save the game"); return; }
    rs.blob = saved;
    rs.id = rs.done_id + 1;
    rs.base = gametic / ticdup;
    rs.chunks = (int)((rs.size + 4 * RS_WORDS - 1) / (4 * RS_WORDS));
    rs.crc = blob_crc(rs.blob, rs.size);
    rs.cursor = 0;
    memset(rs.acked, 0, sizeof(rs.acked));
    rs.state = RS_SEND;
    rs.started = SDL_GetTicks64();
}

static void finish_send(void) {
    uint32_t id = rs.id;
    int base = rs.base;
    resync_free();
    rs.state = RS_IDLE;
    rs.done_id = id;
    ++netresyncs;
    reset_to_tic(base, id);
    printf("Resync %u complete: all players reloaded tic %d.\n", id, base);
}

static void receive_chunk(const ticcmd_t *c, int node) {
    uint32_t id = (uint32_t)c->position.y;
    int index = c->position.z;
    if (id <= rs.done_id) { /* Already loaded: the authority missed our ACK. */
        if (id == rs.done_id) send_resync(node, RS_ACK, id, 0);
        return;
    }
    if (!begin_resync() && rs.state == RS_IDLE) return;
    size_t size = c->target;
    int chunks = c->product;
    if (!size || size > RS_MAX_BYTES || chunks != (int)((size + 4 * RS_WORDS - 1) / (4 * RS_WORDS)) ||
        index < 0 || index >= chunks || c->count > RS_WORDS ||
        (size_t)index * RS_WORDS * 4 + c->count * 4 < (index == chunks - 1 ? size : 0)) return;
    if (rs.state != RS_RECV || rs.id != id) {
        resync_free();
        rs.blob = calloc(1, size + 4 * RS_WORDS);
        rs.have = calloc((size_t)chunks, sizeof(bool));
        if (!rs.blob || !rs.have) { snapshot_failed("out of memory"); return; }
        rs.state = RS_RECV; rs.id = id; rs.size = size; rs.chunks = chunks;
        rs.crc = c->consistancy; rs.base = (int)c->subsystems[0]; rs.have_count = 0;
    }
    if (size != rs.size || chunks != rs.chunks || rs.have[index]) return;
    for (unsigned w = 0; w < c->count; ++w)
        for (int b = 0; b < 4; ++b)
            rs.blob[(size_t)index * RS_WORDS * 4 + 4 * w + b] = (uint8_t)(c->units[w] >> (24 - 8 * b));
    rs.have[index] = true;
    if (++rs.have_count < rs.chunks) return;
    if (blob_crc(rs.blob, rs.size) != rs.crc) { snapshot_failed("save arrived damaged"); return; }
    bool ok = rs.handlers->load(rs.blob, rs.size);
    int base = rs.base;
    resync_free();
    if (!ok) { snapshot_failed("this peer could not load the save"); return; }
    rs.state = RS_IDLE;
    rs.done_id = id;
    ++netresyncs;
    reset_to_tic(base, id);
    send_resync(node, RS_ACK, id, 0);
    printf("Resync %u: reloaded tic %d.\n", id, base);
}

static void resync_packet(int node, int player) {
    ticcmd_t c = netbuffer->cmds[0];
    if (netbuffer->numtics != 1 || c.order != TC_RESYNC) return;
    uint32_t id = (uint32_t)c.position.y;
    switch (c.position.x) {
    case RS_REQUEST:
        /* A peer that is behind the latest epoch noticed a desync first. */
        if (consoleplayer == authority_player() && id == rs.done_id && begin_resync()) {}
        break;
    case RS_CHUNK:
        if (player == authority_player()) receive_chunk(&c, node);
        break;
    case RS_ACK:
        if (rs.state == RS_SEND && id == rs.id) rs.acked[node] = true;
        break;
    }
}

/* Called from NetUpdate: returns true while the game must stay frozen. */
static bool resync_update(void) {
    if (rs.state == RS_IDLE) return false;
    uint64_t now = SDL_GetTicks64();
    if (now - rs.started > RS_TIMEOUT_MS) {
        snapshot_failed("timed out");
        return true;
    }
    if (rs.state == RS_SEND) {
        bool all = true;
        for (int node = 1; node < doomcom->numnodes; ++node) {
            if (!nodeingame[node] || rs.acked[node]) continue;
            all = false;
            for (int i = 0; i < RS_WINDOW; ++i)
                send_resync(node, RS_CHUNK, rs.id, (rs.cursor + i) % rs.chunks);
        }
        rs.cursor = (rs.cursor + RS_WINDOW) % rs.chunks;
        if (all) finish_send();
    } else if (consoleplayer != authority_player() && now - rs.last_request >= 200) {
        rs.last_request = now;
        send_resync(nodeforplayer[authority_player()], RS_REQUEST, rs.done_id, 0);
    }
    return rs.state != RS_IDLE;
}

static void GetPackets(void) {
    for (int packets = 0; packets < 64 && HGetPacket(); ++packets) {
        int node = doomcom->remotenode;
        int player = netbuffer->player;
        if (player >= doomcom->numplayers || player != playerfornode[node]) {
            snprintf(neterror, sizeof(neterror), "Network player numbers or peer order disagree (node %d, player %d)", node, player + 1);
            return;
        }
        lastreceived[node] = SDL_GetTicks64();
        if (netbuffer->checksum & NCMD_KILL) {
            snprintf(neterror, sizeof(neterror), "Network game killed by player %d", player + 1);
            return;
        }
        if (netbuffer->checksum & NCMD_SETUP) {
            const ticcmd_t *setup = &netbuffer->cmds[0];
            if (netbuffer->numtics == 1 && setup->product == NETVERSION && setup->position.z != game_speed) {
                snprintf(neterror, sizeof(neterror), "Game speed mismatch: yours is %d%%, player %d uses %d%%. Use the same --speed on every player.",
                         game_speed, player + 1, setup->position.z);
                return;
            }
            if (netbuffer->numtics != 1 || setup->product != NETVERSION ||
                setup->consistancy != startsignature || setup->target != (unsigned)doomcom->numplayers ||
                setup->position.x != ticdup || setup->position.y != RTS_TICRATE) {
                snprintf(neterror, sizeof(neterror), "Network setup mismatch: game, map, initial state, version or ticdup (player %d)", player + 1);
                return;
            }
            gotsetup[node] = true;
            /* A late peer may still need our setup after we enter the game. */
            if (netready && !gotcommands[node]) SendSetup(node);
            continue;
        }
        if (!gotsetup[node] || !nodeingame[node]) continue;
        if (netbuffer->checksum & NCMD_RESYNC) { resync_packet(node, player); continue; }
        if (rs.state != RS_IDLE) continue; /* Tic traffic of the timeline being replaced. */
        gotcommands[node] = true;
        if (netbuffer->checksum & NCMD_EXIT) {
            if (I_NetJoining() && player == 0) {
                snprintf(neterror, sizeof(neterror), "Host left the game");
                return;
            }
            nodeingame[node] = playeringame[player] = false;
            printf("Player %d left the game.\n", player + 1);
            continue;
        }
        int start = ExpandTics(netbuffer->starttic);
        int end = start + netbuffer->numtics;
        if (resendcount[node] <= 0 && (netbuffer->checksum & NCMD_RETRANSMIT)) {
            int from = ExpandTics(netbuffer->retransmitfrom);
            if (from < 0 || from > maketic) continue;
            if (from < maketic - BACKUPTICS) {
                snprintf(neterror, sizeof(neterror), "Player %d requested expired tic %d at %d", player + 1, from, maketic);
                return;
            }
            resendto[node] = from;
            resendcount[node] = RESENDCOUNT;
        } else if (resendcount[node] > 0) --resendcount[node];
        if (end <= nettics[node]) continue;
        if (start > nettics[node]) { remoteresend[node] = true; continue; }
        if (start < 0 || end > gametic / ticdup + BACKUPTICS) continue;
        remoteresend[node] = false;
        while (nettics[node] < end) {
            int tic = nettics[node]++;
            netcmds[player][tic % BACKUPTICS] = netbuffer->cmds[tic - start];
        }
    }
}

void D_CheckNetGame(uint32_t signature) {
    netbuffer = &doomcom->data;
    /* A net game takes its seat from the lobby. Single player keeps the slot
     * the map chose: Warcraft II's human is often not player 0, and replacing
     * that here hid every unit after the screenshot path had already returned. */
    if (netgame) consoleplayer = doomcom->consoleplayer;
    ticdup = doomcom->ticdup;
    gametic = maketic = skiptics = frameon = oldnettics = 0;
    memset(localcmds, 0, sizeof(localcmds));
    memset(netcmds, 0, sizeof(netcmds));
    memset(nettics, 0, sizeof(nettics));
    memset(resendto, 0, sizeof(resendto));
    memset(resendcount, 0, sizeof(resendcount));
    memset(remoteresend, 0, sizeof(remoteresend));
    memset(gotsetup, 0, sizeof(gotsetup));
    memset(gotcommands, 0, sizeof(gotcommands));
    memset(nodeingame, 0, sizeof(nodeingame));
    memset(playeringame, 0, sizeof(playeringame));
    memset(consistancy, 0, sizeof(consistancy));
    memset(consvec, 0, sizeof(consvec));
    memset(frameskip, 0, sizeof(frameskip));
    consistency_floor = 0;
    net_epoch = 0;
    netdesync = (struct netdesync_s){0};
    netresyncs = 0;
    resync_free();
    rs.state = RS_IDLE; rs.done_id = 0; rs.id = 0; rs.want_snapshot = false;
    G_ClearTiccmds();
    startsignature = signature;
    reboundpacket = false;
    gotsetup[0] = true;
    netready = !netgame;
    netactive = true;
    neterror[0] = '\0';
    if (!netgame) {
        /* One local seat. Its map slot can be anywhere in 0..7. */
        nodeforplayer[0] = 0;
        playerfornode[0] = 0;
        nodeingame[0] = playeringame[0] = true;
    } else {
        int node = 1;
        for (int player = 0; player < doomcom->numplayers; ++player) {
            int n = player == consoleplayer ? 0 : node++;
            nodeforplayer[player] = n;
            playerfornode[n] = player;
            nodeingame[n] = playeringame[player] = true;
        }
    }
    gametime = oldentertics = I_GetTime();
    for (int n = 0; n < doomcom->numnodes; ++n) lastreceived[n] = SDL_GetTicks64();
    if (netgame) printf("Synchronizing player %d of %d...\n", consoleplayer + 1, doomcom->numplayers);
}

void NetUpdate(void) {
    if (neterror[0]) return;
    GetPackets();
    if (netgame) {
        for (int node = 1; node < doomcom->numnodes; ++node) {
            if (nodeingame[node] && SDL_GetTicks64() - lastreceived[node] >= 30000) {
                snprintf(neterror, sizeof(neterror), "Network timed out waiting for player %d", playerfornode[node] + 1);
                return;
            }
        }
    }
    uint64_t now = I_GetTime();
    int newtics = now > gametime ? (int)(now - gametime) : 0;
    gametime = now;
    if (!netready) {
        bool ready = true;
        for (int node = 1; node < doomcom->numnodes; ++node) {
            if (newtics) SendSetup(node);
            if (!gotsetup[node]) ready = false;
        }
        if (!ready) return;
        netready = true;
        oldentertics = now;
        printf("Network game synchronized.\n");
        newtics = 1;
    }
    if (resync_update()) return;
    if (newtics <= 0 || neterror[0]) return;
    int skip = skiptics < newtics ? skiptics : newtics;
    newtics -= skip; skiptics -= skip;
    for (int i = 0; i < newtics; ++i) {
        if (maketic - gametic / ticdup >= BACKUPTICS / 2 - 1) break;
        ticcmd_t *cmd = &localcmds[maketic % BACKUPTICS];
        G_BuildTiccmd(cmd);
        cmd->consistancy = consistancy[maketic % BACKUPTICS];
        memcpy(cmd->subsystems, consvec[maketic % BACKUPTICS], sizeof(cmd->subsystems));
        ++maketic;
    }
    for (int node = 0; node < doomcom->numnodes; ++node) {
        if (!nodeingame[node]) continue;
        int start = resendto[node];
        if (start < 0) start = 0;
        if (maketic - start > BACKUPTICS) {
            snprintf(neterror, sizeof(neterror), "Network command history exhausted"); return;
        }
        *netbuffer = (doomdata_t){ .player = (uint8_t)(netgame ? consoleplayer : 0),
            .starttic = (uint8_t)start, .numtics = (uint8_t)(maketic - start),
            .retransmitfrom = (uint8_t)nettics[node] };
        for (int i = start; i < maketic; ++i)
            netbuffer->cmds[i - start] = localcmds[i % BACKUPTICS];
        resendto[node] = maketic - doomcom->extratics;
        HSendPacket(node, remoteresend[node] ? NCMD_RETRANSMIT : 0);
    }
    GetPackets();
}

int TryRunTics(void) {
    uint64_t now = I_GetTime();
    int realtics = (int)(now - oldentertics);
    oldentertics = now;
    NetUpdate();
    if (!netready || neterror[0]) return 0;
    int lowtic = maketic;
    for (int node = 0; node < doomcom->numnodes; ++node)
        if (nodeingame[node] && nettics[node] < lowtic) lowtic = nettics[node];
    int available = lowtic - gametic / ticdup;
    if (available < 0) {
        snprintf(neterror, sizeof(neterror), "TryRunTics: lowtic < gametic"); return 0;
    }
    /* Doom's key player does not adapt its clock. */
    if (netgame && realtics > 0) {
        int key = 0;
        while (key < doomcom->numplayers && !playeringame[key]) ++key;
        if (key < doomcom->numplayers && consoleplayer != key) {
            int remote = nettics[nodeforplayer[key]];
            if (nettics[0] <= remote && gametime) --gametime;
            frameskip[frameon++ & 3] = oldnettics > remote;
            oldnettics = nettics[0];
            if (frameskip[0] && frameskip[1] && frameskip[2] && frameskip[3]) skiptics = 1;
        }
    }
    int counts = realtics < available - 1 ? realtics + 1 :
                 realtics < available ? realtics : available;
    /* Yield to the SDL loop while waiting; menus and the window stay alive. */
    if (counts < 1) counts = available > 0 ? 1 : 0;
    return counts * ticdup;
}

static uint32_t fold_consistency(const uint32_t parts[CONSISTENCY_COUNT]) {
    uint32_t hash = UINT32_C(2166136261);
    for (int i = 0; i < CONSISTENCY_COUNT; ++i) hash = G_HashValue(hash, parts[i]);
    return hash;
}

/* The slot's hash describes the state at tic - BACKUPTICS: that is where the
 * peers first differed, not the tic that noticed. */
static void report_desync(int player, int tic, const ticcmd_t *theirs, const uint32_t ours[CONSISTENCY_COUNT]) {
    netdesync = (struct netdesync_s){ .tic = (tic - BACKUPTICS) * ticdup, .detected = tic * ticdup, .player = player };
    char list[160] = "";
    for (int i = 0; i < CONSISTENCY_COUNT; ++i) {
        if (theirs->subsystems[i] == ours[i]) continue;
        netdesync.subsystems |= 1u << i;
        size_t used = strlen(list);
        snprintf(list + used, sizeof(list) - used, "%s%s %08x != %08x", used ? "; " : "",
                 g_consistency_names[i], ours[i], theirs->subsystems[i]);
    }
    snprintf(neterror, sizeof(neterror), "Desync: player %d's game first differs at tic %d (noticed at tic %d) in %s",
             player + 1, netdesync.tic, netdesync.detected, list[0] ? list : "no subsystem (corrupt packet?)");
}

bool D_RunTiccmds(void) {
    if (gametic % ticdup) return true; /* RTS orders, like BT_SPECIAL, fire once. */
    if (rs.state != RS_IDLE) {
        if (rs.want_snapshot) take_snapshot();
        return false;
    }
    int tic = gametic / ticdup, slot = tic % BACKUPTICS;
    for (int player = 0; player < doomcom->numplayers; ++player) {
        if (!playeringame[player]) continue;
        if (nettics[nodeforplayer[player]] <= tic) return false;
        if (netgame && tic >= BACKUPTICS && tic >= consistency_floor &&
            netcmds[player][slot].consistancy != consistancy[slot]) {
            report_desync(player, tic, &netcmds[player][slot], consvec[slot]);
            if (begin_resync()) {
                neterror[0] = '\0';
                if (rs.want_snapshot) take_snapshot();
            }
            return false;
        }
    }
    G_ConsistencyVector(consvec[slot]);
    consistancy[slot] = fold_consistency(consvec[slot]);
    for (int player = 0; player < doomcom->numplayers; ++player)
        if (playeringame[player])
            G_RunTiccmd(netgame ? player : consoleplayer, &netcmds[player][slot]);
    return true;
}

void D_QuitNetGame(void) {
    if (netactive && netgame) {
        for (int repeat = 0; repeat < 4; ++repeat) {
            *netbuffer = (doomdata_t){ .player = (uint8_t)consoleplayer };
            for (int node = 1; node < doomcom->numnodes; ++node)
                if (nodeingame[node]) HSendPacket(node, neterror[0] ? NCMD_KILL : NCMD_EXIT);
            SDL_Delay(1);
        }
    }
    resync_free();
    rs.state = RS_IDLE;
    I_ShutdownNetwork();
    netactive = netgame = netready = false;
    consoleplayer = 0;
    G_ClearTiccmds();
}
