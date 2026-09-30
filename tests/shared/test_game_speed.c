#include "game.h"
#include "d_net.h"
#include <assert.h>
#include <string.h>

/* Each game declares its default simulation speed in percent, the unit of
 * DC.EXE's Options dialog; the driver applies it before parsing --speed. */
int main(void) {
    G_InitGame();
    assert(gameinfo);
    int expected = strcmp(g_game_id, "dark-colony") == 0 ? 100 : 0;
    assert(gameinfo->game_speed == expected);
    assert(gameinfo->game_speed >= 0 && gameinfo->game_speed <= 200);

    assert(game_speed == 100);
    if (gameinfo->game_speed) D_SetGameSpeed(gameinfo->game_speed);
    assert(game_speed == (expected ? expected : 100));
    D_SetGameSpeed(9);
    D_SetGameSpeed(201);
    assert(game_speed == (expected ? expected : 100));
    D_SetGameSpeed(200);
    assert(game_speed == 200);
    return 0;
}
