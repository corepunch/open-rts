#include "t_local.h"
#include "starcraft.h"
#include "sc_local.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define CHECK(c) RTS_CHECK(c,"StarCraft computer player",#c)

/* What the shared AI reads from units.dat and weapons.dat through the
 * game's describe(): roles, anti-air and Brood War strengths. */
static int unit_knowledge(const AiContext *ai) {
    AiUnitInfo ling, hydra, zealot, goliath, overlord, observer, scv, marine, turret, scourge;
    P_AiUnitInfo(ai, MT_ZERGLING, &ling);
    P_AiUnitInfo(ai, MT_HYDRALISK, &hydra);
    P_AiUnitInfo(ai, MT_ZEALOT, &zealot);
    P_AiUnitInfo(ai, MT_GOLIATH, &goliath);
    P_AiUnitInfo(ai, MT_OVERLORD, &overlord);
    P_AiUnitInfo(ai, MT_OBSERVER, &observer);
    P_AiUnitInfo(ai, MT_SCV, &scv);
    P_AiUnitInfo(ai, MT_MARINE, &marine);
    P_AiUnitInfo(ai, MT_MISSILE_TURRET, &turret);
    P_AiUnitInfo(ai, 48, &scourge);
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
    return 0;
}

typedef struct {
    AiStats stats;
    int first_wave_ms, fighters, strength, defenses, supply_used, supply_cap;
} race_run_t;

/* Ten simulated minutes of one computer race (CHK side) against an idle human. */
static int play(int race, race_run_t *out) {
    static const char road[] = "maps/(2)road war.scm/staredit/scenario.chk";
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
    memset(out, 0, sizeof(*out));
    out->first_wave_ms = -1;
    for (int t = 0; t < RTS_TICRATE * 60 * 10; ++t) {
        CHECK(rts_game_model_tick(model, RTS_FIXED_DT));
        AiEvent event;
        while (P_AiPollEvent(ai, &event)) {
            if (getenv("AI_TRACE") && event.owner == computer && event.type != AI_EVENT_HARVEST_ASSIGNED)
            {
                int u = 0, c = 0;
                G_AiInterface()->supply(computer, &u, &c);
                printf("    %6d ms ev %d val %d  res %d/%d supply %d/%d\n", event.time_ms, event.type, event.value,
                       level.player_resources[computer][0], level.player_resources[computer][1], u, c);
            }
            if (event.type == AI_EVENT_WAVE_LAUNCHED && event.owner == computer && out->first_wave_ms < 0)
                out->first_wave_ms = event.time_ms;
        }
    }
    if (getenv("AI_TRACE")) for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (th->function != P_MobjThinker || mo->owner != computer || mo->hp <= 0 || mo->remove) continue;
        printf("    unit %3d at %.0f,%.0f move %d target %d harvest %d prod %d\n", mo->type_id,
               fixed_to_float(mo->core.position.x), fixed_to_float(mo->core.position.y),
               P_HasMoveOrder(mo), mo->attack.target != NULL, mo->harvest.phase,
               mo->production ? mo->production->product_type : 0);
    }
    out->stats = *P_AiStats(ai, computer);
    G_AiInterface()->supply(computer, &out->supply_used, &out->supply_cap);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (th->function != P_MobjThinker || mo->owner != computer || mo->hp <= 0 || mo->remove) continue;
        AiUnitInfo info;
        P_AiUnitInfo(ai, mo->type_id, &info);
        /* The army, not the workers that can also fight. */
        if ((info.roles & AI_ROLE_FIGHTER) && !(info.roles & AI_ROLE_WORKER)) {
            ++out->fighters;
            out->strength += info.ground_strength;
        }
        if (info.roles & AI_ROLE_DEFENSE) ++out->defenses;
    }
    printf("  race %d: buys %d, waves %d (first %d ms), holds %d, retreats %d, fighters %d "
           "(strength %d), defenses %d, supply %d/%d\n", race, out->stats.purchases, out->stats.waves,
           out->first_wave_ms, out->stats.holds, out->stats.retreats, out->fighters, out->strength,
           out->defenses, out->supply_used, out->supply_cap);
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
        printf("  duel side %d: buys %d, waves %d, holds %d, retreats %d, enemy estimate %d (air %d%%)\n",
               sc_player_side(owner), stats->purchases, stats->waves, stats->holds, stats->retreats,
               team->enemy_strength, team->enemy_air_pct);
        if (sc_player_side(owner) == 0) *zerg = *stats;
        if (sc_player_side(owner) == 2) *protoss = *stats;
    }
    rts_game_model_destroy(model);
    sc_set_custom_slots(NULL, NULL);
    return 0;
}

int main(void) {
    AiStats duel_zerg = {0}, duel_protoss = {0};
    CHECK(duel(&duel_zerg, &duel_protoss) == 0);
    /* Both sides attack; seeing a stronger enemy makes waves wait (Zerg
     * lings against shielded zealots), and losing fights sends them home. */
    CHECK(duel_zerg.waves >= 1 && duel_protoss.waves >= 1);
    CHECK(duel_zerg.holds + duel_protoss.holds > 0);
    CHECK(duel_zerg.retreats + duel_protoss.retreats > 0);
    race_run_t zerg, terran, protoss;
    CHECK(play(0, &zerg) == 0);
    CHECK(play(1, &terran) == 0);
    CHECK(play(2, &protoss) == 0);
    const race_run_t *all[] = {&zerg, &terran, &protoss};
    for (int i = 0; i < 3; ++i) {
        CHECK(all[i]->stats.purchases >= 20);
        CHECK(all[i]->stats.waves >= 1 && all[i]->fighters + all[i]->stats.wave_units > 0);
        /* Supply stays ahead of the army. */
        CHECK(all[i]->supply_cap > 10 && all[i]->supply_used <= all[i]->supply_cap);
    }
    /* The doctrines show: Zerg strikes first, Protoss fields the strongest
     * units, Terran rings its base with turrets and Protoss guards with
     * cannons. (A zergling is worth a marine; with workers left out of the
     * army, Zerg is not weaker per unit than Terran.) */
    CHECK(zerg.first_wave_ms > 0 && zerg.first_wave_ms < terran.first_wave_ms &&
          zerg.first_wave_ms < protoss.first_wave_ms);
    CHECK(protoss.strength / protoss.fighters > terran.strength / terran.fighters &&
          protoss.strength / protoss.fighters > zerg.strength / zerg.fighters);
    CHECK(terran.defenses >= 2 && protoss.defenses >= 1 && zerg.defenses == 0);
    puts("PASS: unit roles and strengths from DAT, race doctrines for supply, workers, defenses, army mix, waves, holds and retreats");
    return 0;
}
