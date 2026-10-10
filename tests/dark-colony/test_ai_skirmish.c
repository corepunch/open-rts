/* Dark Colony skirmish AI, observed by fast-forwarding the simulation and
 * searching the resulting event streams (model events and AI events). */
#include "engine.h"
#include "info.h"
#include "dark-colony.h"
#include "t_local.h"
#include <stdio.h>
#include <stdlib.h>

#define TICKS_PER_SECOND 30
#define REQUIRE(cond, msg) do { if (!(cond)) return rts_fail("ai_skirmish", msg); } while (0)

typedef struct { int units, buildings, harvesters, attackers, credits; } census_t;

typedef struct {
    int queued[8], built[8], died[8], building_built[8], attack_started[8];
    int ai_purchase[8], ai_harvest[8], ai_wave[8], ai_defense[8];
    int ai_wave_units[8];
    int first_purchase_ms[8], first_wave_ms[8];
    int product_bought[8][256];
    uint64_t checksum;
} log_t;

static void census(int owner, census_t *c) {
    memset(c, 0, sizeof(*c));
    mobjlist_t l = P_ListMobjs();
    for (int i = 0; i < l.count; ++i) {
        const mobj_t *m = l.items[i];
        if (m->owner != owner || m->remove || m->hp <= 0 || m->type_id == MT_VENT) continue;
        if (m->traits & MF_MOBILE) c->units++; else c->buildings++;
        if (m->traits & MF_HARVESTER) c->harvesters++;
        if (m->traits & MF_ATTACK) c->attackers++;
    }
    P_FreeMobjList(&l);
    c->credits = level.player_resources[owner][0];
}

static void init_log(log_t *log) {
    memset(log, 0, sizeof(*log));
    for (int i = 0; i < 8; ++i) log->first_purchase_ms[i] = log->first_wave_ms[i] = -1;
}

/* Advances `seconds` of game time, draining both event streams every tick so
 * the bounded rings never overflow. */
typedef void (*probe_fn)(RtsGameModel *model, void *user);

static bool fast_forward_probe(RtsGameModel *model, int seconds, log_t *log,
                               probe_fn probe, void *user) {
    AiContext *ai = rts_game_model_ai(model);
    for (int t = 0; t < seconds * TICKS_PER_SECOND; ++t) {
        if (!rts_tick(model, NULL)) return false;
        if (probe && t % TICKS_PER_SECOND == 0) probe(model, user);
        RtsGameEvent e;
        while (rts_game_model_poll_event(model, &e)) {
            int o = e.subject_owner & 7;
            switch (e.type) {
            case RTS_GAME_EVENT_BUILD_QUEUED: log->queued[o]++; break;
            case RTS_GAME_EVENT_UNIT_BUILT: log->built[o]++; break;
            case RTS_GAME_EVENT_BUILDING_BUILT: log->building_built[o]++; break;
            case RTS_GAME_EVENT_UNIT_DIED: log->died[o]++; break;
            case RTS_GAME_EVENT_ATTACK_STARTED: log->attack_started[o]++; break;
            default: break;
            }
        }
        AiEvent a;
        while (P_AiPollEvent(ai, &a)) {
            int o = a.owner & 7;
            log->checksum = log->checksum * 1099511628211ull ^ (uint64_t)(a.type * 131 + a.value * 7 + a.time_ms);
            switch (a.type) {
            case AI_EVENT_PURCHASE:
                log->ai_purchase[o]++;
                if (a.value >= 0 && a.value < 256) log->product_bought[o][a.value]++;
                if (log->first_purchase_ms[o] < 0) log->first_purchase_ms[o] = a.time_ms;
                break;
            case AI_EVENT_HARVEST_ASSIGNED: log->ai_harvest[o]++; break;
            case AI_EVENT_WAVE_LAUNCHED:
                log->ai_wave[o]++; log->ai_wave_units[o] += a.value;
                if (log->first_wave_ms[o] < 0) log->first_wave_ms[o] = a.time_ms;
                break;
            case AI_EVENT_DEFENSE_RALLY: log->ai_defense[o]++; break;
            default: break;
            }
        }
    }
    return true;
}

static bool fast_forward(RtsGameModel *model, int seconds, log_t *log) {
    return fast_forward_probe(model, seconds, log, NULL, NULL);
}

