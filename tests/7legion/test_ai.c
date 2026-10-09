#define DOCTRINE_BASE_A MT_MOBILE_BASE, MT_BARRACKS, MT_TANK_FACTORY, MT_ROBOT_FACTORY
#define DOCTRINE_BASE_B DOCTRINE_BASE_A
#define DOCTRINE_ANCHOR MT_MOBILE_BASE
#define DOCTRINE_WEAK MT_TROOPER
#define DOCTRINE_STRONG MT_ROCK_MECH
#define DOCTRINE_ENEMY MT_TANK
#include "../ai_doctrine_regression.h"

/* Roles from the actor table: no flyers, towers or supply in 7th Legion. */
static const doctrine_role_t roles[] = {
    { MT_TROOPER, AI_ROLE_FIGHTER | AI_ROLE_HITS_GROUND, AI_ROLE_WORKER | AI_ROLE_FLYER },
    { MT_ROCK_MECH, AI_ROLE_FIGHTER, AI_ROLE_WORKER },
    { MT_SLAVE, AI_ROLE_WORKER, AI_ROLE_FIGHTER },
    { MT_TRUCK, AI_ROLE_WORKER, AI_ROLE_FIGHTER },
    { MT_MOBILE_BASE, 0, AI_ROLE_FIGHTER | AI_ROLE_DEFENSE | AI_ROLE_WORKER },
    { MT_WALL, 0, AI_ROLE_DEFENSE },
};

int main(void) {
    DCHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = g_game_default_root };
    DCHECK(model && rts_game_model_load(model, &config));
    RTS_RUN(doctrine_roles(roles, (int)(sizeof(roles) / sizeof(*roles))));
    doctrine_army_t a, b;
    RTS_RUN(doctrine_mix(&a, &b));
    RTS_RUN(doctrine_weights());
    RTS_RUN(doctrine_waves());
    rts_game_model_destroy(model);
    SDL_Quit();
    puts("PASS: 7legion doctrine: roles, army mix by weight, workers, held and launched waves");
    return 0;
}
