#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(c) RTS_CHECK(c,"StarCraft computer player",#c)

/* What the shared AI reads from units.dat and weapons.dat through the
 * game's describe(): roles, anti-air and Brood War strengths. */
static int unit_knowledge(const AiContext *ai) {
    AiUnitInfo ling, hydra, zealot, goliath, overlord, observer, scv, marine, turret, scourge, creep;
    P_AiUnitInfo(ai, MT_ZERGLING, &ling);
    P_AiUnitInfo(ai, MT_HYDRALISK, &hydra);
    P_AiUnitInfo(ai, MT_ZEALOT, &zealot);
    P_AiUnitInfo(ai, MT_GOLIATH, &goliath);
    P_AiUnitInfo(ai, MT_OVERLORD, &overlord);
    P_AiUnitInfo(ai, MT_OBSERVER, &observer);
    P_AiUnitInfo(ai, MT_SCV, &scv);
    P_AiUnitInfo(ai, MT_MARINE, &marine);
    P_AiUnitInfo(ai, MT_MISSILE_TURRET, &turret);
    P_AiUnitInfo(ai, MT_SCOURGE, &scourge);
    P_AiUnitInfo(ai, MT_CREEP_COLONY, &creep);
    CHECK((ling.roles & AI_ROLE_FIGHTER) && !(ling.roles & AI_ROLE_HITS_AIR) && ling.air_strength == 0);
    CHECK((hydra.roles & AI_ROLE_HITS_AIR) && (hydra.roles & AI_ROLE_HITS_GROUND));
    /* Shields and two blades: a zealot is worth more than two zerglings. */
    CHECK(zealot.hp > 80 && zealot.ground_strength > 2 * ling.ground_strength);
    /* The Goliath fires from its turret. */
    CHECK((goliath.roles & AI_ROLE_FIGHTER) && goliath.ground_strength > 0 && goliath.air_strength > 0);
    CHECK((overlord.roles & (AI_ROLE_SUPPLY | AI_ROLE_DETECTOR | AI_ROLE_FLYER)) ==
          (AI_ROLE_SUPPLY | AI_ROLE_DETECTOR | AI_ROLE_FLYER) && !(overlord.roles & AI_ROLE_FIGHTER));
    CHECK((observer.roles & AI_ROLE_CLOAKED) && (observer.roles & AI_ROLE_DETECTOR));
    CHECK((scv.roles & AI_ROLE_WORKER) && scv.ground_strength < marine.ground_strength);
    CHECK((turret.roles & AI_ROLE_DEFENSE) && (turret.roles & AI_ROLE_HITS_AIR) &&
          !(turret.roles & AI_ROLE_HITS_GROUND));
    CHECK((scourge.roles & AI_ROLE_HITS_AIR) && !(scourge.roles & AI_ROLE_HITS_GROUND));
    /* A Creep Colony is a defense site a Sunken or Spore Colony grows from. */
    CHECK((creep.roles & AI_ROLE_DEFENSE) && !(creep.roles & (AI_ROLE_HITS_GROUND | AI_ROLE_HITS_AIR)));
    return 0;
}

/* ── aiscript.bin ─────────────────────────────────────────────────────── */

/* The retail melee AI (PyMS AIBIN): a table of 16-byte entries, an id such
 * as "TMCu" then the script's offset, its name string and flags, ending in
 * a zero id; scripts are byte code. Brood War's file opens with the
 * table's offset; the one here opens with the table itself. Arguments per
 * opcode: b a byte, w a word (unit, upgrade, tech or address); NULL ends
 * the walk (stop, debug text, a race or random branch). */
