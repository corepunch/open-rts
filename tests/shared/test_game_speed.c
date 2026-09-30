#include "game.h"
#include "d_net.h"
#include <assert.h>
#include <string.h>

/* Each game declares its default simulation speed; the driver applies it
 * before parsing --speed. Dark Colony runs at its retail 200% maximum. */
int main(void) {
    G_InitGame();
    assert(gameinfo);
    int expected = strcmp(g_game_id, "dark-colony") == 0 ? 2 : 0;
    assert(gameinfo->game_speed == expected);
    assert(gameinfo->game_speed >= 0 && gameinfo->game_speed <= 9);

    assert(game_speed == 1);
    if (gameinfo->game_speed) D_SetGameSpeed(gameinfo->game_speed);
    assert(game_speed == (expected ? expected : 1));
    D_SetGameSpeed(0);
    D_SetGameSpeed(10);
    assert(game_speed == (expected ? expected : 1));
    D_SetGameSpeed(3);
    assert(game_speed == 3);
    return 0;
}
