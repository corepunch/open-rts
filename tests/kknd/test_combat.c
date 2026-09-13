#include "game.h"
#include "info.h"
#include "p_local.h"
#include "rts_model_test.h"

#define CHECK(c) RTS_CHECK(c, "KKND opening combat", #c)

static int opening_encounter(void) {
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/KKND"};
    CHECK(model && rts_game_model_load(model, &config));
    mobj_t *rifleman = NULL, *enemy = NULL;
    float nearest = INFINITY;
    for (thinker_t *a = thinkercap.next; a != &thinkercap; a = a->next) {
        mobj_t *unit = (mobj_t *)a;
        if (unit->type_id != MT_SURV_RIFLEMAN || unit->owner != 0) continue;
        for (thinker_t *b = thinkercap.next; b != &thinkercap; b = b->next) {
            mobj_t *target = (mobj_t *)b;
            if (target->owner != 1) continue;
            float distance = fvec2_distance_squared(fixed3_xy_to_fvec2(unit->core.position),
                                                    fixed3_xy_to_fvec2(target->core.position));
            if (distance < nearest) {nearest = distance; rifleman = unit; enemy = target;}
        }
    }
    CHECK(rifleman && enemy && enemy->type_id == MT_MUTE_BERSERKER);
    fvec2_t start = fixed3_xy_to_fvec2(enemy->core.position);
    uint32_t player_id = rifleman->id, enemy_id = enemy->id;
    int hp = enemy->hp;
    rifleman->attack.target = enemy;
    CHECK(P_MoveUnitTo(&level, rifleman, start));
    bool hit = false, returned_fire = false, advanced = false, dying = false;
    RtsRenderSnapshot snapshot;
    for (int tic = 0; tic < 1800 && !dying; ++tic) {
        CHECK(rts_tick(model, &snapshot));
        int pi = rts_find_unit_by_id(&snapshot, player_id);
        int ei = rts_find_unit_by_id(&snapshot, enemy_id);
        CHECK(pi >= 0 && ei >= 0);
        if (snapshot.units[ei].hp < hp) {
            hit = true;
            if (snapshot.units[ei].hp > 0) CHECK(enemy->attack.target);
        }
        advanced |= fvec2_distance_squared(snapshot.units[ei].position, start) > 0.25f;
        returned_fire |= snapshot.units[pi].hp < snapshot.units[pi].max_hp;
        dying = snapshot.units[ei].hp == 0;
    }
    CHECK(hit && advanced && returned_fire && dying);
    CHECK(!enemy->remove && enemy->core.sprite_id == SPR_EXTRAS);
    CHECK(!(enemy->traits & (MF_ATTACK | MF_MOBILE | MF_SELECTABLE)));
    rts_game_model_destroy(model);
    puts("PASS: native first encounter advances, returns fire, and enters a visible death state");
    return 0;
}

static int death_sequences(void) {
    static const struct {int type, sprite, first, length;} cases[] = {
        {MT_SURV_RIFLEMAN, SPR_EXTRAS, 128, 15},
        {MT_MUTE_BERSERKER, SPR_EXTRAS, 143, 15},
        {MT_MUTE_DIRE_WOLF, SPR_MUTE_DIRE_WOLF, 2, 13},
        {MT_SURV_DIRT_BIKE, SPR_EXTRAS, 44, 13},
        {MT_SURV_4X4_PICKUP, SPR_EXTRAS, 44, 13},
    };
    P_InitThinkers();
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        mobj_t *unit = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){8,8},0), cases[i].type);
        CHECK(unit);
        fixed3_t position = unit->core.position;
        P_DamageMobj(unit, NULL, unit->hp);
        int ticks = 0;
        for (int frame = 0; frame < cases[i].length; ++frame) {
            CHECK(!unit->remove && unit->hp == 0);
            CHECK(unit->core.sprite_id == cases[i].sprite && unit->core.frame == cases[i].first + frame);
            CHECK(memcmp(&position, &unit->core.position, sizeof(position)) == 0);
            int tics = unit->core.tics;
            CHECK(tics > 0);
            for (int t = 0; t < tics; ++t) {P_MobjThinker(unit); ++ticks;}
        }
        CHECK(unit->remove);
        CHECK(ticks == (cases[i].length * 120 * 30 + 999) / 1000);
    }
    P_FreeLevel(&level);
    puts("PASS: every opening unit plays its complete native death sequence before removal");
    return 0;
}

int main(void) {
    RTS_RUN(opening_encounter());
    RTS_RUN(death_sequences());
    return 0;
}
