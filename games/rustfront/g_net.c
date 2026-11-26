#include "engine.h"
#include "rustfront.h"

/* Lockstep (driver/d_net.c) compares a hash of the world every tic. The engine hashes every
 * mobj, the money and the ore deposits; Rustfront adds the ground it changes during a game:
 * each cell's terrain (a mined-out ore cell turns to sand, which changes the transitions and
 * what an idle harvester looks for) and the blocking grid that footprints and cliffs write
 * (g_mission.c), which steers pathing and where the computer may build. */
uint32_t G_ConsistencyExtra(uint32_t hash) {
    size_t cells = (size_t)level.width * level.height;
    if (level.width <= 0 || level.height <= 0) return hash;
    for (size_t i = 0; i < cells; i += 4) {
        uint32_t terrain = 0, blocked = 0;
        for (size_t k = 0; k < 4 && i + k < cells; ++k) {
            if (level.cell_terrain) terrain |= (uint32_t)level.cell_terrain[i + k] << (8 * k);
            if (level.blocked) blocked |= (uint32_t)(level.blocked[i + k] != 0) << k;
        }
        hash = G_HashValue(hash, terrain);
        hash = G_HashValue(hash, blocked);
    }
    hash = G_HashValue(hash, level.player_teams);
    for (int team = 0; team < 8; ++team) hash = G_HashValue(hash, level.sight.allies[team]);
    return RF_UnitsHash(hash);
}