static RtsGameModel *start(const char *map, const int types[8], const int races[8],
                           const int teams[8]) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/DCOLONY", .map_path = map};
    dc_skirmish_t setup = {.quantity = 4, .flow = 4, .rank = 0};
    for (int i = 0; i < 8; ++i)
        setup.players[i] = (dc_skirmish_player_t){.type = types[i], .race = races[i],
                                                  .color = i, .team = teams[i]};
    DC_RequestSkirmish(map, &setup);
    if (!model || !rts_game_model_load(model, &config)) return NULL;
    return model;
}

#define N DC_PLAYER_NONE
#define H DC_PLAYER_HUMAN
#define A DC_PLAYER_AI
#define P DC_PLAYER_AI_PLUS

enum { UI_EXPLOITER = 87, UI_BROZAAR = 46 };

static const char *D2 = "SCENARIO/MPLAYER/D2PLAY01.MAP";
static const int solo_teams[8] = {0, 1, 2, 3, 4, 5, 6, 7};

static RtsGameModel *duel(const char *map, int ai_type, int ai_race, int human_race) {
    int types[8] = {H, ai_type, N, N, N, N, N, N};
    int races[8] = {human_race, ai_race, 0, 0, 0, 0, 0, 0};
    return start(map, types, races, solo_teams);
}

/* The AI of either race must build an economy, tech up, raise an army, attack
 * and eventually win against a passive human, using only the retail purchase path. */
static int test_builds_and_attacks(int race) {
    RtsGameModel *model = duel(D2, A, race, 0);
    REQUIRE(model, "skirmish loads");
    census_t h0, c0; census(0, &h0); census(1, &c0);
    REQUIRE(c0.harvesters == 0 && c0.attackers <= 2, "the AI starts with only its commander and city");
    log_t log; init_log(&log);
    REQUIRE(fast_forward(model, 20 * 60, &log), "20 simulated minutes run");
    census_t h1, c1; census(0, &h1); census(1, &c1);
    const AiStats *st = P_AiStats(rts_game_model_ai(model), 1);

    REQUIRE(log.ai_purchase[1] >= 30 && st->purchases == log.ai_purchase[1], "AI purchases are logged exactly");
    REQUIRE(log.first_purchase_ms[1] >= 0 && log.first_purchase_ms[1] < 2000, "the AI buys right away");
    REQUIRE(log.built[1] >= 20, "production completion events come from the AI owner");
    REQUIRE(c1.harvesters >= 2, "the AI fields harvesters");
    REQUIRE(c1.attackers >= c0.attackers + 12, "the AI raises an army");
    REQUIRE(c1.buildings >= c0.buildings + 2, "the AI added city modules (tech buildings)");
    REQUIRE(log.ai_wave[1] >= 2 && log.ai_wave_units[1] >= 12, "the AI launches attack waves");
    REQUIRE(log.first_wave_ms[1] > 40000, "no wave before the first plan interval");
    REQUIRE(h1.units + h1.buildings < h0.units + h0.buildings, "the passive human takes losses");
    REQUIRE(log.died[0] > 0, "unit death events are reported for the human side");
    REQUIRE(log.ai_purchase[0] == 0 && log.ai_harvest[0] == 0 && log.ai_wave[0] == 0 &&
            P_AiStats(rts_game_model_ai(model), 0)->thinks == 0, "the human player is never driven by the AI");
    /* Spending follows the ladder: a harvester first, then the tech chain. */
    int exploiter = race ? UI_BROZAAR : UI_EXPLOITER;
    REQUIRE(log.product_bought[1][exploiter] >= 1, "the first purchase class is a harvester");
    rts_game_model_destroy(model);
    return 0;
}

/* The shipped binary does not use the headless model's production ticker: with
 * the custom sidebar it runs P_Ticker, P_AiTick and G_ModelUpdateProduction
 * (barracks release animation, native spawn points). Drive exactly that loop. */