static const char *const aiscript_args[] = {
    "w", "ww", "w", "", "", "bw", "bwb", "bwb", "wb", "bw", "bw", "", "bw", "", "", "",       /* 00 */
    "", "", "", "bw", "bw", "bw", "bw", "bw", "bw", "bw", "bw", "", "", "", "", "b",          /* 10 */
    "", "", "b", "", NULL, "", "", NULL, "", "", "", "", "", "b", "w", "",                   /* 20 */
    NULL, NULL, "", "", "", "", "", "w", "", NULL, NULL, "b", "w", "wb", "bw", "w",           /* 30 */
    "w", "", "w", "b", "w", "bw", "bw", "", "w", "bw", "w", "bw", "bw",                       /* 40 */
};
enum { AIS_GOTO = 0x00, AIS_BUILD = 0x06, AIS_GROUNDMAP_JUMP = 0x3c, AIS_CALL = 0x40, AIS_RETURN = 0x41 };

typedef struct { int type, count; } build_line_t;

/* The build lines of a script's main thread, as units.dat rows plus one
 * (our type ids). Gotos and calls are followed, threads it starts are
 * not, and a melee map is a ground map. */
static int aiscript_builds(const blob_t *file, const char *id, build_line_t *out, int cap) {
    const uint8_t *b = file->bytes;
    size_t size = file->size, table = size >= 4 && read_u32_le(b) < size ? read_u32_le(b) : 0, pc = 0;
    for (size_t at = table; at + 16 <= size && read_u32_le(b + at); at += 16)
        if (!memcmp(b + at, id, 4)) pc = read_u32_le(b + at + 4);
    size_t stack[8];
    int depth = 0, n = 0;
    for (int steps = 0; pc && pc < size && n < cap && steps < 4096; ++steps) {
        int op = b[pc], arg[3] = {0};
        if (op >= (int)(sizeof(aiscript_args) / sizeof(*aiscript_args)) || !aiscript_args[op]) break;
        size_t at = pc + 1;
        for (int k = 0; aiscript_args[op][k]; ++k) {
            bool word = aiscript_args[op][k] == 'w';
            if (at + 1 + word > size) return n;
            arg[k] = word ? read_u16_le(b + at) : b[at];
            at += 1 + word;
        }
        pc = at;
        if (op == AIS_GOTO || op == AIS_GROUNDMAP_JUMP) pc = (size_t)arg[0];
        else if (op == AIS_CALL && depth < 8) { stack[depth++] = pc; pc = (size_t)arg[0]; }
        else if (op == AIS_RETURN) { if (!depth) break; pc = stack[--depth]; }
        else if (op == AIS_BUILD && arg[1] < SC_TYPES && (sc_units[arg[1]].flags & SC_UNIT_BUILDING))
            out[n++] = (build_line_t){arg[1] + 1, arg[0]};
    }
    return n;
}

/* The buildings an opening owns: not the start town hall, nor supply or
 * static defense (the doctrine's), and only lines that ask for more. A
 * Hatchery gives supply but is a town hall first. */
static int opening_buildings(const AiContext *ai, const build_line_t *in, int count, build_line_t *out, int cap) {
    int n = 0;
    for (int i = 0; i < count && n < cap; ++i) {
        AiUnitInfo info;
        P_AiUnitInfo(ai, (uint16_t)in[i].type, &info);
        bool hall = (sc_units[in[i].type - 1].flags & 0x1000) != 0;
        if ((hall && in[i].count == 1) || (!hall && (info.roles & AI_ROLE_SUPPLY)) || (info.roles & AI_ROLE_DEFENSE))
            continue;
        bool more = true;
        for (int j = 0; j < n; ++j)
            if (out[j].type == in[i].type && out[j].count >= in[i].count) more = false;
        if (more) out[n++] = in[i];
    }
    return n;
}

