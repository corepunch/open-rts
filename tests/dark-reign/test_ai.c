#define DOCTRINE_BASE_A MT_FG_CONSTRUCTION_CREW, MT_FG_HQ1, MT_FG_HQ2, MT_FG_BARRACKS, MT_FG_ADV_BARRACKS, \
    MT_FG_VEHICLE_FACTORY, MT_FG_ADV_VEHICLE_FACTORY
#define DOCTRINE_BASE_B MT_IMP_CONSTRUCTION_CREW, MT_IMP_HQ1, MT_IMP_HQ2, MT_IMP_BARRACKS, MT_IMP_ADV_BARRACKS, \
    MT_IMP_VEHICLE_FACTORY, MT_IMP_ADV_VEHICLE_FACTORY
#define DOCTRINE_ANCHOR MT_FG_HQ1
#define DOCTRINE_WEAK MT_FG_RAIDER
#define DOCTRINE_STRONG MT_FG_TRIPLE_RAIL_TANK
#define DOCTRINE_ENEMY MT_IMP_TACHYON_TANK
#include "../ai_doctrine_regression.h"

/* Roles from the actor table, its WEAPON.TXT reach and describe(). */
static const doctrine_role_t roles[] = {
    { MT_FG_RAIDER, AI_ROLE_FIGHTER | AI_ROLE_HITS_GROUND, AI_ROLE_HITS_AIR | AI_ROLE_WORKER },
    { MT_IMP_BION, AI_ROLE_FIGHTER | AI_ROLE_HITS_GROUND | AI_ROLE_HITS_AIR, 0 },
    { MT_FG_MAD, AI_ROLE_FIGHTER | AI_ROLE_HITS_AIR, AI_ROLE_HITS_GROUND },
    { MT_IMP_MAD, AI_ROLE_FIGHTER | AI_ROLE_HITS_AIR, AI_ROLE_HITS_GROUND },
    { MT_FG_SKY_BIKE, AI_ROLE_FIGHTER | AI_ROLE_FLYER | AI_ROLE_HITS_GROUND | AI_ROLE_HITS_AIR, 0 },
    { MT_IMP_SKY_FORTRESS, AI_ROLE_FIGHTER | AI_ROLE_FLYER | AI_ROLE_HITS_GROUND, AI_ROLE_HITS_AIR },
    { MT_FG_FREIGHTER, AI_ROLE_WORKER, AI_ROLE_FIGHTER },
    { MT_IMP_GROUND_TRANSPORTER, AI_ROLE_WORKER, AI_ROLE_FIGHTER },
    { MT_FG_GUARD_TOWER, AI_ROLE_DEFENSE | AI_ROLE_HITS_GROUND | AI_ROLE_HITS_AIR, AI_ROLE_FIGHTER },
    { MT_FG_AA_SITE, AI_ROLE_DEFENSE | AI_ROLE_HITS_AIR, AI_ROLE_HITS_GROUND },
    { MT_IMP_ADV_GUARD_TOWER, AI_ROLE_DEFENSE | AI_ROLE_HITS_GROUND, AI_ROLE_HITS_AIR },
    { MT_FG_MEDIC, AI_ROLE_SUPPORT, AI_ROLE_FIGHTER },
    { MT_FG_MECHANIC, AI_ROLE_SUPPORT, AI_ROLE_FIGHTER },
    { MT_IMP_AMPER, AI_ROLE_SUPPORT, AI_ROLE_FIGHTER },
    { MT_FG_SNIPER, AI_ROLE_FIGHTER | AI_ROLE_CLOAKED, 0 },
    { MT_FG_SPY, AI_ROLE_CLOAKED, AI_ROLE_FIGHTER },
    { MT_IMP_SPY, AI_ROLE_CLOAKED, AI_ROLE_FIGHTER },
    { MT_FG_RAIDER, 0, AI_ROLE_CLOAKED | AI_ROLE_SUPPORT },
};

int main(void) {
    DCHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {0};
    DCHECK(model && rts_game_model_load(model, &config));
    RTS_RUN(doctrine_roles(roles, (int)(sizeof(roles) / sizeof(*roles))));
    doctrine_army_t fg, imp;
    RTS_RUN(doctrine_mix(&fg, &imp));
    /* The Imperium fields fewer, heavier units than the Freedom Guard. */
    DCHECK(imp.strength / imp.fighters > fg.strength / fg.fighters);
    RTS_RUN(doctrine_weights());
    RTS_RUN(doctrine_waves());
    rts_game_model_destroy(model);
    SDL_Quit();
    puts("PASS: dark-reign doctrines: roles, army mix by weight, workers, defenses, held and launched waves");
    return 0;
}