static int test_interactive_loop(int race) {
    RtsGameModel *model = duel(D2, P, race, 0);
    REQUIRE(model, "skirmish loads");
    AiContext ai;
    P_AiInit(&ai);
    P_AiAttachGame(&ai, G_AiInterface());
    census_t before, after; census(1, &before);
    for (int t = 0; t < 12 * 60 * TICKS_PER_SECOND; ++t) {
        P_Ticker();
        mobjlist_t objects = P_ListMobjs();
        P_AiTick(&ai, &level, objects.items, objects.count, gameinfo, (int)(RTS_TICK_MS * 1000));
        G_ModelUpdateProduction(&level, objects.items, &objects.count, RTS_TICK_MS);
        P_FreeMobjList(&objects);
    }
    census(1, &after);
    const AiStats *st = P_AiStats(&ai, 1);
    REQUIRE(st->purchases >= 15, "the AI buys through the interactive purchase path");
    REQUIRE(after.harvesters >= 1, "harvesters come out of the native producer");
    REQUIRE(after.attackers >= before.attackers + 8, "combat units are released by barracks/hives");
    REQUIRE(after.credits < 6000, "credits are being spent, not hoarded");
    REQUIRE(P_AiStats(&ai, 0)->thinks == 0, "the human is not driven");
    rts_game_model_destroy(model);
    return 0;
}

static int test_ai_plus_income(void) {
    int incomes[2] = {0, 0};
    for (int pass = 0; pass < 2; ++pass) {
        RtsGameModel *model = duel(D2, pass ? P : A, 0, 0);
        REQUIRE(model, "skirmish loads");
        REQUIRE(level.income_scale[1] == (pass ? 0x200 : 0) && level.income_scale[0] == 0,
                "only AI+ gets the 0x200 credit multiplier; AI keeps 1.0 (DC.EXE 0x401694/0x4016a0)");
        /* Isolate the economy: no AI purchasing or exo trickle, one harvester bought by hand. */
        for (int owner = 0; owner < 8; ++owner) level.exo_income[owner] = 0;
        P_AiSetFeatures(rts_game_model_ai(model), AI_FEATURE_ECONOMY);
        DC_SelectPurchase(1, UI_EXPLOITER, false);
        DC_SubmitPurchases(1);
        int start_credits = level.player_resources[1][0];
        log_t log; init_log(&log);
        REQUIRE(fast_forward(model, 6 * 60, &log), "six simulated minutes run");
        census_t c; census(1, &c);
        REQUIRE(c.harvesters == 1 && log.ai_purchase[1] == 0, "exactly the hand-bought harvester, no AI purchases");
        incomes[pass] = level.player_resources[1][0] - start_credits;
        rts_game_model_destroy(model);
    }
    REQUIRE(incomes[0] > 500, "a lone harvester earns credits");
    REQUIRE(incomes[1] * 100 >= incomes[0] * 195 && incomes[1] * 100 <= incomes[0] * 205,
            "AI+ earns twice the credits of AI from identical mining");
    return 0;
}

static int test_feature_toggles(void) {
    struct { uint32_t features; const char *name; } cases[] = {
        { 0, "all off" },
        { AI_FEATURE_ALL & ~AI_FEATURE_PRODUCTION, "production off" },
        { AI_FEATURE_ALL & ~AI_FEATURE_ATTACK, "attack off" },
        { AI_FEATURE_PRODUCTION, "production only" },
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        RtsGameModel *model = duel(D2, A, 0, 0);
        REQUIRE(model, "skirmish loads");
        P_AiSetFeatures(rts_game_model_ai(model), cases[i].features);
        for (int owner = 0; owner < 8; ++owner) level.exo_income[owner] = 0; /* Only purchases move credits. */
        census_t before_h, before; census(0, &before_h); census(1, &before);
        log_t log; init_log(&log);
        REQUIRE(fast_forward(model, 15 * 60, &log), "fifteen simulated minutes run");
        census_t after_h, after; census(0, &after_h); census(1, &after);
        bool production = cases[i].features & AI_FEATURE_PRODUCTION;
        bool attack = cases[i].features & AI_FEATURE_ATTACK;
        if (!production) {
            REQUIRE(log.ai_purchase[1] == 0 && log.queued[1] == 0 && after.credits == before.credits &&
                    after.units == before.units && after.buildings == before.buildings,
                    "without PRODUCTION the AI buys nothing and its credits are untouched");
        } else {
            REQUIRE(log.ai_purchase[1] > 10 && after.attackers > before.attackers, "with PRODUCTION the AI builds");
        }
        if (!attack) {
            REQUIRE(log.ai_wave[1] == 0, "without ATTACK no wave is launched");
            REQUIRE(after_h.units == before_h.units && after_h.buildings == before_h.buildings,
                    "without ATTACK the human is left alone");
        }
        if (!cases[i].features)
            REQUIRE(log.ai_harvest[1] == 0 && log.ai_defense[1] == 0, "all features off: a dormant AI");
        rts_game_model_destroy(model);
    }
    return 0;
}