/* Our opening's first buildings follow the retail script's order and counts. */
static int opening_follows(const AiContext *ai, int owner, const char *script) {
    enum { FIRST = 5 };
    blob_t file = {0};
    CHECK(sc_read(g_game_default_root, "scripts\\aiscript.bin", &file));
    build_line_t retail[64], ours[64], want[FIRST], have[FIRST];
    int retail_count = aiscript_builds(&file, script, retail, 64), ours_count = 0;
    W_FreeFile(&file);
    const AiPlan *plan = &ai->teams[owner].plan;
    for (int i = 0; i < plan->goal_count && ours_count < 64; ++i) {
        const StaticProductDefinition *p = G_ModelProductByUIId(NULL, plan->goals[i].product);
        if (p && p->product_class == RTS_PRODUCT_BUILDING)
            ours[ours_count++] = (build_line_t){p->product_type, plan->goals[i].count};
    }
    CHECK(opening_buildings(ai, retail, retail_count, want, FIRST) == FIRST);
    CHECK(opening_buildings(ai, ours, ours_count, have, FIRST) == FIRST);
    printf("  %s opening:", script);
    for (int i = 0; i < FIRST; ++i) printf(" %s x%d", sc_units[want[i].type - 1].name, want[i].count);
    puts("");
    CHECK(!memcmp(want, have, sizeof(want)));
    return 0;
}

/* ── one race against an idle human ──────────────────────────────────── */

typedef struct {
    AiStats stats;
    int first_wave_ms, found_ms, fighters, strength, defenses, supply_used, supply_cap;
    int bought[SC_TYPES + 1]; /* purchases by actor type */
    int peak[SC_TYPES + 1];   /* most alive at once */
    bool sieged, bunkered, stormed;
    int town_workers;         /* most workers mining by a second town hall */
    int upgrade_level;        /* highest weapon, armor or shield level researched */
} race_run_t;

static bool alive(const mobj_t *mo, int owner) {
    return mo->thinker.function == P_MobjThinker && mo->owner == owner && mo->hp > 0 && !mo->remove;
}

/* What the computer fields: unit counts, tanks in siege mode, Marines in
 * a Bunker, a storm on the field, workers mining at an expansion. */
static void observe(int owner, const mobj_t *start_hall, race_run_t *out) {
    int count[SC_TYPES + 1] = {0};
    fvec2_t start = fixed3_xy_to_fvec2(start_hall->core.position);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        /* A storm is a neutral effect area that keeps its caster's owner. */
        out->stormed |= th->function == P_MobjThinker && mo->type_id == MT_MAP_REVEALER && mo->sc.order.tech == SC_TECH_PSIONIC_STORM &&
            mo->sc.attacker == owner;
        if (!alive(mo, owner) || mo->type_id > SC_TYPES) continue;
        ++count[mo->type_id];
        out->sieged |= mo->type_id == MT_SIEGE_MODE;
        out->bunkered |= (mo->sc.flags & SC_LOADED) && mo->type_id == MT_MARINE;
        fvec2_t hall = fixed3_xy_to_fvec2(mo->core.position);
        if (!(mo->traits & MF_RESOURCE_BASE) || fvec2_distance_squared(hall, start) < 144.0f) continue;
        int workers = 0;
        for (thinker_t *w = thinkercap.next; w != &thinkercap; w = w->next) {
            const mobj_t *u = (const mobj_t *)w;
            if (alive(u, owner) && u->harvest.phase != HARVEST_PHASE_NONE && u->harvest.target >= 0 &&
                u->harvest.target < level.resource_vent_count &&
                fvec2_distance_squared(level.resource_vents[u->harvest.target].attachment, hall) < 144.0f) ++workers;
        }
        if (workers > out->town_workers) out->town_workers = workers;
    }
    for (int t = 1; t <= SC_TYPES; ++t) if (count[t] > out->peak[t]) out->peak[t] = count[t];
    /* upgrades.dat 0..15: the weapons, armor and shields of each race. */
    for (int u = 0; u < 16; ++u)
        if (level.upgrades[u][owner].weapon > out->upgrade_level) out->upgrade_level = level.upgrades[u][owner].weapon;
}

