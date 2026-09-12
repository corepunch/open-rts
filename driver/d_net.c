#include "d_net.h"
#include "engine_config.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>

doomdata_t *netbuffer;
bool netgame, netready, netactive;
bool nodeingame[MAXNETNODES], playeringame[MAXPLAYERS];
int consoleplayer, gametic, maketic, ticdup = 1;
int nettics[MAXNETNODES];
ticcmd_t netcmds[MAXPLAYERS][BACKUPTICS];
char neterror[256];

static ticcmd_t localcmds[BACKUPTICS];
static int resendto[MAXNETNODES], resendcount[MAXNETNODES];
static bool remoteresend[MAXNETNODES], gotsetup[MAXNETNODES];
static bool gotcommands[MAXNETNODES];
static int nodeforplayer[MAXPLAYERS], playerfornode[MAXNETNODES];
static uint32_t consistancy[BACKUPTICS], startsignature;
static doomdata_t reboundstore;
static bool reboundpacket;
static uint64_t gametime, oldentertics;
static int skiptics, frameon, frameskip[4], oldnettics;

enum { RESENDCOUNT = 10, NETVERSION = 1 };

static uint64_t I_GetTime(void) {
    return SDL_GetTicks64() * RTS_TICRATE / 1000 / ticdup;
}

int ExpandTics(int low) {
    int delta = low - (maketic & 255);
    if (delta > 64) return (maketic & ~255) - 256 + low;
    if (delta < -64) return (maketic & ~255) + 256 + low;
    return (maketic & ~255) + low;
}

bool D_PlayerIsHuman(int owner) {
    return owner == 0 || (netgame && owner >= 0 && owner < doomcom->numplayers);
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
        .position = { ticdup, RTS_TICRATE, 0 } };
    HSendPacket(node, NCMD_SETUP);
}

static void GetPackets(void) {
    for (int packets = 0; packets < 64 && HGetPacket(); ++packets) {
        int node = doomcom->remotenode;
        int player = netbuffer->player;
        if (player >= doomcom->numplayers || player != playerfornode[node]) {
            snprintf(neterror, sizeof(neterror), "Network player numbers or peer order disagree (node %d, player %d)", node, player + 1);
            return;
        }
        if (netbuffer->checksum & NCMD_KILL) {
            snprintf(neterror, sizeof(neterror), "Network game killed by player %d", player + 1);
            return;
        }
        if (netbuffer->checksum & NCMD_SETUP) {
            const ticcmd_t *setup = &netbuffer->cmds[0];
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
        gotcommands[node] = true;
        if (netbuffer->checksum & NCMD_EXIT) {
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
    consoleplayer = doomcom->consoleplayer;
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
    memset(frameskip, 0, sizeof(frameskip));
    G_ClearTiccmds();
    startsignature = signature;
    reboundpacket = false;
    gotsetup[0] = true;
    netready = !netgame;
    netactive = true;
    neterror[0] = '\0';
    int node = 1;
    for (int player = 0; player < doomcom->numplayers; ++player) {
        int n = player == consoleplayer ? 0 : node++;
        nodeforplayer[player] = n;
        playerfornode[n] = player;
        nodeingame[n] = playeringame[player] = true;
    }
    gametime = oldentertics = I_GetTime();
    if (netgame) printf("Synchronizing player %d of %d...\n", consoleplayer + 1, doomcom->numplayers);
}

void NetUpdate(void) {
    if (neterror[0]) return;
    GetPackets();
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
    if (newtics <= 0 || neterror[0]) return;
    int skip = skiptics < newtics ? skiptics : newtics;
    newtics -= skip; skiptics -= skip;
    for (int i = 0; i < newtics; ++i) {
        if (maketic - gametic / ticdup >= BACKUPTICS / 2 - 1) break;
        ticcmd_t *cmd = &localcmds[maketic % BACKUPTICS];
        G_BuildTiccmd(cmd);
        cmd->consistancy = consistancy[maketic % BACKUPTICS];
        ++maketic;
    }
    for (int node = 0; node < doomcom->numnodes; ++node) {
        if (!nodeingame[node]) continue;
        int start = resendto[node];
        if (start < 0) start = 0;
        if (maketic - start > BACKUPTICS) {
            snprintf(neterror, sizeof(neterror), "Network command history exhausted"); return;
        }
        *netbuffer = (doomdata_t){ .player = (uint8_t)consoleplayer,
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

bool D_RunTiccmds(void) {
    if (gametic % ticdup) return true; /* RTS orders, like BT_SPECIAL, fire once. */
    int tic = gametic / ticdup, slot = tic % BACKUPTICS;
    for (int player = 0; player < doomcom->numplayers; ++player) {
        if (!playeringame[player]) continue;
        if (nettics[nodeforplayer[player]] <= tic) return false;
        if (netgame && tic >= BACKUPTICS && netcmds[player][slot].consistancy != consistancy[slot]) {
            snprintf(neterror, sizeof(neterror), "Consistency failure at tic %d, player %d: %08x != %08x",
                     gametic, player + 1, netcmds[player][slot].consistancy, consistancy[slot]);
            return false;
        }
    }
    consistancy[slot] = G_Consistency();
    for (int player = 0; player < doomcom->numplayers; ++player)
        if (playeringame[player]) G_RunTiccmd(player, &netcmds[player][slot]);
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
    I_ShutdownNetwork();
    netactive = netgame = netready = false;
    consoleplayer = 0;
    G_ClearTiccmds();
}