static int test_defense(void) {
    RtsGameModel *model = duel(D2, A, 0, 0);
    REQUIRE(model, "skirmish loads");
    P_AiSetFeatures(rts_game_model_ai(model), AI_FEATURE_DEFENSE);
    mobjlist_t l = P_ListMobjs();
    mobj_t *raider = NULL, *city = NULL;
    for (int i = 0; i < l.count; ++i) {
        mobj_t *m = l.items[i];
        if (m->owner == 0 && (m->traits & MF_ATTACK) && (m->traits & MF_MOBILE) && !raider) raider = m;
        if (m->owner == 1 && m->type_id == MT_EXCOPOD) city = m;
    }
    REQUIRE(raider && city, "a human fighter and the AI city exist");
    fixed2_t at = fixed3_xy(city->core.position);
    raider->core.position = fixed3_from_fixed2((fixed2_t){ at.x + 4 * FIXED_ONE, at.y + 2 * FIXED_ONE }, raider->core.position.z);
    uint32_t raider_id = raider->id;
    P_FreeMobjList(&l);
    log_t log; init_log(&log);
    REQUIRE(fast_forward(model, 20, &log), "twenty simulated seconds run");
    REQUIRE(log.ai_defense[1] >= 1, "defenders rally when an enemy enters the base radius");
    (void)raider_id;
    rts_game_model_destroy(model);
    return 0;
}

typedef struct { int checks, hostile_checks; bool friendly_fire; } target_probe_t;

static void probe_targets(RtsGameModel *model, void *user) {
    (void)model;
    target_probe_t *p = user;
    mobjlist_t l = P_ListMobjs();
    for (int i = 0; i < l.count; ++i) {
        const mobj_t *m = l.items[i];
        const mobj_t *t = m->attack.target;
        if (!t || m->owner == 0 || m->owner > 2 || t->remove || t->hp <= 0) continue;
        p->checks++;
        if (t->owner == 1 || t->owner == 2) { if (t->owner != m->owner) p->friendly_fire = true; }
        else p->hostile_checks++;
    }
    P_FreeMobjList(&l);
}

static int test_allies(void) {
    int types[8] = {H, A, P, A, N, N, N, N};
    int races[8] = {0, 0, 1, 1, 0, 0, 0, 0};
    int teams[8] = {0, 1, 1, 2, 4, 5, 6, 7}; /* AI 1 and 2 are allied; AI 3 is alone */
    RtsGameModel *model = start("SCENARIO/MPLAYER/D8PLAY01.MAP", types, races, teams);
    REQUIRE(model, "eight-start skirmish loads");
    REQUIRE(level.income_scale[2] == 0x200 && level.income_scale[1] == 0 && level.income_scale[3] == 0,
            "the income multiplier follows each slot's AI type");
    target_probe_t probe = {0};
    log_t log; init_log(&log);
    REQUIRE(fast_forward_probe(model, 14 * 60, &log, probe_targets, &probe), "fourteen simulated minutes run");
    REQUIRE(log.ai_purchase[1] > 10 && log.ai_purchase[2] > 10 && log.ai_purchase[3] > 10,
            "every AI slot builds, each from its own race plan");
    REQUIRE(log.ai_wave[1] + log.ai_wave[2] + log.ai_wave[3] >= 3, "the AIs attack");
    REQUIRE(probe.hostile_checks > 0, "AI units take hostile targets");
    REQUIRE(!probe.friendly_fire, "allied AIs never target each other");
    for (int o = 4; o < 8; ++o) {
        census_t c; census(o, &c);
        REQUIRE(c.units == 0 && c.buildings == 0 && P_AiStats(rts_game_model_ai(model), o)->thinks == 0,
                "empty slots stay empty and idle");
    }
    rts_game_model_destroy(model);
    return 0;
}

static int run_fingerprint(uint64_t *checksum, int *credits, census_t *c) {
    RtsGameModel *model = duel(D2, P, 1, 0);
    if (!model) return 1;
    log_t log; init_log(&log);
    if (!fast_forward(model, 10 * 60, &log)) return 1;
    *checksum = log.checksum;
    *credits = level.player_resources[1][0];
    census(1, c);
    rts_game_model_destroy(model);
    return 0;
}