/* Fourteen simulated minutes of one computer race (CHK side) against an idle human. */
static int play(int race, race_run_t *out) {
    static const char road[] = "maps/(2)road war.scm/staredit/scenario.chk";
    static const char *const script[3] = {"ZMCu", "TMCu", "PMCu"};
    int kinds[8] = {SC_SLOT_HUMAN, SC_SLOT_COMPUTER, SC_SLOT_CLOSED, SC_SLOT_CLOSED,
                    SC_SLOT_CLOSED, SC_SLOT_CLOSED, SC_SLOT_CLOSED, SC_SLOT_CLOSED};
    /* The lobby counts Terran, Zerg, Protoss; CHK sides Zerg, Terran, Protoss. */
    int races[8] = {0, race == 2 ? 2 : !race};
    sc_set_custom_slots(kinds, races);
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = g_game_default_root, .map_path = road };
    CHECK(model && rts_game_model_load(model, &config));
    AiContext *ai = rts_game_model_ai(model);
    CHECK(ai && ai->game == G_AiInterface());
    if (race == 0) CHECK(unit_knowledge(ai) == 0);
    int computer = -1;
    for (int owner = 0; owner < 8; ++owner)
        if (owner != consoleplayer && G_AiInterface()->player_level(&level, owner) == AI_LEVEL_NORMAL) computer = owner;
    CHECK(computer >= 0 && sc_player_side(computer) == race);
    /* The idle human cannot fall, so the game runs its full length and
     * every wave meets a base to fight. */
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (((mobj_t *)th)->owner == consoleplayer) ((mobj_t *)th)->sc.flags |= SC_INVINCIBLE;
    memset(out, 0, sizeof(*out));
    out->first_wave_ms = -1;
    uint32_t start_hall = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap && !start_hall; th = th->next)
        if (alive((const mobj_t *)th, computer) && (((const mobj_t *)th)->traits & MF_RESOURCE_BASE))
            start_hall = ((const mobj_t *)th)->id;
    CHECK(start_hall);
    for (int t = 0; t < RTS_TICRATE * 60 * 14; ++t) {
        CHECK(rts_game_model_tick(model, RTS_FIXED_DT));
        AiEvent event;
        while (P_AiPollEvent(ai, &event)) {
            if (event.owner != computer) continue;
            if (getenv("AI_TRACE") && event.type != AI_EVENT_HARVEST_ASSIGNED)
                printf("    %6d ms ev %d val %d  res %d/%d\n", event.time_ms, event.type, event.value,
                       level.player_resources[computer][0], level.player_resources[computer][1]);
            if (event.type == AI_EVENT_WAVE_LAUNCHED && out->first_wave_ms < 0) out->first_wave_ms = event.time_ms;
            const StaticProductDefinition *p = G_ModelProductByUIId(NULL, event.value);
            if (event.type == AI_EVENT_PURCHASE && p && p->product_type <= SC_TYPES) ++out->bought[p->product_type];
        }
        const mobj_t *hall = P_MobjById(start_hall);
        if (t % RTS_TICRATE == 0 && hall) observe(computer, hall, out);
    }
    CHECK(opening_follows(ai, computer, script[race]) == 0);
    out->stats = *P_AiStats(ai, computer);
    out->found_ms = ai->teams[computer].found_ms;
    G_AiInterface()->supply(computer, &out->supply_used, &out->supply_cap);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (!alive(mo, computer)) continue;
        AiUnitInfo info;
        P_AiUnitInfo(ai, mo->type_id, &info);
        /* The army, not the workers that can also fight. */
        if ((info.roles & AI_ROLE_FIGHTER) && !(info.roles & AI_ROLE_WORKER)) {
            ++out->fighters;
            out->strength += info.ground_strength;
        }
        if ((info.roles & AI_ROLE_DEFENSE) && (info.roles & (AI_ROLE_HITS_GROUND | AI_ROLE_HITS_AIR))) ++out->defenses;
    }
    printf("  race %d: buys %d, waves %d (first %d ms), enemy base seen at %d ms, holds %d, retreats %d, "
           "fighters %d (strength %d), defenses %d, supply %d/%d, expansions %d (%d workers), upgrades %d (level %d)\n",
           race, out->stats.purchases, out->stats.waves, out->first_wave_ms, out->found_ms, out->stats.holds,
           out->stats.retreats, out->fighters, out->strength, out->defenses, out->supply_used, out->supply_cap,
           out->stats.expansions, out->town_workers, out->stats.upgrades, out->upgrade_level);
    static const uint16_t restored[] = {MT_ULTRALISK, MT_GUARDIAN, MT_SUNKEN_COLONY, MT_SPORE_COLONY, MT_BUNKER,
                                        MT_SIEGE_TANK, MT_CARRIER, MT_REAVER, MT_HIGH_TEMPLAR};
    printf("   restored units (most at once):");
    for (size_t i = 0; i < sizeof(restored) / sizeof(*restored); ++i)
        if (out->peak[restored[i]]) printf(" %s %d", sc_units[restored[i] - 1].name, out->peak[restored[i]]);
    printf("%s%s%s\n", out->sieged ? ", sieged" : "", out->bunkered ? ", bunkered" : "", out->stormed ? ", stormed" : "");
    rts_game_model_destroy(model);
    sc_set_custom_slots(NULL, NULL);
    return 0;
}

