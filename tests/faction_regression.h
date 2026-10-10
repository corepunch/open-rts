#ifndef __FACTION_REGRESSION__
#define __FACTION_REGRESSION__
/* A computer player of each faction in g_ruleset, on an empty level with
 * credits to spare: it follows its own opening and then its doctrine's
 * roster, and never fields the other side's units. The including test
 * defines FACTION_STARTERS, the actor type each faction starts with. */
#include "engine.h"
#include "info.h"
#include "t_local.h"
#ifdef RTS_GAME_KKND
void A_KkndResearch(mobj_t *actor);
#endif

#define CHECK(c) RTS_CHECK(c, g_game_id, #c)

static const uint16_t starters[] = FACTION_STARTERS;
enum { STARTER_COUNT = (int)(sizeof(starters) / sizeof(*starters)) };

static mobj_t *spawn_owner(uint16_t type, int owner, fvec2_t position) {
    mobj_t *unit = P_SpawnMobj(fixed3_from_fixed2(fixed2_from_fvec2(position), 0), type);
    if (!unit) return NULL;
    unit->owner = unit->team = owner;
    unit->allegiance = owner ? ALLEGIANCE_ENEMY : ALLEGIANCE_PLAYER;
    return unit;
}

static void ai_run(AiContext *ai, int ticks, int dt_ms) {
    for (int tick = 0; tick < ticks; ++tick) {
        mobjlist_t list = P_ListMobjs();
        P_AiTick(ai, &level, list.items, list.count, gameinfo, dt_ms);
        P_FreeMobjList(&list);
        G_ProductionTicker(dt_ms);
#ifdef RTS_GAME_KKND
        for (int tic = 0; tic < RTS_TICRATE; ++tic)
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
                if (th->function == P_MobjThinker) A_KkndResearch((mobj_t *)th);
#endif
    }
}

/* Does `faction`'s opening or roster make an actor of `type`? */
static bool faction_makes(const AiContext *ai, const faction_t *faction, uint16_t type) {
    for (int i = 0; i < faction->opening_count; ++i)
        if (P_AiProductActor(ai, faction->opening[i].product) == type) return true;
    for (int i = 0; i < faction->doctrine.roster_count; ++i)
        if (P_AiProductActor(ai, faction->doctrine.roster[i].product) == type) return true;
    return false;
}

/* The fighters an opening orders by itself: its largest count of each. */
static int ladder_fighters(const AiContext *ai, const faction_t *faction) {
    int total = 0;
    for (int i = 0; i < faction->opening_count; ++i) {
        int product = faction->opening[i].product, count = faction->opening[i].count, repeated = 0;
        for (int j = 0; j < i; ++j) repeated |= faction->opening[j].product == product;
        if (repeated) continue;
        for (int j = i + 1; j < faction->opening_count; ++j)
            if (faction->opening[j].product == product && faction->opening[j].count > count)
                count = faction->opening[j].count;
        AiUnitInfo info;
        int actor = P_AiProductActor(ai, product);
        if (actor <= 0) continue;
        P_AiUnitInfo(ai, (uint16_t)actor, &info);
        if (info.roles & AI_ROLE_FIGHTER) total += count;
    }
    return total;
}

static int test_factions(void) {
    CHECK(g_ruleset.faction_count == STARTER_COUNT && STARTER_COUNT >= 1);
    P_FreeLevel(&level);
    P_InitThinkers();
    level.width = level.height = 96;
    level.blocked = calloc(96 * 96, 1);
    CHECK(level.blocked && G_AiInterface());
    /* A human of the first faction, then one computer player per faction. */
    CHECK(spawn_owner(starters[0], 0, (fvec2_t){10, 10}));
    level.player_resources[0][0] = 60000;
    for (int f = 0; f < STARTER_COUNT; ++f) {
        CHECK(spawn_owner(starters[f], 1 + f, (fvec2_t){20.0f + 14.0f * (float)f, 40}));
        level.player_resources[1 + f][0] = 1000000;
    }
    AiContext ai;
    P_AiInit(&ai);
    P_AiAttachGame(&ai, G_AiInterface());
    P_AiSetFeatures(&ai, AI_FEATURE_ECONOMY | AI_FEATURE_PRODUCTION | AI_FEATURE_RESEARCH | AI_FEATURE_DOCTRINE);
    ai_run(&ai, 1200, 1000);
    CHECK(level.player_resources[0][0] == 60000 && P_AiStats(&ai, 0)->purchases == 0);
    for (int f = 0; f < STARTER_COUNT; ++f) {
        int owner = 1 + f;
        const faction_t *faction = &g_ruleset.factions[f];
        /* The owner is mapped to its faction, which fills the plan. */
        CHECK(R_OwnerFaction(&level, owner) == faction);
        CHECK(faction->name && faction->opening_count > 0 && faction->doctrine.roster_count > 0);
        const AiTeamState *team = &ai.teams[owner];
        CHECK(team->plan_loaded && team->plan.goal_count == faction->opening_count);
        CHECK(team->plan.doctrine.roster_count == faction->doctrine.roster_count);
        CHECK(P_AiStats(&ai, owner)->purchases > faction->opening_count / 2);
        int fighters = 0, foreign = 0;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            const mobj_t *u = (mobj_t *)th;
            if (th->function != P_MobjThinker || u->owner != owner || u->hp <= 0 || u->remove) continue;
            AiUnitInfo info;
            P_AiUnitInfo(&ai, u->type_id, &info);
            if (info.roles & AI_ROLE_FIGHTER) ++fighters;
            for (int other = 0; other < g_ruleset.faction_count; ++other)
                if (other != f && faction_makes(&ai, &g_ruleset.factions[other], u->type_id) &&
                    !faction_makes(&ai, faction, u->type_id) && u->type_id != starters[f]) ++foreign;
        }
        CHECK(foreign == 0);
        /* Past its ladder, the doctrine keeps raising the army. */
        CHECK(fighters > ladder_fighters(&ai, faction));
    }
    P_FreeLevel(&level);
    printf("PASS: %d %s faction(s) follow their openings, then their doctrine rosters\n",
           STARTER_COUNT, g_game_id);
    return 0;
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = g_game_default_root };
    CHECK(model && rts_game_model_load(model, &config));
    RTS_RUN(test_factions());
    rts_game_model_destroy(model);
    SDL_Quit();
    return 0;
}
#endif