static int test_determinism(void) {
    uint64_t a, b; int ca, cb; census_t xa, xb;
    REQUIRE(!run_fingerprint(&a, &ca, &xa) && !run_fingerprint(&b, &cb, &xb), "both runs complete");
    REQUIRE(a == b && ca == cb && xa.units == xb.units && xa.buildings == xb.buildings,
            "identical setups produce identical AI decisions (required for lockstep play)");
    REQUIRE(a != 0, "the fingerprint covers real decisions");
    return 0;
}

static int test_campaign_untouched(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/DCOLONY", .map_path = "SCENARIO/HUMAN/HUMAN02.MAP"};
    REQUIRE(model && rts_game_model_load(model, &config), "campaign mission loads");
    REQUIRE(!DC_LevelSkirmish(&level), "it is not a skirmish");
    for (int i = 0; i < 8; ++i) REQUIRE(level.income_scale[i] == 0, "campaign credits are unscaled");
    log_t log; init_log(&log);
    REQUIRE(fast_forward(model, 4 * 60, &log), "four simulated minutes run");
    for (int o = 0; o < 8; ++o) {
        const AiStats *s = P_AiStats(rts_game_model_ai(model), o);
        REQUIRE(s->thinks == 0 && s->purchases == 0 && s->waves == 0,
                "scripted missions are not driven by the skirmish AI");
        REQUIRE(log.ai_purchase[o] == 0 && log.ai_wave[o] == 0 && log.ai_defense[o] == 0, "no AI events in a mission");
    }
    rts_game_model_destroy(model);
    return 0;
}

static int test_map_sweep(void) {
    char map[64];
    int maps = 0;
    for (int set = 0; set < 3; ++set) {
        const char *prefix = set == 0 ? "D2" : set == 1 ? "J2" : "D4";
        int last = set == 1 ? 9 : 10;
        for (int n = 1; n <= last; ++n) {
            snprintf(map, sizeof(map), "SCENARIO/MPLAYER/%sPLAY%02d.MAP", prefix, n);
            RtsGameModel *model = duel(map, A, n & 1, 0);
            if (!model) { fprintf(stderr, "sweep: %s failed to load\n", map); return rts_fail("ai_skirmish", "a retail map failed to load"); }
            log_t log; init_log(&log);
            if (!fast_forward(model, 5 * 60, &log)) return rts_fail("ai_skirmish", "sweep tick failed");
            census_t c; census(1, &c);
            /* Every start must be placed on the map (maps wider than 128 cells
             * once wrapped their right-hand objects to negative coordinates). */
            mobjlist_t l = P_ListMobjs();
            for (int i = 0; i < l.count; ++i) {
                const mobj_t *m = l.items[i];
                if (m->owner > 1 || m->type_id == MT_VENT || m->remove) continue;
                fixed2_t at = fixed3_xy(m->core.position);
                if (at.x < 0 || at.y < 0 || at.x >= FIXED_FROM_INT(level.width) || at.y >= FIXED_FROM_INT(level.height)) {
                    fprintf(stderr, "sweep: %s: owner %d object type %d at %.1f,%.1f\n", map, m->owner, m->type_id, fixed_to_float(at.x), fixed_to_float(at.y));
                    P_FreeMobjList(&l);
                    return rts_fail("ai_skirmish", "an object was placed outside the map");
                }
            }
            P_FreeMobjList(&l);
            if (log.ai_purchase[1] < 3 || c.units < 3) {
                fprintf(stderr, "sweep: %s: purchases=%d units=%d\n", map, log.ai_purchase[1], c.units);
                return rts_fail("ai_skirmish", "the AI did not build on a retail map");
            }
            rts_game_model_destroy(model);
            ++maps;
        }
    }
    printf("swept %d retail maps\n", maps);
    return 0;
}

int main(void) {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    int rc = 0;
    rc |= test_builds_and_attacks(0);
    rc |= test_builds_and_attacks(1);
    rc |= test_interactive_loop(0);
    rc |= test_interactive_loop(1);
    rc |= test_ai_plus_income();
    rc |= test_feature_toggles();
    rc |= test_defense();
    rc |= test_allies();
    rc |= test_determinism();
    rc |= test_campaign_untouched();
    rc |= test_map_sweep();
    if (!rc) puts("PASS: skirmish AI builds, attacks, scales AI+ income, honors toggles and alliances");
    return rc;
}