/* Zerg against Protoss on a three-seat map, the human seat idle: the two
 * computers scout each other, so waves wait for strength and back off. */
static int duel(AiStats *zerg, AiStats *protoss) {
    static const char map[] = "maps/(3)holy ground.scm/staredit/scenario.chk";
    int kinds[8] = {SC_SLOT_HUMAN, SC_SLOT_COMPUTER, SC_SLOT_COMPUTER, SC_SLOT_CLOSED,
                    SC_SLOT_CLOSED, SC_SLOT_CLOSED, SC_SLOT_CLOSED, SC_SLOT_CLOSED};
    int races[8] = {0, 1, 2};
    sc_set_custom_slots(kinds, races);
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = { .data_root = g_game_default_root, .map_path = map };
    CHECK(model && rts_game_model_load(model, &config));
    AiContext *ai = rts_game_model_ai(model);
    bool computer[8] = {0};
    for (int owner = 0; owner < 8; ++owner)
        computer[owner] = owner != consoleplayer && G_AiInterface()->player_level(&level, owner) == AI_LEVEL_NORMAL;
    /* The idle seat only watches: it cannot fall and end the match. */
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (((mobj_t *)th)->owner == consoleplayer) ((mobj_t *)th)->sc.flags |= SC_INVINCIBLE;
    for (int t = 0; t < RTS_TICRATE * 60 * 15; ++t) CHECK(rts_game_model_tick(model, RTS_FIXED_DT));
    for (int owner = 0; owner < 8; ++owner) {
        if (!computer[owner]) continue;
        const AiStats *stats = P_AiStats(ai, owner);
        const AiTeamState *team = &ai->teams[owner];
        printf("  duel side %d: buys %d, waves %d, holds %d, retreats %d, expansions %d, enemy estimate %d (air %d%%)\n",
               sc_player_side(owner), stats->purchases, stats->waves, stats->holds, stats->retreats,
               stats->expansions, team->enemy_strength, team->enemy_air_pct);
        if (sc_player_side(owner) == 0) *zerg = *stats;
        if (sc_player_side(owner) == 2) *protoss = *stats;
    }
    rts_game_model_destroy(model);
    sc_set_custom_slots(NULL, NULL);
    return 0;
}

/* A race plays in a child process that hands its run back through a pipe:
 * the long games are independent, so the three races and the duel play
 * at once. */
static pid_t play_apart(int race, int *fd) {
    int ends[2];
    if (pipe(ends)) return -1;
    fflush(stdout);
    pid_t child = fork();
    if (child == 0) {
        race_run_t run;
        int failed = play(race, &run);
        if (!failed && write(ends[1], &run, sizeof(run)) != (ssize_t)sizeof(run)) failed = 1;
        fflush(stdout);
        _exit(failed);
    }
    close(ends[1]);
    *fd = ends[0];
    return child;
}

