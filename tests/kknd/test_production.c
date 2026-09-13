#include "game.h"
#include "info.h"
#include <assert.h>

int main(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/KKND"};
    assert(model && rts_game_model_load(model, &config));
    mobj_t *base = P_SpawnMobj(fixed3_zero(), MT_MUTE_CLANHALL);
    assert(base);
    base->owner = 1;
    base->team = 1;
    base->allegiance = ALLEGIANCE_ENEMY;
    StaticProductDefinition products[64];
    assert(G_ModelGetProducts(model, 0, products, 64) == 0);
    assert(G_ModelGetProducts(model, 1, products, 64) == 0);
    assert(!G_ModelProductByUIId(model, 20));
    mobjlist_t before = P_ListMobjs();
    int count = before.count;
    P_FreeMobjList(&before);
    int oil = level.player_resources[1][0];
    for (int i = 0; i < 60; ++i) G_ModelAIProduction(model, 1000);
    mobjlist_t after = P_ListMobjs();
    assert(after.count == count && level.player_resources[1][0] == oil);
    P_FreeMobjList(&after);
    rts_game_model_destroy(model);
    puts("PASS: empty native production table leaves AI units and oil unchanged");
    return 0;
}
