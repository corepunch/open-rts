#include "game.h"
#include "kknd.h"
#include "info.h"
#include "p_ai.h"
#include "rts_test.h"
#define CHECK(c) RTS_CHECK(c, "kknd ai", #c)

static mobj_t *spawn_owner(uint16_t type, int owner, fvec2_t at) {
    mobj_t *u = P_SpawnMobj(fixed3_from_fvec2(at, 0), type);
    if (u) { u->owner = u->team = owner; u->allegiance = owner ? ALLEGIANCE_ENEMY : ALLEGIANCE_PLAYER; }
    return u;
}

static int count(int owner, uint16_t type) {
    int n = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *u = (mobj_t *)th;
        if (th->function == P_MobjThinker && u->owner == owner && u->type_id == type &&
            u->hp > 0 && !u->remove) n++;
    }
    return n;
}

static void run(AiContext *ai, int ticks) {
    for (int t = 0; t < ticks; ++t) {
        mobjlist_t list = P_ListMobjs();
        P_AiTick(ai, &level, list.items, list.count, gameinfo, 1000);
        P_FreeMobjList(&list);
        G_ProductionTicker(1.0f);
    }
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = "data/KKND" };
    CHECK(model && rts_game_model_load(model, &config));
    P_FreeLevel(&level);
    P_InitThinkers();
    level.width = level.height = 96;
    level.blocked = calloc(96 * 96, 1);
    CHECK(level.blocked && G_AiInterface());
    /* A human Survivor, a Survivor computer player and a Mutant computer player,
     * each with only the mobile outpost that unpacks the base. */
    CHECK(spawn_owner(MT_SURV_MOBILE_OUTPOST, 0, (fvec2_t){10, 10}));
    CHECK(spawn_owner(MT_SURV_MOBILE_OUTPOST, 1, (fvec2_t){40, 10}));
    CHECK(spawn_owner(MT_MUTE_CLANHALL_WAGON, 2, (fvec2_t){70, 10}));
    for (int owner = 0; owner < 3; ++owner) level.player_resources[owner][0] = 60000;
    AiContext ai;
    P_AiInit(&ai);
    P_AiAttachGame(&ai, G_AiInterface());
    P_AiSetFeatures(&ai, AI_FEATURE_ECONOMY | AI_FEATURE_PRODUCTION | AI_FEATURE_RESEARCH);
    run(&ai, 900);
    CHECK(level.player_resources[0][0] == 60000 && P_AiStats(&ai, 0)->purchases == 0);
    CHECK(count(0, MT_SURV_OUTPOST) == 0);
    /* Survivors unpack an outpost and machine shop, then field an army. */
    CHECK(count(1, MT_SURV_OUTPOST) == 1 && count(1, MT_SURV_MACHINE_SHOP) == 1);
    CHECK(count(1, MT_SURV_RIFLEMAN) >= 3 && count(1, MT_SURV_OIL_TANKER) >= 1);
    CHECK(count(1, MT_MUTE_BERSERKER) == 0);
    /* The Mutants follow their own column of the shared ladder. */
    CHECK(count(2, MT_MUTE_CLANHALL) == 1 && count(2, MT_MUTE_BLACKSMITH) == 1);
    CHECK(count(2, MT_MUTE_BERSERKER) >= 3 && count(2, MT_MUTE_OIL_TANKER) >= 1);
    CHECK(count(2, MT_SURV_RIFLEMAN) == 0);
    /* Spending stays bounded once the early ladder is done. */
    int oil1 = level.player_resources[1][0], oil2 = level.player_resources[2][0];
    run(&ai, 60);
    CHECK(level.player_resources[1][0] >= oil1 - 3000 && level.player_resources[2][0] >= oil2 - 3000);
    /* Losses are replaced. */
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (th->function == P_MobjThinker && u->owner == 1 && u->type_id == MT_SURV_MACHINE_SHOP) {
            P_RemoveMobj(u);
            break;
        }
    }
    CHECK(count(1, MT_SURV_MACHINE_SHOP) == 0);
    run(&ai, 120);
    CHECK(count(1, MT_SURV_MACHINE_SHOP) == 1);
    P_FreeLevel(&level);
    rts_game_model_destroy(model);
    SDL_Quit();
    puts("PASS: kknd computer players build both factions' ladders and replace losses");
    return 0;
}