static int collect(pid_t child, int fd, race_run_t *out) {
    size_t got = 0;
    for (ssize_t n; got < sizeof(*out) && (n = read(fd, (char *)out + got, sizeof(*out) - got)) > 0;) got += (size_t)n;
    close(fd);
    int status = 0;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(got == sizeof(*out));
    return 0;
}

int main(void) {
    race_run_t zerg, terran, protoss, *runs[] = {&zerg, &terran, &protoss};
    /* AI_RACE=n plays one race only, for tuning. */
    if (getenv("AI_RACE")) return play(atoi(getenv("AI_RACE")) % 3, runs[atoi(getenv("AI_RACE")) % 3]);
    pid_t child[3];
    int fd[3];
    for (int i = 0; i < 3; ++i) CHECK((child[i] = play_apart(i, &fd[i])) > 0);
    AiStats duel_zerg = {0}, duel_protoss = {0};
    CHECK(duel(&duel_zerg, &duel_protoss) == 0);
    for (int i = 0; i < 3; ++i) CHECK(collect(child[i], fd[i], runs[i]) == 0);
    /* Both sides attack; seeing a stronger enemy makes waves wait, and
     * losing fights sends them home. */
    CHECK(duel_zerg.waves >= 1 && duel_protoss.waves >= 1);
    CHECK(duel_zerg.holds + duel_protoss.holds > 0);
    CHECK(duel_zerg.retreats + duel_protoss.retreats > 0);
    for (int i = 0; i < 3; ++i) {
        const race_run_t *run = runs[i];
        CHECK(run->stats.purchases >= 20);
        CHECK(run->stats.waves >= 1 && run->fighters + run->stats.wave_units > 0);
        /* Supply stays ahead of the army. */
        CHECK(run->supply_cap > 10 && run->supply_used <= run->supply_cap);
        /* A scout finds the enemy base before the first wave sets out. */
        CHECK(run->stats.scouts >= 1 && run->found_ms > 0 && run->found_ms < run->first_wave_ms);
        /* A second town at other minerals, and workers mining there. */
        CHECK(run->stats.expansions >= 1 && run->town_workers >= 3);
        /* The research ladder reaches second-level upgrades. */
        CHECK(run->stats.upgrades >= 2 && run->upgrade_level >= 2);
    }
    /* The units the rosters gained back are bought and fielded: Zerg
     * Ultralisks, Guardians and Sunken Colonies; Terran Bunkers manned by
     * Marines and Siege Tanks that siege; Protoss Carriers, Reavers and
     * High Templar that storm. */
    CHECK(zerg.peak[MT_ULTRALISK] > 0 && zerg.peak[MT_GUARDIAN] > 0 && zerg.peak[MT_SUNKEN_COLONY] > 0);
    CHECK(terran.bought[MT_BUNKER] > 0 && terran.bunkered && terran.bought[MT_SIEGE_TANK] > 0 && terran.sieged);
    CHECK(protoss.peak[MT_CARRIER] > 0 && protoss.peak[MT_REAVER] > 0 && protoss.peak[MT_HIGH_TEMPLAR] > 0 &&
          protoss.stormed);
    /* The doctrines show: Zerg strikes first, Protoss fields the strongest
     * units, Terran rings its base with bunkers and turrets, Protoss guards
     * with cannons and Zerg with sunkens. (A zergling is worth a marine;
     * with workers left out of the army, Zerg is not weaker per unit than
     * Terran.) */
    CHECK(zerg.first_wave_ms > 0 && zerg.first_wave_ms < terran.first_wave_ms &&
          zerg.first_wave_ms < protoss.first_wave_ms);
    CHECK(protoss.strength / protoss.fighters > terran.strength / terran.fighters &&
          protoss.strength / protoss.fighters > zerg.strength / zerg.fighters);
    CHECK(terran.defenses >= 2 && protoss.defenses >= 1 && zerg.defenses >= 1);
    puts("PASS: unit roles and strengths from DAT, retail openings, race doctrines for supply, workers, scouting, "
         "expansions, research, defenses, army mix, waves, holds and retreats");
    return 0;
}
