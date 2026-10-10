#ifndef __P_AI_LOCAL__
#define __P_AI_LOCAL__

/* Shared between the computer player's scheduler (p_ai.c) and its doctrine
 * layer (p_ai_doctrine.c). Not part of the engine API. */

#include "engine.h"

typedef enum {
    AI_TRY_SKIP = 0, /* Nothing to do for this product now; look further. */
    AI_TRY_BOUGHT,
    AI_TRY_SAVING,   /* Short of credits or waiting on research: stop spending. */
} AiTry;

/* A town is a cluster of drop-offs: a second hatchery or a lumber mill
 * beside the hall feeds the same workers, an expansion does not. */
#define AI_TOWN_RADIUS FIXED_LIT(12.0)

void P_AiEmit(AiContext *ctx, AiEventType type, int owner, int value);
/* Units a game job or the scout occupies: economy, defense and waves leave them be. */
bool P_AiIsBusy(const AiContext *ctx, const mobj_t *unit);
bool P_AiIsAlly(const level_t *map, const AiTeamState *team, int owner, const mobj_t *other);
/* One purchase attempt, with research for a goal that needs tech. */
AiTry P_AiTry(AiContext *ctx, AiTeamState *team, int owner, level_t *map, int product);

/* Updates the team's memory of the enemy army from what it can see. */
void P_AiScout(AiContext *ctx, AiTeamState *team, int owner, const level_t *map,
               mobj_t *const *units, int unit_count, int elapsed_ms);
/* Sends the doctrine's scout to the start locations and brings it home
 * once an enemy base has been seen. */
void P_AiSendScout(AiContext *ctx, AiTeamState *team, int owner, level_t *map,
                   mobj_t *const *units, int unit_count);
/* Orders supply ahead of demand. True when the team must save for it. */
bool P_AiBuySupply(AiContext *ctx, AiTeamState *team, int owner, level_t *map, int elapsed_ms);
/* Founds a town once each standing one has expand_workers. True when the
 * team must save for it. */
bool P_AiExpand(AiContext *ctx, AiTeamState *team, int owner, level_t *map);
/* Serves the doctrine's needs after the opening. Returns purchases made. */
int  P_AiBuyDoctrine(AiContext *ctx, AiTeamState *team, int owner, level_t *map, int budget);
/* Whether an idle army may leave, per doctrine.attack_ratio. */
bool P_AiArmyReady(const AiContext *ctx, AiTeamState *team,
                   mobj_t *const *army, int army_count, int max_size);
void P_AiTrackWave(AiTeamState *team, mobj_t *const *sent, int sent_count);
/* Pulls a losing wave home, per doctrine.retreat_ratio. */
void P_AiCheckRetreat(AiContext *ctx, AiTeamState *team, int owner, level_t *map,
                      mobj_t *const *units, int unit_count);

#endif
