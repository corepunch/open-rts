#ifndef __P_AI__
#define __P_AI__

#include "actor.h"
#include "map.h"

/*
 * Universal computer-player layer.
 *
 * The engine owns scheduling, economy (harvester assignment), base defense,
 * attack waves and a goal-ladder production planner. A game attaches an
 * AiGameInterface that answers the game-specific questions (who is an AI and
 * of which strength, what to build in which order, how to buy it) and selects
 * which engine features run. With no interface attached the original generic
 * MF_RESOURCE_BASE behavior is retained for legacy callers.
 */

#define AI_MAX_TEAMS 8
#define AI_MAX_HARVEST_ASSIGNMENTS 32
#define AI_DEFENSE_RADIUS 15.0f
#define AI_ATTACK_WAVE_INTERVAL_MS 30000
#define AI_ATTACK_WAVE_MIN_SIZE 3
#define AI_ATTACK_WAVE_MAX_SIZE 8
#define AI_EVENT_LOG_SIZE 256
/* DC.EXE 0x419a54 runs the AI chooser only when (tick & 3) == 0. */
#define AI_THINK_INTERVAL_TICKS 4

typedef enum {
    AI_FEATURE_ECONOMY    = 1u << 0, /* assign idle harvesters to free vents */
    AI_FEATURE_PRODUCTION = 1u << 1, /* buy along the game's goal ladder */
    AI_FEATURE_DEFENSE    = 1u << 2, /* rally idle fighters on base intruders */
    AI_FEATURE_ATTACK     = 1u << 3, /* periodic attack waves */
    AI_FEATURE_ALL        = 0xFu,
} AiFeature;

typedef enum {
    AI_LEVEL_NONE = 0, /* Human, empty slot or scripted: the AI never acts. */
    AI_LEVEL_NORMAL = 1,
    AI_LEVEL_PLUS = 2,
} AiLevel;

typedef enum {
    AI_BUY_OK = 0,
    AI_BUY_BLOCKED,       /* missing prerequisite/producer or already owned */
    AI_BUY_NEED_CREDITS,  /* possible with more credits: the AI saves up */
} AiBuyStatus;

/* Keep at least `count` of `product` (alive plus queued) once `after_ms` of
 * game time has passed. Product ids are opaque to the engine. */
typedef struct {
    int product;
    int count;
    int after_ms;
} AiGoal;

#define AI_MAX_GOALS 96

typedef struct {
    AiGoal goals[AI_MAX_GOALS]; /* Priority order; earlier goals are served first. */
    int goal_count;
    int wave_interval_ms;       /* 0 selects AI_ATTACK_WAVE_INTERVAL_MS. */
    int wave_min_size;          /* 0 selects AI_ATTACK_WAVE_MIN_SIZE. */
    int wave_max_size;          /* 0 selects AI_ATTACK_WAVE_MAX_SIZE. */
} AiPlan;

typedef struct AiGameInterface {
    const char *name;
    uint32_t features; /* AiFeature mask the game enables. */
    /* AiLevel of an owner; AI_LEVEL_NONE owners are skipped entirely. */
    int  (*player_level)(const level_t *map, int owner);
    /* Fills the owner's goal ladder. Optional when PRODUCTION is disabled. */
    bool (*plan)(const level_t *map, int owner, int level, AiPlan *out);
    int  (*owned)(int owner, int product);  /* alive plus queued */
    int  (*can_purchase)(const level_t *map, int owner, int product);
    bool (*purchase)(level_t *map, int owner, int product);
    /* Base anchors for defense and wave objectives. Optional; the default
     * is MF_RESOURCE_BASE. */
    bool (*is_base)(const mobj_t *unit);
} AiGameInterface;

typedef enum {
    AI_EVENT_NONE = 0,
    AI_EVENT_HARVEST_ASSIGNED, /* value = vent index */
    AI_EVENT_PURCHASE,         /* value = product id */
    AI_EVENT_DEFENSE_RALLY,    /* value = defenders sent */
    AI_EVENT_WAVE_LAUNCHED,    /* value = units sent */
} AiEventType;

typedef struct {
    AiEventType type;
    int owner;
    int time_ms; /* AI clock: accumulated tick time */
    int value;
} AiEvent;

typedef struct {
    int harvest_orders;
    int purchases;
    int defense_rallies;
    int waves;
    int wave_units; /* total units sent in waves */
    int thinks;
} AiStats;

typedef struct {
    int vent_index;
    int slug_unit_index;
} AiHarvestAssignment;

typedef struct {
    fvec2_t base_position;
    bool has_base;
    int combat_unit_count;
    int harvester_count;
    AiHarvestAssignment harvest_assignments[AI_MAX_HARVEST_ASSIGNMENTS];
    int harvest_assignment_count;
    int attack_wave_timer_ms;
    int attack_wave_size;
    bool attack_wave_active;
    uint8_t allegiance;
    int level;          /* AiLevel; legacy mode uses AI_LEVEL_NORMAL */
    AiPlan plan;
    bool plan_loaded;
    AiStats stats;
} AiTeamState;

typedef struct AiContext {
    AiTeamState teams[AI_MAX_TEAMS];
    bool initialized;
    const AiGameInterface *game; /* NULL keeps the legacy generic behavior. */
    uint32_t features;           /* Active mask; defaults to the game's. */
    int clock_ms;
    int think_counter;
    AiEvent events[AI_EVENT_LOG_SIZE]; /* Ring; oldest entries are overwritten. */
    int event_head;
    int event_count;
    int events_dropped;
} AiContext;

void P_AiInit(AiContext *ctx);
/* Attaches a game's interface and adopts its feature mask (NULL detaches). */
void P_AiAttachGame(AiContext *ctx, const AiGameInterface *game);
/* Runtime toggles, e.g. for a difficulty menu or tests. */
void P_AiSetFeatures(AiContext *ctx, uint32_t features);
void P_AiTick(AiContext *ctx, level_t *map, mobj_t *const *units, int unit_count,
              const gameinfo_t *game_info, int dt_ms);

/* Pops the oldest retained AI event. Returns false when the log is empty. */
bool P_AiPollEvent(AiContext *ctx, AiEvent *out);
const AiStats *P_AiStats(const AiContext *ctx, int owner);

#endif
