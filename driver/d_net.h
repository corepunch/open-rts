#ifndef __D_NET__
#define __D_NET__

#include "d_ticcmd.h"

#define MAXPLAYERS 4
#define MAXNETNODES 8
#define BACKUPTICS 12
#define DOOMCOM_ID UINT32_C(0x12345678)
#define NCMD_EXIT UINT32_C(0x80000000)
#define NCMD_RETRANSMIT UINT32_C(0x40000000)
#define NCMD_SETUP UINT32_C(0x20000000)
#define NCMD_KILL UINT32_C(0x10000000)
#define NCMD_CHECKSUM UINT32_C(0x0fffffff)

typedef struct {
    uint32_t checksum;
    uint8_t retransmitfrom, starttic, player, numtics;
    ticcmd_t cmds[BACKUPTICS];
} doomdata_t;

typedef struct {
    uint32_t id;
    int command, remotenode, datalength;
    int numnodes, numplayers, consoleplayer, ticdup, extratics;
    doomdata_t data;
} doomcom_t;

enum { CMD_SEND = 1, CMD_GET = 2 };
extern doomcom_t *doomcom;
extern doomdata_t *netbuffer;
extern bool netgame, netready, nodeingame[MAXNETNODES], playeringame[MAXPLAYERS];
extern bool netactive;
extern int consoleplayer, gametic, maketic, ticdup;
extern int nettics[MAXNETNODES];
extern ticcmd_t netcmds[MAXPLAYERS][BACKUPTICS];
extern char neterror[256];

/* Remove network switches from argv before the game parses its arguments. */
bool I_InitNetwork(int *argc, char **argv);
void I_NetCmd(void);
void I_ShutdownNetwork(void);
void D_CheckNetGame(uint32_t signature);
void D_QuitNetGame(void);
void NetUpdate(void);
/* Returns available simulation tics; the driver runs each and advances gametic. */
int TryRunTics(void);
bool D_RunTiccmds(void);
bool D_PlayerIsHuman(int owner);
int ExpandTics(int low);

#endif
